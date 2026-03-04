#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>

// Scalextric BLE Bridge (ESP32-B of split relay)
// Reads event lines from Serial2 (sent by ESP32-A running scalextric_espnow_receiver)
// Sends BLE notifications to desktop client
//
// NO WiFi on this board — dedicated BLE radio, no coexistence delay
// Wiring: GPIO26 (RX2) ← ESP32-A GPIO25 (TX2), plus shared GND
//
// Input format:  NODE:SENSOR:CAR:FREQ\n
// Output format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS
//
// SYNC handled entirely by this board using its own millis() — consistent with event timestamps

#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define EVENT_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define SYNC_CHAR_UUID      "a1b2c3d4-e5f6-7890-abcd-ef1234567892"

BLECharacteristic* eventCharacteristic = nullptr;
BLECharacteristic* syncCharacteristic = nullptr;
bool clientConnected = false;

// Parsed event queue
const int EVENT_QUEUE_SIZE = 8;
struct ParsedEvent {
  int nodeId;
  int sensorId;
  int carNumber;
  int frequency;
};
ParsedEvent eventQueue[EVENT_QUEUE_SIZE];
uint32_t eventReceiveMs[EVENT_QUEUE_SIZE];  // millis() when Serial2 line arrived
volatile int eventQueueCount = 0;

// Pending SYNC response
volatile bool syncPending = false;

// Sequence number for drop detection
uint32_t seqNumber = 0;

// BLE keepalive - sends a minimal notification every 1s to prevent
// Windows from increasing the connection interval during idle periods
hw_timer_t* keepaliveTimer = nullptr;
volatile bool keepalivePending = false;

void IRAM_ATTR onKeepalive() {
  if (clientConnected) {
    keepalivePending = true;
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server, esp_ble_gatts_cb_param_t* param) override {
    clientConnected = true;
    // Request fast connection interval: 7.5ms min, 15ms max
    esp_ble_conn_update_params_t connParams = {};
    memcpy(connParams.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    connParams.min_int = 6;   // 7.5ms
    connParams.max_int = 12;  // 15ms
    connParams.latency = 0;
    connParams.timeout = 400; // 4s supervision timeout
    esp_ble_gap_update_conn_params(&connParams);
    Serial.println("Client connected");
  }
  void onDisconnect(BLEServer* server) override {
    clientConnected = false;
    server->getAdvertising()->start();
    Serial.println("Client disconnected, re-advertising");
  }
};

class SyncCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.find("SYNC") != std::string::npos) {
      syncPending = true;
    }
  }
};

// Serial2 line buffer
char lineBuf[64];
int linePos = 0;

bool parseLine(const char* line, ParsedEvent& out) {
  // Format: NODE:SENSOR:CAR:FREQ
  int node, sensor, car, freq;
  if (sscanf(line, "%d:%d:%d:%d", &node, &sensor, &car, &freq) == 4) {
    out.nodeId = node;
    out.sensorId = sensor;
    out.carNumber = car;
    out.frequency = freq;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n# Scalextric BLE Bridge (split relay - ESP32-B)");
  Serial.println("# =============================================");

  // Serial2 for inter-board communication from ESP-NOW receiver
  Serial2.begin(115200, SERIAL_8N1, /*RX=*/26, /*TX=*/25);
  Serial.println("# Serial2: RX=GPIO26 ← ESP-NOW receiver TX=GPIO25");

  // Init BLE (no WiFi!)
  BLEDevice::init("Scalextric-Bridge");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  // Event characteristic - notify only
  eventCharacteristic = service->createCharacteristic(
    EVENT_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  eventCharacteristic->addDescriptor(new BLE2902());

  // Sync characteristic - write + notify
  syncCharacteristic = service->createCharacteristic(
    SYNC_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
  );
  syncCharacteristic->addDescriptor(new BLE2902());
  syncCharacteristic->setCallbacks(new SyncCallbacks());

  service->start();

  BLEAdvertising* advertising = server->getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.println("# BLE: advertising as 'Scalextric-Bridge'");

  // Keepalive timer: prevents Windows BLE CI drift during idle periods
  keepaliveTimer = timerBegin(0, 80, true);  // 80 prescaler = 1MHz (1us ticks)
  timerAttachInterrupt(keepaliveTimer, &onKeepalive, true);
  timerAlarmWrite(keepaliveTimer, 1000000, true);  // 1 second
  timerAlarmEnable(keepaliveTimer);
  Serial.println("# KEEPALIVE: 1s interval");

  Serial.println("# Format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS");
  Serial.println("# Waiting for events from ESP-NOW receiver...\n");
}

void loop() {
  // Read lines from Serial2 and queue parsed events
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n' || c == '\r') {
      if (linePos > 0) {
        lineBuf[linePos] = '\0';
        ParsedEvent parsed;
        if (parseLine(lineBuf, parsed) && eventQueueCount < EVENT_QUEUE_SIZE) {
          eventQueue[eventQueueCount] = parsed;
          eventReceiveMs[eventQueueCount] = millis();
          eventQueueCount++;
        }
        linePos = 0;
      }
    } else if (linePos < (int)sizeof(lineBuf) - 1) {
      lineBuf[linePos++] = c;
    }
  }

  // Flush queued events via BLE notification
  if (clientConnected && eventQueueCount > 0) {
    for (int i = 0; i < eventQueueCount; i++) {
      ParsedEvent& event = eventQueue[i];
      char msg[64];
      snprintf(msg, sizeof(msg), "%lu:%d:%d:%d:%d:%lu",
               seqNumber++, event.nodeId, event.sensorId, event.carNumber,
               event.frequency, (unsigned long)eventReceiveMs[i]);
      eventCharacteristic->setValue(msg);
      eventCharacteristic->notify();
    }
  }
  eventQueueCount = 0;

  // Send keepalive ping to maintain short connection interval
  if (keepalivePending) {
    keepalivePending = false;
    char ping[16];
    snprintf(ping, sizeof(ping), "PING:%lu", millis());
    syncCharacteristic->setValue(ping);
    syncCharacteristic->notify();
  }

  // Handle pending SYNC response
  if (syncPending) {
    syncPending = false;
    char reply[32];
    snprintf(reply, sizeof(reply), "SYNC:%lu", millis());
    syncCharacteristic->setValue(reply);
    syncCharacteristic->notify();
  }

  delay(1);  // Give BLE stack time to process
}
