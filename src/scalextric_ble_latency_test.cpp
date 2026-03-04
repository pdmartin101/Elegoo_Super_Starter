#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>

// BLE Latency Test - mirrors the real BLE relay code structure exactly
// Uses a timer interrupt to simulate ESP-NOW callback (queue + millis capture)
// Then loop() flushes the queue via BLE notify - identical to the relay
// No WiFi, no ESP-NOW - just BLE, to test if coexistence causes the delay
//
// Same UUIDs and format as the real BLE relay so the client works unchanged

#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define EVENT_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define SYNC_CHAR_UUID      "a1b2c3d4-e5f6-7890-abcd-ef1234567892"

BLECharacteristic* eventCharacteristic = nullptr;
BLECharacteristic* syncCharacteristic = nullptr;
bool clientConnected = false;

// Event queue - identical structure to the real relay
const int EVENT_QUEUE_SIZE = 8;
struct FakeEvent {
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
};
FakeEvent eventQueue[EVENT_QUEUE_SIZE];
uint32_t eventReceiveMs[EVENT_QUEUE_SIZE];  // millis() when "received"
volatile int eventQueueCount = 0;

// Pending SYNC response
volatile bool syncPending = false;

// Sequence number for drop detection
uint32_t seqNumber = 0;
int eventCount = 0;

// Timer for simulating ESP-NOW callback
hw_timer_t* timer = nullptr;

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

// Timer ISR - simulates ESP-NOW onDataReceived callback
// Fires every 1 second, queues a fake event with millis() timestamp
void IRAM_ATTR onTimer() {
  if (!clientConnected) return;
  if (eventQueueCount >= EVENT_QUEUE_SIZE) return;

  int idx = eventQueueCount;
  int car = (eventCount % 6) + 1;
  eventQueue[idx].nodeId = 255;
  eventQueue[idx].sensorId = 0;
  eventQueue[idx].carNumber = car;
  eventQueue[idx].frequency = (uint16_t[]){5500, 4400, 3700, 3100, 2800, 2400}[car - 1];
  eventReceiveMs[idx] = millis();
  eventQueueCount++;
  eventCount++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n# BLE Latency Test (relay-mirror)");
  Serial.println("# ================================");

  BLEDevice::init("Scalextric-Relay");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* service = server->createService(SERVICE_UUID);

  eventCharacteristic = service->createCharacteristic(
    EVENT_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  eventCharacteristic->addDescriptor(new BLE2902());

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

  // Timer interrupt every 1 second to simulate ESP-NOW callback
  timer = timerBegin(0, 80, true);  // 80 prescaler = 1MHz (1us ticks)
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000, true);  // 1 second
  timerAlarmEnable(timer);

  Serial.println("# BLE advertising as 'Scalextric-Relay'");
  Serial.println("# Format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS");
  Serial.println("# Timer fires every 1s (simulating ESP-NOW callback)...\n");
}

void loop() {
  // Flush queued events via BLE notification - identical to real relay
  if (clientConnected && eventQueueCount > 0) {
    for (int i = 0; i < eventQueueCount; i++) {
      FakeEvent& event = eventQueue[i];
      char msg[64];
      snprintf(msg, sizeof(msg), "%lu:%d:%d:%d:%d:%lu",
               seqNumber++, event.nodeId, event.sensorId, event.carNumber,
               event.frequency, (unsigned long)eventReceiveMs[i]);
      eventCharacteristic->setValue(msg);
      eventCharacteristic->notify();
    }
  }
  eventQueueCount = 0;

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
