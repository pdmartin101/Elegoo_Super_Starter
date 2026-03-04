#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include "scalextric_protocol.h"
#include "car_detection.h"

// Scalextric BLE Local - standalone single-board parent
// Local sensors + BLE output + OLED display, no WiFi/ESP-NOW
// For use without child nodes — ~3ms latency
// Includes a 1s keepalive to prevent Windows BLE CI drift
//
// Output format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS

#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define EVENT_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define SYNC_CHAR_UUID      "a1b2c3d4-e5f6-7890-abcd-ef1234567892"

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool hasDisplay = false;

// BLE
BLECharacteristic* eventCharacteristic = nullptr;
BLECharacteristic* syncCharacteristic = nullptr;
bool clientConnected = false;

// Display state
volatile bool displayNeedsUpdate = false;
CarEvent lastEvent;

// Per-car detection counts (index 0 = car 1, etc.)
const int NUM_CARS = 6;
int carCounts[NUM_CARS] = {0};
int totalDetections = 0;

// Event queue
const int EVENT_QUEUE_SIZE = 8;
CarEvent eventQueue[EVENT_QUEUE_SIZE];
uint32_t eventReceiveMs[EVENT_QUEUE_SIZE];
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
    esp_ble_conn_update_params_t connParams = {};
    memcpy(connParams.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
    connParams.min_int = 6;   // 7.5ms
    connParams.max_int = 12;  // 15ms
    connParams.latency = 0;
    connParams.timeout = 400;
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

void onLocalCarDetected(uint8_t sensorId, int car, float freq) {
  CarEvent event;
  event.nodeId = PARENT_NODE_ID;
  event.sensorId = sensorId;
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  lastEvent = event;
  if (car >= 1 && car <= NUM_CARS) {
    carCounts[car - 1]++;
  }
  totalDetections++;
  displayNeedsUpdate = true;

  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    eventQueue[eventQueueCount] = event;
    eventReceiveMs[eventQueueCount] = millis();
    eventQueueCount++;
  }
}

void updateDisplay() {
  display.clearDisplay();

  // Header (size 1)
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("Local  S:%d  #%d", NUM_SENSORS, totalDetections);

  // Big car number (size 3 = 18x24px)
  display.setTextSize(3);
  display.setCursor(0, 12);
  display.printf("Car %d", lastEvent.carNumber);

  // Frequency and sensor (size 1)
  display.setTextSize(1);
  display.setCursor(0, 40);
  display.printf("%d Hz  Sensor %d", lastEvent.frequency, lastEvent.sensorId);

  // Per-car detection counts
  display.setCursor(0, 48);
  display.printf("1:%d 2:%d 3:%d", carCounts[0], carCounts[1], carCounts[2]);
  display.setCursor(0, 56);
  display.printf("4:%d 5:%d 6:%d", carCounts[3], carCounts[4], carCounts[5]);

  display.display();
}

void displayTask(void* param) {
  for (;;) {
    if (displayNeedsUpdate) {
      displayNeedsUpdate = false;
      updateDisplay();
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n# Scalextric BLE Local (sensors + BLE + OLED, no WiFi)");
  Serial.println("# ====================================================");

  // Init OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    hasDisplay = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Scalextric BLE Local");
    display.println("Starting...");
    display.display();
    Serial.println("# OLED: OK");
  } else {
    Serial.println("# OLED: not found (continuing without display)");
  }

  // Setup local sensors
  initSensors();
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.printf("# S%d - GPIO %d\n", i, SENSOR_PINS[i]);
  }

  // Init BLE
  BLEDevice::init("Scalextric-Local");
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

  Serial.println("# BLE: advertising as 'Scalextric-Local'");

  // Keepalive timer: prevents Windows BLE CI drift during idle periods
  keepaliveTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(keepaliveTimer, &onKeepalive, true);
  timerAlarmWrite(keepaliveTimer, 1000000, true);  // 1 second
  timerAlarmEnable(keepaliveTimer);
  Serial.println("# KEEPALIVE: 1s interval");

  if (hasDisplay) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Scalextric BLE Local");
    display.println("BLE: advertising");
    display.println("Waiting for cars...");
    display.display();

    xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 1, NULL, 0);
    Serial.println("# OLED: running on core 0");
  }

  Serial.println("# Format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS");
  Serial.println("# Listening...\n");
}

void loop() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    processSensor(sensors[i], onLocalCarDetected);
  }

  if (clientConnected && eventQueueCount > 0) {
    for (int i = 0; i < eventQueueCount; i++) {
      CarEvent& event = eventQueue[i];
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

  if (syncPending) {
    syncPending = false;
    char reply[32];
    snprintf(reply, sizeof(reply), "SYNC:%lu", millis());
    syncCharacteristic->setValue(reply);
    syncCharacteristic->notify();
  }

  delay(1);
}
