#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "scalextric_protocol.h"

// Scalextric ESP-NOW USB Dongle
// Receives car events from sensor nodes via ESP-NOW and forwards to PC via Serial
// Plug into PC USB port, run ScalextricSerialClient to read events
//
// Output format: SEQ:NODE:SENSOR:CAR:FREQ:MILLIS
// Sensor nodes auto-discover this dongle via channel probe (same as parent)

const uint8_t ESPNOW_CHANNEL = 1;

uint32_t seqNumber = 0;

// Event queue - decouple ESP-NOW callback from Serial writes
// Serial.println in the callback blocks the WiFi task and causes packet loss
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

  // Queue car events for Serial output in loop()
  if (len != sizeof(CarEvent)) return;

  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    memcpy(&eventQueue[eventQueueCount], data, sizeof(CarEvent));
    eventQueueCount++;
  }
}

void setup() {
  Serial.setTxBufferSize(512);
  Serial.begin(115200);
  Serial.println("\n# Scalextric ESP-NOW Dongle");
  Serial.println("# ========================");

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.printf("# MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("# Channel: %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataReceived);
    Serial.println("# ESP-NOW: OK");
  } else {
    Serial.println("# ESP-NOW: FAILED");
  }

  Serial.println("#");
  Serial.println("# Format: SEQ:NODE:SENSOR:CAR:FREQ:MILLIS");
  Serial.println("# Waiting for sensor nodes...\n");
}

void loop() {
  // Flush queued events to Serial
  for (int i = 0; i < eventQueueCount; i++) {
    CarEvent& event = eventQueue[i];
    char msg[64];
    snprintf(msg, sizeof(msg), "%lu:%d:%d:%d:%d:%lu",
             seqNumber++, event.nodeId, event.sensorId, event.carNumber,
             event.frequency, (unsigned long)millis());
    Serial.println(msg);
  }
  eventQueueCount = 0;

  // Handle SYNC requests from PC for clock calibration
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("SYNC")) {
      char reply[32];
      snprintf(reply, sizeof(reply), "SYNC:%lu", millis());
      Serial.println(reply);
    }
  }
}
