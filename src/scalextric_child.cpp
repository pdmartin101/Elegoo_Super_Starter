#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Scalextric Car Detector - ESP-NOW Child Node
// Detects cars and sends events to parent via ESP-NOW
//
// Output format: NODE:SENSOR:CAR:FREQ:TIME
// e.g., 0:2:3:3704:12345 = Node 0, Sensor 2, Car 3, 3704 Hz, timestamp
//
// Sensor naming is done in the PC app, not here

// ========== CONFIGURATION ==========
const uint8_t NODE_ID = 0;  // Change this for each child (0, 1, 2, etc.)

// Parent ESP32 MAC address (get from parent serial output)
uint8_t parentMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Broadcast for testing

// Sensor GPIO pins
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

// ESP-NOW message structure (compact)
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

esp_now_peer_info_t peerInfo;

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

void sendCarEvent(uint8_t sensorId, int car, float freq) {
  CarEvent event;
  event.nodeId = NODE_ID;
  event.sensorId = sensorId;
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  esp_err_t result = esp_now_send(parentMac, (uint8_t*)&event, sizeof(event));
  if (result == ESP_OK) {
    Serial.printf("SENT: %d:%d:%d:%d:%lu\n", NODE_ID, sensorId, car, (int)freq, event.timestamp);
  } else {
    Serial.printf("SEND FAILED: %d:%d\n", NODE_ID, sensorId);
  }
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

void setup() {
  Serial.begin(115200);
  Serial.printf("\nScalextric Child Node %d\n", NODE_ID);
  Serial.println("========================");

  // Init WiFi in station mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  // Register parent peer
  memcpy(peerInfo.peer_addr, parentMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add parent peer");
    return;
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

    pinMode(SENSOR_PINS[i], INPUT);
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
  delay(1);
}
