#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebSocketsServer.h>
#include "wifi_credentials.h"

// WebSocket Latency Test
// Sends a fake car event every second with millis() timestamp
// Use with ScalextricClient to measure pure WiFi/WebSocket latency
// No sensors, no OLED, no ESP-NOW - just WiFi + WebSocket

const int WEBSOCKET_PORT = 81;
WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);

int eventCount = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("Client %d connected\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("Client %d disconnected\n", num);
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
  Serial.println("\nWebSocket Latency Test");
  Serial.println("======================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s", WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  esp_wifi_set_ps(WIFI_PS_NONE);
  Serial.printf("\nConnected: %s, Channel: %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());

  // Start WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.printf("WebSocket on port %d\n", WEBSOCKET_PORT);
  Serial.println("Sending fake event every 1s...\n");
}

void loop() {
  webSocket.loop();

  // Send a fake car event every second
  static unsigned long lastSend = 0;
  if (millis() - lastSend >= 1000) {
    lastSend = millis();

    // Rotate through cars 1-6 for variety
    int fakeCar = (eventCount % 6) + 1;
    int fakeFreq = (int[]){5500, 4400, 3700, 3100, 2800, 2400}[fakeCar - 1];
    eventCount++;

    char msg[48];
    snprintf(msg, sizeof(msg), "255:0:%d:%d:%lu", fakeCar, fakeFreq, millis());

    webSocket.broadcastTXT(msg);
    Serial.printf("Sent #%d: %s\n", eventCount, msg);
  }
}
