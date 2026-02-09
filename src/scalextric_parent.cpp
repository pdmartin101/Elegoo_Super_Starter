#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Scalextric Car Detector - ESP-NOW Parent Node
// Detects cars locally AND receives events from child nodes via ESP-NOW
// Outputs to Serial in format: NODE:SENSOR:CAR:FREQ:TIME
// Displays last detection on 128x64 OLED (I2C: SDA=21, SCL=22)
//
// e.g., P:2:3:3704:12345 = Parent, Sensor 2, Car 3, 3704 Hz, timestamp
//       0:2:3:3704:12345 = Child Node 0, Sensor 2, Car 3, 3704 Hz, timestamp

// ========== CONFIGURATION ==========
const uint8_t PARENT_NODE_ID = 255;  // Parent uses 255 to distinguish from children

// Sensor GPIO pins (same as child)
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

// ESP-NOW message structure (must match child)
struct CarEvent {
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
  uint32_t timestamp;
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

void logEvent(CarEvent& event) {
  lastEvent = event;
  for (int i = LOG_SIZE - 1; i > 0; i--) {
    eventLog[i] = eventLog[i - 1];
  }
  eventLog[0] = event;
  if (logCount < LOG_SIZE) logCount++;
  displayNeedsUpdate = true;
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
  if (len != sizeof(CarEvent)) {
    Serial.printf("Invalid packet size: %d\n", len);
    return;
  }

  CarEvent event;
  memcpy(&event, data, sizeof(event));

  // Output format: NODE:SENSOR:CAR:FREQ:TIME
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

void updateDisplay() {
  display.clearDisplay();

  // Header (size 1)
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (lastEvent.nodeId == PARENT_NODE_ID) {
    display.print("Parent");
  } else {
    display.printf("Node %d", lastEvent.nodeId);
  }

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
    display.println("Waiting for cars...");
    display.display();
    Serial.println("# OLED: OK");
  } else {
    Serial.println("# OLED: not found (continuing without display)");
  }

  // Init WiFi in station mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("# Parent MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("#");

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("# ESP-NOW init failed!");
    return;
  }

  // Register receive callback
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

    pinMode(SENSOR_PINS[i], INPUT);
    attachInterrupt(digitalPinToInterrupt(SENSOR_PINS[i]), isrFunctions[i], FALLING);
    Serial.printf("#   P:%d - GPIO %d\n", i, SENSOR_PINS[i]);
  }

  Serial.println("#");
  Serial.println("# Format: NODE:SENSOR:CAR:FREQ:TIME");
  Serial.println("# Parent node = 255, Children = 0,1,2...");
  Serial.println("# Listening for cars...\n");
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
