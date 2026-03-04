#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include "scalextric_protocol.h"

// Scalextric ESP-NOW → BLE Relay
// Receives car events from sensor nodes via ESP-NOW and notifies via BLE
// No WiFi network connection needed - just radio for ESP-NOW on fixed channel 1
// No local sensors, no OLED - pure relay
//
// BLE Service: one event characteristic (notify) + one sync characteristic (write+notify)
// Output format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS

#define SERVICE_UUID        "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
#define EVENT_CHAR_UUID     "a1b2c3d4-e5f6-7890-abcd-ef1234567891"
#define SYNC_CHAR_UUID      "a1b2c3d4-e5f6-7890-abcd-ef1234567892"

const uint8_t ESPNOW_CHANNEL = 1;

BLECharacteristic* eventCharacteristic = nullptr;
BLECharacteristic* syncCharacteristic = nullptr;
bool clientConnected = false;

// Event queue - decouple ESP-NOW callback from BLE notifications
const int EVENT_QUEUE_SIZE = 8;
CarEvent eventQueue[EVENT_QUEUE_SIZE];
uint32_t eventReceiveMs[EVENT_QUEUE_SIZE];  // millis() when ESP-NOW received
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
    // Units are 1.25ms, so 6 = 7.5ms, 12 = 15ms
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
    // Restart advertising so client can reconnect
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

  if (len != sizeof(CarEvent)) return;

  CarEvent event;
  memcpy(&event, data, sizeof(event));

  // Queue for BLE notification (don't do BLE in callback)
  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    eventQueue[eventQueueCount] = event;
    eventReceiveMs[eventQueueCount] = millis();
    eventQueueCount++;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n# Scalextric BLE Relay");
  Serial.println("# ====================");

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

  // Init BLE
  BLEDevice::init("Scalextric-Relay");
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

  Serial.println("# BLE: advertising as 'Scalextric-Relay'");
  Serial.printf("# Service: %s\n", SERVICE_UUID);

  // Keepalive timer: prevents Windows BLE CI drift during idle periods
  keepaliveTimer = timerBegin(1, 80, true);  // Timer 1 (timer 0 may be in use)
  timerAttachInterrupt(keepaliveTimer, &onKeepalive, true);
  timerAlarmWrite(keepaliveTimer, 1000000, true);  // 1 second
  timerAlarmEnable(keepaliveTimer);
  Serial.println("# KEEPALIVE: 1s interval");

  Serial.println("#");
  Serial.println("# Format: SEQ:NODE:SENSOR:CAR:FREQ:RECV_MILLIS");
  Serial.println("# Waiting for sensor nodes...\n");
}

void loop() {
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
