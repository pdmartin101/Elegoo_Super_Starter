#ifndef CAR_DETECTION_H
#define CAR_DETECTION_H

#include <Arduino.h>
#include "scalextric_protocol.h"

// Scalextric Car Detector - Shared Detection Logic
// Header-only library used by both parent and child nodes

// Callback type for car detection events
typedef void (*CarDetectedCallback)(uint8_t sensorId, int car, float freq);

// ========== SENSOR STATE ==========

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

SensorState sensors[NUM_SENSORS];

// ========== ISRs (attachInterrupt needs separate function pointers) ==========

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

// ========== DETECTION FUNCTIONS ==========

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
  noInterrupts();
  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (history[i] > 0) {
      temp[validSamples++] = history[i];
    }
  }
  interrupts();
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
  noInterrupts();
  sensor.pulseCount = 0;
  sensor.historyIndex = 0;
  sensor.lastPulseTime = 0;
  sensor.newPulseData = false;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    sensor.intervalHistory[i] = 0;
  }
  interrupts();
  sensor.detecting = false;
  sensor.lastCarDetected = 0;
  sensor.candidateCar = 0;
  sensor.confirmCount = 0;
}

void processSensor(SensorState& sensor, CarDetectedCallback onCarDetected) {
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
        onCarDetected(sensor.id, car, freq);
        sensor.lastCarDetected = car;
      }
    }
  }

  if (sensor.detecting && (micros() - sensor.lastActivityTime > DETECTION_TIMEOUT)) {
    // Report unknown car if we had enough pulses but never matched
    if (sensor.lastCarDetected == 0 && sensor.pulseCount >= MIN_PULSES_FOR_ID) {
      float freq = calculateMedianFrequency(sensor.intervalHistory);
      if (freq > 0) {
        onCarDetected(sensor.id, 0, freq);
      }
    }
    resetSensor(sensor);
  }
}

// ========== SENSOR INITIALISATION ==========

void initSensors() {
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
  }
}

#endif
