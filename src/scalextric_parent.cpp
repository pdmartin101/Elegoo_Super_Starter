#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include "wifi_credentials.h"

// Scalextric Car Detector - ESP-NOW Parent Node
// Detects cars locally AND receives events from child nodes via ESP-NOW
// Serves car events via WebSocket on port 81
// Discoverable via mDNS at scalextric.local
//
// Output format: NODE:SENSOR:CAR:FREQ:TIME
// e.g., 255:2:3:3704:12345 = Parent, Sensor 2, Car 3, 3704 Hz, timestamp
//       0:2:3:3704:12345 = Child Node 0, Sensor 2, Car 3, 3704 Hz, timestamp

// ========== CONFIGURATION ==========
const uint8_t PARENT_NODE_ID = 255;  // Parent uses 255 to distinguish from children
const int WEBSOCKET_PORT = 81;

// Sensor GPIO pins (INPUT_PULLUP prevents false triggers on unconnected pins)
const int SENSOR_PINS[] = {4, 5, 16, 17};
const int NUM_SENSORS = 4;

// ========== CAR DETECTION ==========
const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const int FREQUENCY_TOLERANCE = 300;
const unsigned long MIN_VALID_INTERVAL = 150;
const unsigned long MAX_VALID_INTERVAL = 450;
const int MIN_PULSES_FOR_ID = 10;
const unsigned long DETECTION_TIMEOUT = 50000;
const int CONFIRM_COUNT = 3;
const int HISTORY_SIZE = 10;

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool hasDisplay = false;

// WebSocket server
WebSocketsServer webSocket = WebSocketsServer(WEBSOCKET_PORT);

// ESP-NOW message structure (must match child)
struct CarEvent {
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
  uint32_t timestamp;
};

// Channel discovery probe (must match child)
struct ProbeMsg {
  uint8_t magic;    // 0xAA = request, 0xBB = response
  uint8_t nodeId;
};

// Sensor state structure
struct SensorState {
  uint8_t id;
  int pin;
  volatile unsigned long lastPulseTime;
  volatile unsigned long pulseInterval;
  volatile int pulseCount;
  volatile bool newPulseData;
  volatile unsigned long intervalHistory[HISTORY_SIZE];
  volatile int historyIndex;
  unsigned long lastActivityTime;
  bool detecting;
  int lastCarDetected;
  int candidateCar;
  int confirmCount;
};

// Four sensor instances
SensorState sensors[NUM_SENSORS];

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

// ISRs for each sensor (must be separate functions)
void IRAM_ATTR onPulse0() {
  unsigned long now = micros();
  SensorState& s = sensors[0];
  if (s.lastPulseTime > 0) {
    s.pulseInterval = now - s.lastPulseTime;
    if (s.pulseInterval >= MIN_VALID_INTERVAL && s.pulseInterval <= MAX_VALID_INTERVAL) {
      s.intervalHistory[s.historyIndex] = s.pulseInterval;
      s.historyIndex = (s.historyIndex + 1) % HISTORY_SIZE;
      s.pulseCount++;
      s.newPulseData = true;
    }
  }
  s.lastPulseTime = now;
}

void IRAM_ATTR onPulse1() {
  unsigned long now = micros();
  SensorState& s = sensors[1];
  if (s.lastPulseTime > 0) {
    s.pulseInterval = now - s.lastPulseTime;
    if (s.pulseInterval >= MIN_VALID_INTERVAL && s.pulseInterval <= MAX_VALID_INTERVAL) {
      s.intervalHistory[s.historyIndex] = s.pulseInterval;
      s.historyIndex = (s.historyIndex + 1) % HISTORY_SIZE;
      s.pulseCount++;
      s.newPulseData = true;
    }
  }
  s.lastPulseTime = now;
}

void IRAM_ATTR onPulse2() {
  unsigned long now = micros();
  SensorState& s = sensors[2];
  if (s.lastPulseTime > 0) {
    s.pulseInterval = now - s.lastPulseTime;
    if (s.pulseInterval >= MIN_VALID_INTERVAL && s.pulseInterval <= MAX_VALID_INTERVAL) {
      s.intervalHistory[s.historyIndex] = s.pulseInterval;
      s.historyIndex = (s.historyIndex + 1) % HISTORY_SIZE;
      s.pulseCount++;
      s.newPulseData = true;
    }
  }
  s.lastPulseTime = now;
}

void IRAM_ATTR onPulse3() {
  unsigned long now = micros();
  SensorState& s = sensors[3];
  if (s.lastPulseTime > 0) {
    s.pulseInterval = now - s.lastPulseTime;
    if (s.pulseInterval >= MIN_VALID_INTERVAL && s.pulseInterval <= MAX_VALID_INTERVAL) {
      s.intervalHistory[s.historyIndex] = s.pulseInterval;
      s.historyIndex = (s.historyIndex + 1) % HISTORY_SIZE;
      s.pulseCount++;
      s.newPulseData = true;
    }
  }
  s.lastPulseTime = now;
}

void (*isrFunctions[])() = {onPulse0, onPulse1, onPulse2, onPulse3};

int identifyCar(float frequency) {
  int bestCar = 0;
  float bestDiff = FREQUENCY_TOLERANCE;
  for (int car = 0; car < 6; car++) {
    float diff = abs(frequency - CAR_FREQUENCIES[car]);
    if (diff < bestDiff) {
      bestDiff = diff;
      bestCar = car + 1;
    }
  }
  return bestCar;
}

float calculateMedianFrequency(volatile unsigned long* history) {
  unsigned long temp[HISTORY_SIZE];
  int validSamples = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (history[i] > 0) {
      temp[validSamples++] = history[i];
    }
  }
  if (validSamples < 3) return 0;
  for (int i = 0; i < validSamples - 1; i++) {
    for (int j = 0; j < validSamples - i - 1; j++) {
      if (temp[j] > temp[j + 1]) {
        unsigned long swap = temp[j];
        temp[j] = temp[j + 1];
        temp[j + 1] = swap;
      }
    }
  }
  return 1000000.0 / temp[validSamples / 2];
}

void resetSensor(SensorState& sensor) {
  sensor.pulseCount = 0;
  sensor.historyIndex = 0;
  sensor.lastPulseTime = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    sensor.intervalHistory[i] = 0;
  }
  sensor.detecting = false;
  sensor.lastCarDetected = 0;
  sensor.candidateCar = 0;
  sensor.confirmCount = 0;
}

void broadcastEvent(CarEvent& event) {
  // Format event as text and send to all WebSocket clients
  char msg[64];
  snprintf(msg, sizeof(msg), "%d:%d:%d:%d:%lu",
           event.nodeId, event.sensorId, event.carNumber,
           event.frequency, event.timestamp);
  webSocket.broadcastTXT(msg);
}

void logEvent(CarEvent& event) {
  lastEvent = event;
  for (int i = LOG_SIZE - 1; i > 0; i--) {
    eventLog[i] = eventLog[i - 1];
  }
  eventLog[0] = event;
  if (logCount < LOG_SIZE) logCount++;
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

  Serial.printf("%d:%d:%d:%d:%lu\n",
                event.nodeId, event.sensorId, event.carNumber,
                event.frequency, event.timestamp);

  logEvent(event);
}

void processSensor(SensorState& sensor) {
  if (sensor.newPulseData) {
    sensor.newPulseData = false;
    sensor.lastActivityTime = micros();
    if (!sensor.detecting) {
      sensor.detecting = true;
    }
  }

  if (sensor.detecting && sensor.pulseCount >= MIN_PULSES_FOR_ID) {
    float freq = calculateMedianFrequency(sensor.intervalHistory);
    int car = identifyCar(freq);

    if (car > 0) {
      if (car == sensor.candidateCar) {
        sensor.confirmCount++;
      } else {
        sensor.candidateCar = car;
        sensor.confirmCount = 1;
      }

      if (sensor.confirmCount >= CONFIRM_COUNT && car != sensor.lastCarDetected) {
        onLocalCarDetected(sensor.id, car, freq);
        sensor.lastCarDetected = car;
      }
    }
  }

  if (sensor.detecting && (micros() - sensor.lastActivityTime > DETECTION_TIMEOUT)) {
    resetSensor(sensor);
  }
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  // Handle channel discovery probe
  if (len == sizeof(ProbeMsg) && data[0] == 0xAA) {
    Serial.printf("# Probe from Node %d, responding\n", data[1]);
    // Add child as peer so we can respond
    if (!esp_now_is_peer_exist(mac)) {
      esp_now_peer_info_t peer;
      memcpy(peer.peer_addr, mac, 6);
      peer.channel = 0;
      peer.encrypt = false;
      esp_now_add_peer(&peer);
    }
    ProbeMsg response;
    response.magic = 0xBB;
    response.nodeId = PARENT_NODE_ID;
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

  Serial.printf("%d:%d:%d:%d:%lu\n",
                event.nodeId,
                event.sensorId,
                event.carNumber,
                event.frequency,
                event.timestamp);

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

  // Recent events log
  display.setCursor(0, 52);
  for (int i = 0; i < logCount && i < LOG_SIZE; i++) {
    if (eventLog[i].nodeId == PARENT_NODE_ID) {
      display.printf("P:%d=C%d ", eventLog[i].sensorId, eventLog[i].carNumber);
    } else {
      display.printf("%d:%d=C%d ", eventLog[i].nodeId, eventLog[i].sensorId, eventLog[i].carNumber);
    }
  }

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
    display.println("Connecting WiFi...");
    display.display();
    Serial.println("# OLED: OK");
  } else {
    Serial.println("# OLED: not found (continuing without display)");
  }

  // Connect to WiFi (needed for WebSocket server)
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
    esp_wifi_set_ps(WIFI_PS_NONE);  // Disable power saving - improves ESP-NOW reliability
    Serial.printf("\n# WiFi connected: %s, Channel: %d\n", WiFi.localIP().toString().c_str(), WiFi.channel());

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

  Serial.print("# Parent MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("#");

  // Init ESP-NOW (works alongside WiFi STA)
  if (esp_now_init() != ESP_OK) {
    Serial.println("# ESP-NOW init failed!");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);

  // Setup local sensors
  Serial.println("# Local sensors:");
  for (int i = 0; i < NUM_SENSORS; i++) {
    sensors[i].id = i;
    sensors[i].pin = SENSOR_PINS[i];
    sensors[i].lastPulseTime = 0;
    sensors[i].pulseInterval = 0;
    sensors[i].pulseCount = 0;
    sensors[i].newPulseData = false;
    sensors[i].historyIndex = 0;
    sensors[i].lastActivityTime = 0;
    sensors[i].detecting = false;
    sensors[i].lastCarDetected = 0;
    sensors[i].candidateCar = 0;
    sensors[i].confirmCount = 0;
    for (int j = 0; j < HISTORY_SIZE; j++) {
      sensors[i].intervalHistory[j] = 0;
    }

    pinMode(SENSOR_PINS[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SENSOR_PINS[i]), isrFunctions[i], FALLING);
    Serial.printf("#   P:%d - GPIO %d\n", i, SENSOR_PINS[i]);
  }

  Serial.println("#");
  Serial.println("# Format: NODE:SENSOR:CAR:FREQ:TIME");
  Serial.println("# Parent node = 255, Children = 0,1,2...");
  Serial.println("# Listening for cars...\n");
}

void loop() {
  webSocket.loop();
  for (int i = 0; i < NUM_SENSORS; i++) {
    processSensor(sensors[i]);
  }
  if (hasDisplay && displayNeedsUpdate) {
    displayNeedsUpdate = false;
    updateDisplay();
  }
  delay(1);
}
