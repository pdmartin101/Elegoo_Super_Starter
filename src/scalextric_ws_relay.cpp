#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include "wifi_credentials.h"
#include "scalextric_protocol.h"

// Scalextric ESP-NOW → WebSocket Relay
// Receives car events from sensor nodes via ESP-NOW and serves via WebSocket
// No local sensors, no OLED - pure relay
//
// Output format: SEQ:NODE:SENSOR:CAR:FREQ:MILLIS
// Client maps millis to wall clock via SYNC handshake at connect

const int WEBSOCKET_PORT = 81;

WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);

// Track registered children
const int MAX_CHILDREN = 10;
uint8_t childMacs[MAX_CHILDREN][6];
int childCount = 0;

// Event queue - decouple ESP-NOW callback from WebSocket TCP writes
const int EVENT_QUEUE_SIZE = 8;
CarEvent eventQueue[EVENT_QUEUE_SIZE];
volatile int eventQueueCount = 0;

// WiFi monitoring
unsigned long lastWifiCheck = 0;
bool wifiWasConnected = false;

// Sequence number for drop detection
uint32_t seqNumber = 0;

void broadcastEvent(CarEvent& event) {
  char msg[64];
  snprintf(msg, sizeof(msg), "%lu:%d:%d:%d:%d:%lu",
           seqNumber++, event.nodeId, event.sensorId, event.carNumber,
           event.frequency, (unsigned long)millis());
  webSocket.broadcastTXT(msg);
}

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
    response.channel = WiFi.channel();
    esp_now_send(mac, (uint8_t*)&response, sizeof(response));
    return;
  }

  if (len != sizeof(CarEvent)) return;

  CarEvent event;
  memcpy(&event, data, sizeof(event));

  // Queue for WebSocket broadcast (don't do TCP in callback)
  if (eventQueueCount < EVENT_QUEUE_SIZE) {
    eventQueue[eventQueueCount] = event;
    eventQueueCount++;
  }

  // Track new children
  bool known = false;
  for (int i = 0; i < childCount; i++) {
    if (memcmp(childMacs[i], mac, 6) == 0) { known = true; break; }
  }
  if (!known && childCount < MAX_CHILDREN) {
    memcpy(childMacs[childCount], mac, 6);
    childCount++;
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      break;
    case WStype_DISCONNECTED:
      break;
    case WStype_TEXT:
      if (length >= 4 && memcmp(payload, "SYNC", 4) == 0) {
        char syncReply[32];
        snprintf(syncReply, sizeof(syncReply), "SYNC:%lu", millis());
        webSocket.sendTXT(num, syncReply);
      }
      break;
    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n# Scalextric WebSocket Relay");
  Serial.println("# =========================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("# Connecting to %s", WIFI_SSID);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_ps(WIFI_PS_NONE);
    Serial.printf("\n# WiFi: %s, Channel: %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());

    if (MDNS.begin("scalextric-relay")) {
      MDNS.addService("ws", "tcp", WEBSOCKET_PORT);
      Serial.println("# mDNS: scalextric-relay.local");
    }

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.printf("# WebSocket server on port %d\n", WEBSOCKET_PORT);
  } else {
    Serial.println("\n# WiFi: FAILED");
  }

  wifiWasConnected = (WiFi.status() == WL_CONNECTED);

  Serial.printf("# MAC: %s\n", WiFi.macAddress().c_str());

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
  // Flush queued events first - minimise time between detection and TCP send
  for (int i = 0; i < eventQueueCount; i++) {
    broadcastEvent(eventQueue[i]);
  }
  eventQueueCount = 0;

  // Then process incoming WebSocket data
  webSocket.loop();

  // Periodic WiFi status check
  if (millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    wifiWasConnected = (WiFi.status() == WL_CONNECTED);
  }

  yield();
}
