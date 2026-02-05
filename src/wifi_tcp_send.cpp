#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include "wifi_credentials.h"

const int TARGET_PORT = 12346;

WiFiClient client;
IPAddress targetIP;
bool receiverFound = false;

// Forward declarations
void discoverReceiver();
bool connectToServer();

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 TCP Sender with mDNS Discovery");

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
  if (!MDNS.begin("esp32sender")) {
    Serial.println("Error starting mDNS");
  }

  // Discover the receiver service
  Serial.println("Searching for TCP receiver service...");
  discoverReceiver();
}

void discoverReceiver() {
  int n = MDNS.queryService("tcpreceiver", "tcp");

  if (n > 0) {
    targetIP = MDNS.IP(0);
    receiverFound = true;
    Serial.print("Found receiver at: ");
    Serial.print(targetIP);
    Serial.print(":");
    Serial.println(TARGET_PORT);
  } else {
    Serial.println("No receiver found. Will retry...");
    receiverFound = false;
  }
}

bool connectToServer() {
  if (!client.connected()) {
    Serial.print("Connecting to server...");
    if (client.connect(targetIP, TARGET_PORT)) {
      Serial.println(" connected!");
      return true;
    } else {
      Serial.println(" failed!");
      return false;
    }
  }
  return true;
}

void loop() {
  static int counter = 0;
  static unsigned long lastDiscovery = 0;

  // Retry discovery every 10 seconds if not found
  if (!receiverFound && millis() - lastDiscovery > 10000) {
    discoverReceiver();
    lastDiscovery = millis();
  }

  if (receiverFound) {
    if (connectToServer()) {
      // Create message
      String message = "Hello from ESP32! Count: " + String(counter++);

      // Send TCP message
      client.println(message);
      Serial.println("Sent: " + message);

      // Check for response
      if (client.available()) {
        String response = client.readStringUntil('\n');
        Serial.println("Response: " + response);
      }
    }
  }

  delay(2000);
}
