#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Scalextric Car Detector - ESP-NOW Child Node
// Detects cars and sends events to parent via ESP-NOW
//
// Each child has a unique NODE_ID (set before flashing)
// Parent MAC address must be configured below

// ========== CONFIGURATION ==========
const uint8_t NODE_ID = 1;  // Change this for each child (1, 2, 3, etc.)

// Parent ESP32 MAC address (get from parent serial output)
uint8_t parentMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Broadcast for testing

// Sensor GPIO pins (adjust per node)
const int SENSOR_A_PIN = 4;
const int SENSOR_B_PIN = 5;
const int SENSOR_C_PIN = 16;
const int SENSOR_D_PIN = 17;

// Sensor names (adjust per node location)
const char* SENSOR_A_NAME = "START1";
const char* SENSOR_B_NAME = "START2";
const char* SENSOR_C_NAME = "PIT_IN";
const char* SENSOR_D_NAME = "PIT_OUT";

// ========== CAR DETECTION ==========
const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const int FREQUENCY_TOLERANCE = 300;
const unsigned long MIN_VALID_INTERVAL = 150;
const unsigned long MAX_VALID_INTERVAL = 450;
const int MIN_PULSES_FOR_ID = 10;
const unsigned long DETECTION_TIMEOUT = 50000;
const int CONFIRM_COUNT = 3;
const int HISTORY_SIZE = 10;

// ESP-NOW message structure
struct CarEvent {
  uint8_t nodeId;
  char sensor[12];
  uint8_t carNumber;
  uint16_t frequency;
  uint32_t timestamp;
};

// Sensor state structure
struct SensorState {
  const char* name;
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
SensorState sensorA = {SENSOR_A_NAME, SENSOR_A_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};
SensorState sensorB = {SENSOR_B_NAME, SENSOR_B_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};
SensorState sensorC = {SENSOR_C_NAME, SENSOR_C_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};
SensorState sensorD = {SENSOR_D_NAME, SENSOR_D_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};

esp_now_peer_info_t peerInfo;

// ISRs for each sensor
void IRAM_ATTR onPulseA() {
  unsigned long now = micros();
  if (sensorA.lastPulseTime > 0) {
    sensorA.pulseInterval = now - sensorA.lastPulseTime;
    if (sensorA.pulseInterval >= MIN_VALID_INTERVAL && sensorA.pulseInterval <= MAX_VALID_INTERVAL) {
      sensorA.intervalHistory[sensorA.historyIndex] = sensorA.pulseInterval;
      sensorA.historyIndex = (sensorA.historyIndex + 1) % HISTORY_SIZE;
      sensorA.pulseCount++;
      sensorA.newPulseData = true;
    }
  }
  sensorA.lastPulseTime = now;
}

void IRAM_ATTR onPulseB() {
  unsigned long now = micros();
  if (sensorB.lastPulseTime > 0) {
    sensorB.pulseInterval = now - sensorB.lastPulseTime;
    if (sensorB.pulseInterval >= MIN_VALID_INTERVAL && sensorB.pulseInterval <= MAX_VALID_INTERVAL) {
      sensorB.intervalHistory[sensorB.historyIndex] = sensorB.pulseInterval;
      sensorB.historyIndex = (sensorB.historyIndex + 1) % HISTORY_SIZE;
      sensorB.pulseCount++;
      sensorB.newPulseData = true;
    }
  }
  sensorB.lastPulseTime = now;
}

void IRAM_ATTR onPulseC() {
  unsigned long now = micros();
  if (sensorC.lastPulseTime > 0) {
    sensorC.pulseInterval = now - sensorC.lastPulseTime;
    if (sensorC.pulseInterval >= MIN_VALID_INTERVAL && sensorC.pulseInterval <= MAX_VALID_INTERVAL) {
      sensorC.intervalHistory[sensorC.historyIndex] = sensorC.pulseInterval;
      sensorC.historyIndex = (sensorC.historyIndex + 1) % HISTORY_SIZE;
      sensorC.pulseCount++;
      sensorC.newPulseData = true;
    }
  }
  sensorC.lastPulseTime = now;
}

void IRAM_ATTR onPulseD() {
  unsigned long now = micros();
  if (sensorD.lastPulseTime > 0) {
    sensorD.pulseInterval = now - sensorD.lastPulseTime;
    if (sensorD.pulseInterval >= MIN_VALID_INTERVAL && sensorD.pulseInterval <= MAX_VALID_INTERVAL) {
      sensorD.intervalHistory[sensorD.historyIndex] = sensorD.pulseInterval;
      sensorD.historyIndex = (sensorD.historyIndex + 1) % HISTORY_SIZE;
      sensorD.pulseCount++;
      sensorD.newPulseData = true;
    }
  }
  sensorD.lastPulseTime = now;
}

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

void sendCarEvent(const char* sensorName, int car, float freq) {
  CarEvent event;
  event.nodeId = NODE_ID;
  strncpy(event.sensor, sensorName, sizeof(event.sensor) - 1);
  event.sensor[sizeof(event.sensor) - 1] = '\0';
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  esp_err_t result = esp_now_send(parentMac, (uint8_t*)&event, sizeof(event));
  if (result == ESP_OK) {
    Serial.printf("SENT: %s Car %d (%d Hz)\n", sensorName, car, (int)freq);
  } else {
    Serial.printf("SEND FAILED: %s Car %d\n", sensorName, car);
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
        sendCarEvent(sensor.name, car, freq);
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
  pinMode(SENSOR_A_PIN, INPUT);
  pinMode(SENSOR_B_PIN, INPUT);
  pinMode(SENSOR_C_PIN, INPUT);
  pinMode(SENSOR_D_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(SENSOR_A_PIN), onPulseA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B_PIN), onPulseB, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_C_PIN), onPulseC, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_D_PIN), onPulseD, FALLING);

  Serial.println("\nSensors:");
  Serial.printf("  %s - GPIO %d\n", SENSOR_A_NAME, SENSOR_A_PIN);
  Serial.printf("  %s - GPIO %d\n", SENSOR_B_NAME, SENSOR_B_PIN);
  Serial.printf("  %s - GPIO %d\n", SENSOR_C_NAME, SENSOR_C_PIN);
  Serial.printf("  %s - GPIO %d\n", SENSOR_D_NAME, SENSOR_D_PIN);
  Serial.println("\nWaiting for cars...\n");
}

void loop() {
  processSensor(sensorA);
  processSensor(sensorB);
  processSensor(sensorC);
  processSensor(sensorD);
  delay(1);
}
