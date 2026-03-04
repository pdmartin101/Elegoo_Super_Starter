#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "scalextric_protocol.h"

// Scalextric ESP-NOW Receiver (ESP32-A of split relay)
// Receives car events from sensor nodes via ESP-NOW and forwards to ESP32-B via Serial2
// ESP32-B runs the BLE bridge (scalextric_ble_bridge.cpp)
//
// Wiring: GPIO25 (TX2) → ESP32-B GPIO26 (RX2), plus shared GND
// Inter-board format: NODE:SENSOR:CAR:FREQ\n (no millis — ESP32-B adds its own)
//
// USB Serial (Serial) remains available for debug monitoring

const uint8_t ESPNOW_CHANNEL = 1;

// Event queue - decouple ESP-NOW callback from Serial2 writes
const int EVENT_QUEUE_SIZE = 8;
CarEvent eventQueue[EVENT_QUEUE_SIZE];
volatile int eventQueueCount = 0;

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  // Handle channel discovery probe from sensor nodes
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

  // Queue car events for Serial2 output in loop()
  if (len != sizeof(CarEvent)) return;

  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    memcpy(&eventQueue[eventQueueCount], data, sizeof(CarEvent));
    eventQueueCount++;
  }
}

void setup() {
  Serial.setTxBufferSize(512);
  Serial.begin(115200);
  Serial.println("\n# Scalextric ESP-NOW Receiver (split relay - ESP32-A)");
  Serial.println("# ==================================================");

  // Serial2 for inter-board communication to BLE bridge
  Serial2.begin(115200, SERIAL_8N1, /*RX=*/26, /*TX=*/25);
  Serial.println("# Serial2: TX=GPIO25 → BLE bridge RX=GPIO26");

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

  Serial.println("#");
  Serial.println("# Inter-board format: NODE:SENSOR:CAR:FREQ");
  Serial.println("# Waiting for sensor nodes...\n");
}

void loop() {
  // Flush queued events to Serial2 (→ BLE bridge)
  for (int i = 0; i < eventQueueCount; i++) {
    CarEvent& event = eventQueue[i];
    char msg[32];
    snprintf(msg, sizeof(msg), "%d:%d:%d:%d",
             event.nodeId, event.sensorId, event.carNumber, event.frequency);
    Serial2.println(msg);
    Serial.println(msg);  // Debug echo on USB
  }
  eventQueueCount = 0;
}
