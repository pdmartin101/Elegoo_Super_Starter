#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Scalextric Car Detector - ESP-NOW Child Node
// Detects cars and broadcasts events via ESP-NOW (zero-config)
// Displays last detection on 128x64 OLED (I2C: SDA=21, SCL=22)
//
// Output format: NODE:SENSOR:CAR:FREQ:TIME
// e.g., 0:2:3:3704:12345 = Node 0, Sensor 2, Car 3, 3704 Hz, timestamp
//
// Only config needed: set NODE_ID (0, 1, 2, etc.) for each child

// ========== CONFIGURATION ==========
const uint8_t NODE_ID = 0;  // Change this for each child (0, 1, 2, etc.)

// Broadcast address - no parent MAC needed
const uint8_t BROADCAST[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Sensor GPIO pins (INPUT_PULLUP prevents false triggers on unconnected pins)
const int SENSOR_PINS[] = {4, 5, 16, 17};
const int NUM_SENSORS = 4;

// ========== CAR DETECTION ==========
const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const float FREQUENCY_TOLERANCE_PCT = 0.08;  // 8% of target frequency
const unsigned long MIN_VALID_INTERVAL = 150;
const unsigned long MAX_VALID_INTERVAL = 450;
const int MIN_PULSES_FOR_ID = 6;
const unsigned long DETECTION_TIMEOUT = 50000;
const int CONFIRM_COUNT = 3;
const int HISTORY_SIZE = 10;

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool hasDisplay = false;

// Display state
volatile bool displayNeedsUpdate = false;
uint8_t lastDetectedSensor = 0;
uint8_t lastDetectedCar = 0;
uint16_t lastDetectedFreq = 0;
int sendOkCount = 0;
int sendFailCount = 0;

// Recent events log
const int LOG_SIZE = 3;
struct LogEntry {
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
};
LogEntry eventLog[LOG_SIZE];
int logCount = 0;

// ESP-NOW message structure (compact)
struct CarEvent {
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
  uint32_t timestamp;
};

// Channel discovery probe (must match parent)
struct ProbeMsg {
  uint8_t magic;    // 0xAA = request, 0xBB = response
  uint8_t nodeId;
  uint8_t channel;  // Parent includes WiFi channel in response
};
volatile bool probeResponseReceived = false;
uint8_t foundChannel = 0;

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

esp_now_peer_info_t peerInfo;

// ISR shared logic + thin wrappers (attachInterrupt needs separate function pointers)
void IRAM_ATTR onPulse(int i) {
  unsigned long now = micros();
  SensorState& s = sensors[i];
  unsigned long delta = now - s.lastPulseTime;

  if (delta < 40) return;  // ignore bounce/glitch

  if (s.lastPulseTime > 0) {
    s.pulseInterval = delta;
    if (delta >= MIN_VALID_INTERVAL && delta <= MAX_VALID_INTERVAL) {
      s.intervalHistory[s.historyIndex] = delta;
      s.historyIndex = (s.historyIndex + 1) % HISTORY_SIZE;
      s.pulseCount++;
      s.newPulseData = true;
    }
  }
  s.lastPulseTime = now;
}

void IRAM_ATTR onPulse0() { onPulse(0); }
void IRAM_ATTR onPulse1() { onPulse(1); }
void IRAM_ATTR onPulse2() { onPulse(2); }
void IRAM_ATTR onPulse3() { onPulse(3); }

void (*isrFunctions[])() = {onPulse0, onPulse1, onPulse2, onPulse3};

int identifyCar(float frequency) {
  int bestCar = 0;
  float bestDiff = frequency;
  for (int car = 0; car < 6; car++) {
    float diff = abs(frequency - CAR_FREQUENCIES[car]);
    float maxDiff = CAR_FREQUENCIES[car] * FREQUENCY_TOLERANCE_PCT;
    if (diff < maxDiff && diff < bestDiff) {
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

void sendCarEvent(uint8_t sensorId, int car, float freq) {
  CarEvent event;
  event.nodeId = NODE_ID;
  event.sensorId = sensorId;
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  esp_err_t result = esp_now_send(BROADCAST, (uint8_t*)&event, sizeof(event));
  if (result == ESP_OK) {
    sendOkCount++;
    Serial.printf("SENT: %d:%d:%d:%d:%lu\n", NODE_ID, sensorId, car, (int)freq, event.timestamp);
  } else {
    sendFailCount++;
    Serial.printf("SEND FAILED (err %d): %d:%d\n", result, NODE_ID, sensorId);
  }

  // Update display state
  lastDetectedSensor = sensorId;
  lastDetectedCar = car;
  lastDetectedFreq = (uint16_t)freq;
  for (int i = LOG_SIZE - 1; i > 0; i--) {
    eventLog[i] = eventLog[i - 1];
  }
  eventLog[0] = {sensorId, (uint8_t)car, (uint16_t)freq};
  if (logCount < LOG_SIZE) logCount++;
  displayNeedsUpdate = true;
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

      if (sensor.confirmCount >= CONFIRM_COUNT && sensor.lastCarDetected == 0) {
        sendCarEvent(sensor.id, car, freq);
        sensor.lastCarDetected = car;
      }
    }
  }

  if (sensor.detecting && (micros() - sensor.lastActivityTime > DETECTION_TIMEOUT)) {
    resetSensor(sensor);
  }
}

void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  // Optional: handle send confirmation
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(ProbeMsg) && data[0] == 0xBB) {
    ProbeMsg response;
    memcpy(&response, data, sizeof(response));
    foundChannel = response.channel;
    probeResponseReceived = true;
  }
}

bool findParentChannel() {
  ProbeMsg probe;
  probe.magic = 0xAA;
  probe.nodeId = NODE_ID;
  probe.channel = 0;

  for (int round = 0; round < 3; round++) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

      if (hasDisplay) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.printf("Child Node %d", NODE_ID);
        display.setCursor(0, 16);
        display.printf("Scanning Ch: %d", ch);
        display.setCursor(0, 28);
        display.printf("Round %d/3", round + 1);
        display.display();
      }

      probeResponseReceived = false;
      esp_now_send(BROADCAST, (uint8_t*)&probe, sizeof(probe));

      unsigned long start = millis();
      while (millis() - start < 150) {
        if (probeResponseReceived) {
          // foundChannel was set from the parent's response in onDataReceived
          esp_wifi_set_channel(foundChannel, WIFI_SECOND_CHAN_NONE);
          Serial.printf("Parent found on channel %d\n", foundChannel);
          return true;
        }
        delay(1);
      }
    }
  }
  return false;
}

void updateDisplay() {
  display.clearDisplay();

  // Header: node + channel + TX stats (single line, matches parent layout)
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("N%d Ch:%d ok:%d f:%d", NODE_ID, foundChannel, sendOkCount, sendFailCount);

  // Big car number (size 3 = 18x24px)
  display.setTextSize(3);
  display.setCursor(0, 12);
  display.printf("Car %d", lastDetectedCar);

  // Frequency and sensor (size 1)
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.printf("%d Hz  Sensor %d", lastDetectedFreq, lastDetectedSensor);

  // Recent events log
  display.setCursor(0, 56);
  for (int i = 0; i < logCount && i < LOG_SIZE; i++) {
    display.printf("S%d=C%d ", eventLog[i].sensorId, eventLog[i].carNumber);
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nScalextric Child Node %d\n", NODE_ID);
  Serial.println("========================");

  // Init OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    hasDisplay = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.printf("Child Node %d", NODE_ID);
    display.setCursor(0, 16);
    display.println("Waiting for cars...");
    display.display();
    Serial.println("OLED: OK");
  } else {
    Serial.println("OLED: not found (continuing without display)");
  }

  // Init WiFi in station mode for ESP-NOW (no WiFi connection needed)
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  // Register broadcast peer
  memcpy(peerInfo.peer_addr, BROADCAST, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
    return;
  }

  // Find parent by probing all channels
  Serial.println("Scanning for parent...");
  if (findParentChannel()) {
    Serial.printf("Locked to channel %d\n", foundChannel);
    if (hasDisplay) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.printf("Child Node %d", NODE_ID);
      display.setCursor(0, 16);
      display.printf("Parent Ch: %d", foundChannel);
      display.setCursor(0, 28);
      display.println("Waiting for cars...");
      display.display();
    }
  } else {
    Serial.println("Parent not found! Staying on channel 1");
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (hasDisplay) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.printf("Child Node %d", NODE_ID);
      display.setCursor(0, 16);
      display.println("Parent NOT FOUND");
      display.setCursor(0, 28);
      display.println("Using Ch 1");
      display.display();
    }
  }

  // Setup sensors
  Serial.println("\nSensors:");
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
    Serial.printf("  %d:%d - GPIO %d\n", NODE_ID, i, SENSOR_PINS[i]);
  }

  Serial.println("\nFormat: NODE:SENSOR:CAR:FREQ:TIME");
  Serial.println("Waiting for cars...\n");
}

void loop() {
  for (int i = 0; i < NUM_SENSORS; i++) {
    processSensor(sensors[i]);
  }
  if (hasDisplay && displayNeedsUpdate) {
    displayNeedsUpdate = false;
    updateDisplay();
  }
  delay(1);
}
