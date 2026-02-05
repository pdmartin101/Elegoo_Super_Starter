#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include "wifi_credentials.h"

WebSocketsServer webSocket(81);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected\n", num);
      break;

    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %s\n", num, ip.toString().c_str());
      }
      break;

    case WStype_TEXT:
      Serial.printf("[%u] Received: %s\n", num, payload);
      // Echo back with ACK
      webSocket.sendTXT(num, "ACK: " + String((char*)payload));
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 WebSocket Server");

  // Connect to WiFi
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.print("ESP32 IP: ");
  Serial.println(WiFi.localIP());

  // Start mDNS
  if (MDNS.begin("esp32ws")) {
    MDNS.addService("ws", "tcp", 81);
    Serial.println("mDNS: esp32ws.local");
  }

  // Start WebSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("WebSocket server started on port 81");
  Serial.printf("Connect to: ws://%s:81\n", WiFi.localIP().toString().c_str());
}

void loop() {
  static int counter = 0;
  static unsigned long lastSend = 0;

  webSocket.loop();

  // Broadcast message every 2 seconds
  if (millis() - lastSend > 2000) {
    String message = "Hello from ESP32! Count: " + String(counter++);

    webSocket.broadcastTXT(message);
    Serial.println("Broadcast: " + message);

    lastSend = millis();
  }
}
