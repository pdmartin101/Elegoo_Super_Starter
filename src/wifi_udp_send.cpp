#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>

// ============ CONFIGURE THESE ============
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const int TARGET_PORT = 12345;
// =========================================

WiFiUDP udp;
IPAddress targetIP;
bool receiverFound = false;

// Forward declaration
void discoverReceiver();

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 UDP Sender with mDNS Discovery");

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
  Serial.println("Searching for UDP receiver service...");
  discoverReceiver();
}

void discoverReceiver() {
  int n = MDNS.queryService("udpreceiver", "udp");

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

void loop() {
  static int counter = 0;
  static unsigned long lastDiscovery = 0;

  // Retry discovery every 10 seconds if not found
  if (!receiverFound && millis() - lastDiscovery > 10000) {
    discoverReceiver();
    lastDiscovery = millis();
  }

  if (receiverFound) {
    // Create message
    String message = "Hello from ESP32! Count: " + String(counter++);

    // Send UDP packet
    udp.beginPacket(targetIP, TARGET_PORT);
    udp.print(message);
    udp.endPacket();

    Serial.println("Sent: " + message);
  }

  delay(2000);
}
