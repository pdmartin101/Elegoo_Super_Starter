#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <sys/time.h>
#include "wifi_credentials.h"
#include "scalextric_protocol.h"
#include "car_detection.h"

// Scalextric Car Detector - ESP-NOW Parent Node
// Detects cars locally AND receives events from child nodes via ESP-NOW
// Serves car events via WebSocket on port 81
// Discoverable via mDNS at scalextric.local
//
// Output format: NODE:SENSOR:CAR:FREQ:TIME
// e.g., 255:2:3:3704:14.23.05.123 = Parent, Sensor 2, Car 3, 3704 Hz, 14:23:05.123
//       0:2:3:3704:14.23.05.123 = Child 0, Sensor 2, Car 3, 3704 Hz, 14:23:05.123

// ========== CONFIGURATION ==========
#define WIFI_ENABLED 1  // Set to 0 to disable WiFi for testing
const int WEBSOCKET_PORT = 81;

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool hasDisplay = false;

// WebSocket server
WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);

// Track registered children
const int MAX_CHILDREN = 10;
uint8_t childMacs[MAX_CHILDREN][6];
int childCount = 0;

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

// WiFi monitoring
unsigned long lastWifiCheck = 0;
bool wifiWasConnected = false;

// Format current NTP time as HH.MM.SS.mmm, returns false if NTP not synced
bool formatNtpTime(char* buf, size_t len) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 0) || timeinfo.tm_year < (2020 - 1900)) {
    return false;
  }
  struct timeval tv;
  gettimeofday(&tv, NULL);
  int ms = tv.tv_usec / 1000;
  snprintf(buf, len, "%02d.%02d.%02d.%03d",
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, ms);
  return true;
}

void broadcastEvent(CarEvent& event) {
  // Format event as text and send to all WebSocket clients
  char msg[64];
  char timeBuf[16];
  if (formatNtpTime(timeBuf, sizeof(timeBuf))) {
    snprintf(msg, sizeof(msg), "%d:%d:%d:%d:%s",
             event.nodeId, event.sensorId, event.carNumber,
             event.frequency, timeBuf);
  } else {
    snprintf(msg, sizeof(msg), "%d:%d:%d:%d:%lu",
             event.nodeId, event.sensorId, event.carNumber,
             event.frequency, event.timestamp);
  }
  webSocket.broadcastTXT(msg);
}

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

  broadcastEvent(event);
}

void onLocalCarDetected(uint8_t sensorId, int car, float freq) {
  CarEvent event;
  event.nodeId = PARENT_NODE_ID;
  event.sensorId = sensorId;
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  char timeBuf[16];
  if (formatNtpTime(timeBuf, sizeof(timeBuf))) {
    Serial.printf("%d:%d:%d:%d:%s\n",
                  event.nodeId, event.sensorId, event.carNumber,
                  event.frequency, timeBuf);
  } else {
    Serial.printf("%d:%d:%d:%d:%lu\n",
                  event.nodeId, event.sensorId, event.carNumber,
                  event.frequency, event.timestamp);
  }

  logEvent(event);
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  // Handle channel discovery probe
  if (len == sizeof(ProbeMsg) && data[0] == PROBE_REQUEST_MAGIC) {
    Serial.printf("# Probe from Node %d, responding\n", data[1]);
    // Add child as peer so we can respond
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

  espNowRecvCount++;
  if (len != sizeof(CarEvent)) {
    Serial.printf("# ESP-NOW recv: unexpected %d bytes (expected %d)\n", len, sizeof(CarEvent));
    return;
  }

  CarEvent event;
  memcpy(&event, data, sizeof(event));

  char timeBuf[16];
  if (formatNtpTime(timeBuf, sizeof(timeBuf))) {
    Serial.printf("%d:%d:%d:%d:%s\n",
                  event.nodeId, event.sensorId, event.carNumber,
                  event.frequency, timeBuf);
  } else {
    Serial.printf("%d:%d:%d:%d:%lu\n",
                  event.nodeId, event.sensorId, event.carNumber,
                  event.frequency, event.timestamp);
  }

  logEvent(event);

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
    Serial.printf("# New child: %02X:%02X:%02X:%02X:%02X:%02X (Node %d)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  event.nodeId);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("# WebSocket client %d connected\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("# WebSocket client %d disconnected\n", num);
      break;
    case WStype_TEXT:
      // Could handle commands from C# app here in future
      break;
    default:
      break;
  }
}

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
  display.printf("  Ch:%d RX:%d", WiFi.channel(), espNowRecvCount);

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

void setup() {
  Serial.begin(115200);
  Serial.println("\n# Scalextric Parent Node");
  Serial.println("# ======================");

  // Init OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    hasDisplay = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Scalextric Parent");
    display.println(WIFI_ENABLED ? "Connecting WiFi..." : "WiFi disabled");
    display.display();
    Serial.println("# OLED: OK");
  } else {
    Serial.println("# OLED: not found (continuing without display)");
  }

  // Connect to WiFi (needed for WebSocket server)
  WiFi.mode(WIFI_STA);

#if WIFI_ENABLED
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("# Connecting to %s", WIFI_SSID);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power saving - improves ESP-NOW reliability
    Serial.printf("\n# WiFi connected: %s, Channel: %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());

    // Sync time via NTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print("# NTP sync...");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
      Serial.printf("OK (%02d:%02d:%02d UTC)\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
      Serial.println("failed (using millis)");
    }

    // Start mDNS
    if (MDNS.begin("scalextric")) {
      MDNS.addService("ws", "tcp", WEBSOCKET_PORT);
      Serial.println("# mDNS: scalextric.local");
    } else {
      Serial.println("# mDNS: failed to start");
    }

    // Start WebSocket server
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    Serial.printf("# WebSocket server on port %d\n", WEBSOCKET_PORT);

    if (hasDisplay) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Scalextric Parent");
      display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      display.printf("Ch: %d  Port: %d\n", WiFi.channel(), WEBSOCKET_PORT);
      display.println("Waiting for cars...");
      display.display();
    }
  } else {
    Serial.println("\n# WiFi: FAILED (WebSocket disabled)");
    if (hasDisplay) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Scalextric Parent");
      display.println("WiFi FAILED");
      display.println("Local sensors only");
      display.display();
    }
  }
#else
  Serial.println("# WiFi: disabled for testing");
#endif

  wifiWasConnected = (WiFi.status() == WL_CONNECTED);

  Serial.print("# Parent MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("#");

  // Init ESP-NOW (works alongside WiFi STA)
  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onDataReceived);
  } else {
    Serial.println("# ESP-NOW init failed! Local sensors only.");
  }

  // Setup local sensors
  Serial.println("# Local sensors:");
  initSensors();
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.printf("#   P:%d - GPIO %d\n", i, SENSOR_PINS[i]);
  }

  Serial.println("#");
  Serial.println("# Format: NODE:SENSOR:CAR:FREQ:TIME");
  Serial.println("# Parent node = 255, Children = 0,1,2...");
  Serial.println("# Listening for cars...\n");
}

void loop() {
#if WIFI_ENABLED
  webSocket.loop();

  // Periodic WiFi status check
  if (millis() - lastWifiCheck > 10000) {
    lastWifiCheck = millis();
    bool connected = WiFi.status() == WL_CONNECTED;
    if (wifiWasConnected && !connected) {
      Serial.println("# WiFi lost, waiting for reconnect...");
    } else if (!wifiWasConnected && connected) {
      Serial.printf("# WiFi restored: %s\n", WiFi.localIP().toString().c_str());
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    }
    wifiWasConnected = connected;
  }
#endif

  for (int i = 0; i < NUM_SENSORS; i++) {
    processSensor(sensors[i], onLocalCarDetected);
  }
  if (hasDisplay && displayNeedsUpdate) {
    displayNeedsUpdate = false;
    updateDisplay();
  }
  delay(1);
}
