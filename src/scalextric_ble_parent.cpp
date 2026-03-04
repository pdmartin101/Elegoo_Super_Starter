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

// Scalextric BLE Parent Node
// Detects cars locally AND receives events from child nodes via ESP-NOW
// Serves car events via BLE notifications
//
// Same as scalextric_ws_parent.cpp but outputs via BLE instead of WebSocket
// Set ESPNOW_ENABLED=0 for BLE-only mode (~5ms latency, no child nodes)
// Set ESPNOW_ENABLED=1 for ESP-NOW + BLE (~55ms coexistence delay, supports children)
// Set TEST_TIMER=1 to generate fake events every 1s (for latency testing without sensors)
//
// Output format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS

#ifndef ESPNOW_ENABLED
#define ESPNOW_ENABLED 1  // Override via build_flags: -DESPNOW_ENABLED=0
#endif
#ifndef TEST_TIMER
#define TEST_TIMER     0  // Override via build_flags: -DTEST_TIMER=1
#endif

#if ESPNOW_ENABLED
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#endif

#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define EVENT_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define SYNC_CHAR_UUID      "a1b2c3d4-e5f6-7890-abcd-ef1234567892"

const uint8_t ESPNOW_CHANNEL = 1;

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool hasDisplay = false;

// BLE
BLECharacteristic* eventCharacteristic = nullptr;
BLECharacteristic* syncCharacteristic = nullptr;
bool clientConnected = false;

#if ESPNOW_ENABLED
// Track registered children
const int MAX_CHILDREN = 10;
uint8_t childMacs[MAX_CHILDREN][6];
int childCount = 0;
#endif

// Display state
volatile bool displayNeedsUpdate = false;
CarEvent lastEvent;
int espNowRecvCount = 0;

// Recent events log
const int LOG_SIZE = 3;
CarEvent eventLog[LOG_SIZE];
int logCount = 0;

// Per-car detection counts (index 0 = car 1, etc.)
const int NUM_CARS = 6;
int carCounts[NUM_CARS] = {0};

// Event queue - decouple detection from BLE sends
const int EVENT_QUEUE_SIZE = 8;
CarEvent eventQueue[EVENT_QUEUE_SIZE];
uint32_t eventReceiveMs[EVENT_QUEUE_SIZE];  // millis() when event was received/detected
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

#if TEST_TIMER
// Timer for generating fake events (same as latency test)
hw_timer_t* testTimer = nullptr;
int testEventCount = 0;

void IRAM_ATTR onTestTimer() {
  if (!clientConnected) return;
  if (eventQueueCount >= EVENT_QUEUE_SIZE) return;

  int idx = eventQueueCount;
  int car = (testEventCount % 6) + 1;
  eventQueue[idx].nodeId = PARENT_NODE_ID;
  eventQueue[idx].sensorId = 0;
  eventQueue[idx].carNumber = car;
  eventQueue[idx].frequency = (uint16_t[]){5500, 4400, 3700, 3100, 2800, 2400}[car - 1];
  eventQueue[idx].timestamp = millis();
  eventReceiveMs[idx] = millis();
  eventQueueCount++;
  testEventCount++;
}
#endif

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
  }
  void onDisconnect(BLEServer* server) override {
    clientConnected = false;
    server->getAdvertising()->start();
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

void logEvent(CarEvent& event) {
  lastEvent = event;
  for (int i = LOG_SIZE - 1; i > 0; i--) {
    eventLog[i] = eventLog[i - 1];
  }
  eventLog[0] = event;
  if (logCount < LOG_SIZE) logCount++;

  if (event.carNumber >= 1 && event.carNumber <= NUM_CARS) {
    carCounts[event.carNumber - 1]++;
  }

  displayNeedsUpdate = true;
}

void onLocalCarDetected(uint8_t sensorId, int car, float freq) {
  CarEvent event;
  event.nodeId = PARENT_NODE_ID;
  event.sensorId = sensorId;
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  logEvent(event);

  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    eventQueue[eventQueueCount] = event;
    eventReceiveMs[eventQueueCount] = millis();
    eventQueueCount++;
  }
}

#if ESPNOW_ENABLED
void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  // Handle channel discovery probe
  if (len == sizeof(ProbeMsg) && data[0] == PROBE_REQUEST_MAGIC) {
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_peer_info_t peer = {};
      memcpy(peer.peer_addr, mac, 6);
      peer.channel = 0;
      peer.encrypt = false;
      esp_now_add_peer(&peer);
    }
    ProbeMsg response;
    response.magic = PROBE_RESPONSE_MAGIC;
    response.nodeId = PARENT_NODE_ID;
    response.channel = ESPNOW_CHANNEL;
    esp_now_send(mac, (uint8_t*)&response, sizeof(response));
    return;
  }

  espNowRecvCount++;
  if (len != sizeof(CarEvent)) return;

  CarEvent event;
  memcpy(&event, data, sizeof(event));

  logEvent(event);

  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    eventQueue[eventQueueCount] = event;
    eventReceiveMs[eventQueueCount] = millis();
    eventQueueCount++;
  }

  // Check if this is a new child
  bool known = false;
  for (int i = 0; i < childCount; i++) {
    if (memcmp(childMacs[i], mac, 6) == 0) {
      known = true;
      break;
    }
  }

  if (!known && childCount < MAX_CHILDREN) {
    memcpy(childMacs[childCount], mac, 6);
    childCount++;
  }
}
#endif

void updateDisplay() {
  display.clearDisplay();

  // Header: source + channel + ESP-NOW stats (size 1)
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (lastEvent.nodeId == PARENT_NODE_ID) {
    display.print("Parent");
  } else {
    display.printf("Node %d", lastEvent.nodeId);
  }
#if ESPNOW_ENABLED
  display.printf("  Ch:%d RX:%d", WiFi.channel(), espNowRecvCount);
#else
  display.printf("  BLE  RX:%d", espNowRecvCount);
#endif

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
  Serial.setTxBufferSize(512);
  Serial.begin(115200);
  Serial.println("\n# Scalextric BLE Parent Node");
  Serial.println("# =========================");

  // Init OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    hasDisplay = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Scalextric BLE Parent");
    display.println("Starting...");
    display.display();
    Serial.println("# OLED: OK");
  } else {
    Serial.println("# OLED: not found (continuing without display)");
  }

#if ESPNOW_ENABLED
  // WiFi STA mode for ESP-NOW radio (no network connection)
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  Serial.printf("# MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("# ESP-NOW Channel: %d\n", ESPNOW_CHANNEL);

  // Init ESP-NOW
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataReceived);
    Serial.println("# ESP-NOW: OK");
  } else {
    Serial.println("# ESP-NOW: FAILED");
  }
#else
  Serial.println("# ESP-NOW: disabled (BLE-only mode, low latency)");
#endif

  // Setup local sensors
  Serial.println("# Local sensors:");
  initSensors();
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.printf("#   S%d - GPIO %d\n", i, SENSOR_PINS[i]);
  }

  // Init BLE
  BLEDevice::init("Scalextric-Parent");
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

  Serial.println("# BLE: advertising as 'Scalextric-Parent'");

  if (hasDisplay) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Scalextric BLE Parent");
#if ESPNOW_ENABLED
    display.printf("Ch: %d\n", ESPNOW_CHANNEL);
#else
    display.println("BLE-only mode");
#endif
    display.println("BLE: advertising");
    display.println("Waiting for cars...");
    display.display();

    xTaskCreatePinnedToCore(displayTask, "display", 4096, NULL, 1, NULL, 0);
    Serial.println("# OLED: running on core 0");
  }

#if TEST_TIMER
  // Timer interrupt every 1 second to generate fake events (latency testing)
  testTimer = timerBegin(0, 80, true);  // 80 prescaler = 1MHz (1us ticks)
  timerAttachInterrupt(testTimer, &onTestTimer, true);
  timerAlarmWrite(testTimer, 1000000, true);  // 1 second
  timerAlarmEnable(testTimer);
  Serial.println("# TEST TIMER: firing every 1s (fake events for latency testing)");
#endif

  // Keepalive timer: prevents Windows BLE CI drift during idle periods
  keepaliveTimer = timerBegin(1, 80, true);  // Timer 1 (timer 0 may be used by TEST_TIMER)
  timerAttachInterrupt(keepaliveTimer, &onKeepalive, true);
  timerAlarmWrite(keepaliveTimer, 1000000, true);  // 1 second
  timerAlarmEnable(keepaliveTimer);
  Serial.println("# KEEPALIVE: 1s interval");

  Serial.println("#");
  Serial.println("# Format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS");
  Serial.println("# Parent node = 255, Children = 0,1,2...");
  Serial.println("# Listening for cars...\n");
}

void loop() {
  // Process sensors FIRST - detection is time-critical
  for (int i = 0; i < NUM_SENSORS; i++) {
    processSensor(sensors[i], onLocalCarDetected);
  }

  // Flush queued events via BLE notification
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
