#include <Arduino.h>

// Scalextric Frequency Logger
// Reads phototransistor sensors and logs detected frequencies to Serial.
// No car identification, no ESP-NOW, no WiFi - just raw frequency output.
//
// Sensor pins: GPIO 4, 5, 18, 19 (INPUT_PULLUP)
// Use the same phototransistor circuit as the car detector.

const int SENSOR_PINS[] = {4, 5, 18, 19};
const int NUM_SENSORS = 4;
const int HISTORY_SIZE = 10;
const unsigned long REPORT_INTERVAL = 500000;  // 500ms in micros

struct SensorState {
  volatile unsigned long lastPulseTime;
  volatile unsigned long intervalHistory[HISTORY_SIZE];
  volatile int historyIndex;
  volatile int pulseCount;
  volatile bool active;
};

SensorState sensors[NUM_SENSORS];
unsigned long lastReportTime = 0;

void IRAM_ATTR onPulse(int i) {
  unsigned long now = micros();
  SensorState& s = sensors[i];
  unsigned long delta = now - s.lastPulseTime;

  if (delta < 40) return;  // ignore bounce/glitch

  if (s.lastPulseTime > 0 && delta >= 150 && delta <= 450) {
    s.intervalHistory[s.historyIndex] = delta;
    s.historyIndex = (s.historyIndex + 1) % HISTORY_SIZE;
    s.pulseCount++;
    s.active = true;
  }
  s.lastPulseTime = now;
}

void IRAM_ATTR onPulse0() { onPulse(0); }
void IRAM_ATTR onPulse1() { onPulse(1); }
void IRAM_ATTR onPulse2() { onPulse(2); }
void IRAM_ATTR onPulse3() { onPulse(3); }

void (*isrFunctions[])() = {onPulse0, onPulse1, onPulse2, onPulse3};

float calculateMedianFrequency(volatile unsigned long* history) {
  unsigned long temp[HISTORY_SIZE];
  int validSamples = 0;
  noInterrupts();
  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (history[i] > 0) {
      temp[validSamples++] = history[i];
    }
  }
  interrupts();
  if (validSamples < 3) return 0;
  // Bubble sort for median
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

void setup() {
  Serial.begin(115200);
  Serial.println("\nScalextric Frequency Logger");
  Serial.println("===========================");
  Serial.println("Logs raw frequencies from phototransistor sensors.");
  Serial.println();

  for (int i = 0; i < NUM_SENSORS; i++) {
    sensors[i].lastPulseTime = 0;
    sensors[i].historyIndex = 0;
    sensors[i].pulseCount = 0;
    sensors[i].active = false;
    for (int j = 0; j < HISTORY_SIZE; j++) {
      sensors[i].intervalHistory[j] = 0;
    }
    pinMode(SENSOR_PINS[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(SENSOR_PINS[i]), isrFunctions[i], FALLING);
    Serial.printf("  Sensor %d: GPIO %d\n", i, SENSOR_PINS[i]);
  }

  Serial.println("\nFormat: S<sensor>=<freq>Hz (<pulses> pulses)");
  Serial.println("Waiting for signals...\n");
}

void loop() {
  if (micros() - lastReportTime < REPORT_INTERVAL) {
    delay(1);
    return;
  }
  lastReportTime = micros();

  bool anyActive = false;
  for (int i = 0; i < NUM_SENSORS; i++) {
    if (!sensors[i].active) continue;
    anyActive = true;

    float freq = calculateMedianFrequency(sensors[i].intervalHistory);
    int pulses = sensors[i].pulseCount;

    if (freq > 0) {
      Serial.printf("S%d=%d Hz  (%d pulses)\n", i, (int)freq, pulses);
    }

    // Reset for next report window
    noInterrupts();
    sensors[i].pulseCount = 0;
    sensors[i].active = false;
    sensors[i].historyIndex = 0;
    sensors[i].lastPulseTime = 0;
    for (int j = 0; j < HISTORY_SIZE; j++) {
      sensors[i].intervalHistory[j] = 0;
    }
    interrupts();
  }

  if (anyActive) {
    Serial.println("---");
  }
}
