#include <Arduino.h>

// Scalextric Digital Car Detector - 4 Sensor Version
// Detects cars 1-6 by their unique IR pulse frequencies
//
// Phototransistor wiring (common emitter config):
//
//         3.3V
//           |
//         [4.7kΩ]
//           |
//           +-------- GPIO
//           |
//     +-----+-----+
//     |           |
//   [22nF]    Collector (short leg)
//     |        TEFT4300
//    GND      Emitter (long leg)
//                 |
//                GND

// Sensor GPIO pins
const int SENSOR_A_PIN = 4;   // Start Lane 1
const int SENSOR_B_PIN = 5;   // Start Lane 2
const int SENSOR_C_PIN = 16;  // Pit Entry
const int SENSOR_D_PIN = 17;  // Pit Exit

// Scalextric Digital car frequencies (Hz)
// Car 1: 5500 Hz, Car 2: 4400 Hz, Car 3: 3700 Hz
// Car 4: 3100 Hz, Car 5: 2800 Hz, Car 6: 2400 Hz
const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const int FREQUENCY_TOLERANCE = 300;  // Hz tolerance for matching

// Valid interval range for car frequencies (2400-5500 Hz = 182-417 us)
const unsigned long MIN_VALID_INTERVAL = 150;  // ~6667 Hz max (with margin)
const unsigned long MAX_VALID_INTERVAL = 450;  // ~2222 Hz min (with margin)

// Detection state
const int MIN_PULSES_FOR_ID = 10;     // Need several pulses to confirm frequency
const unsigned long DETECTION_TIMEOUT = 50000;  // 50ms - car has passed
const int CONFIRM_COUNT = 3;          // Need this many consistent readings to confirm car

// Pulse history for frequency calculation
const int HISTORY_SIZE = 10;

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
SensorState sensorA = {"START1", SENSOR_A_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};
SensorState sensorB = {"START2", SENSOR_B_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};
SensorState sensorC = {"PIT_IN", SENSOR_C_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};
SensorState sensorD = {"PIT_OUT", SENSOR_D_PIN, 0, 0, 0, false, {0}, 0, 0, false, 0, 0, 0};

// ISR for sensor A
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

// ISR for sensor B
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

// ISR for sensor C
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

// ISR for sensor D
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
      bestCar = car + 1;  // Cars are 1-6, not 0-5
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

  // Simple bubble sort (small array)
  for (int i = 0; i < validSamples - 1; i++) {
    for (int j = 0; j < validSamples - i - 1; j++) {
      if (temp[j] > temp[j + 1]) {
        unsigned long swap = temp[j];
        temp[j] = temp[j + 1];
        temp[j + 1] = swap;
      }
    }
  }

  unsigned long medianInterval = temp[validSamples / 2];
  return 1000000.0 / medianInterval;
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

void processSensor(SensorState& sensor) {
  // Check for new pulse data
  if (sensor.newPulseData) {
    sensor.newPulseData = false;
    sensor.lastActivityTime = micros();

    if (!sensor.detecting) {
      sensor.detecting = true;
    }
  }

  // Check if we have enough data to identify
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

      // Report when confirmed and different from last
      if (sensor.confirmCount >= CONFIRM_COUNT && car != sensor.lastCarDetected) {
        Serial.printf("%s: Car %d (%.0f Hz)\n", sensor.name, car, freq);
        sensor.lastCarDetected = car;
      }
    }
  }

  // Check for timeout (car has passed)
  if (sensor.detecting && (micros() - sensor.lastActivityTime > DETECTION_TIMEOUT)) {
    if (sensor.lastCarDetected == 0 && sensor.pulseCount > 0) {
      float freq = calculateMedianFrequency(sensor.intervalHistory);
      Serial.printf("%s: Unknown signal (%.0f Hz, %d pulses)\n",
                    sensor.name, freq, sensor.pulseCount);
    }
    resetSensor(sensor);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nScalextric Car Detector - 4 Sensor");
  Serial.println("==================================");
  Serial.println("Car frequencies:");
  Serial.println("  Car 1: 5500 Hz");
  Serial.println("  Car 2: 4400 Hz");
  Serial.println("  Car 3: 3700 Hz");
  Serial.println("  Car 4: 3100 Hz");
  Serial.println("  Car 5: 2800 Hz");
  Serial.println("  Car 6: 2400 Hz");
  Serial.println();

  pinMode(SENSOR_A_PIN, INPUT);
  pinMode(SENSOR_B_PIN, INPUT);
  pinMode(SENSOR_C_PIN, INPUT);
  pinMode(SENSOR_D_PIN, INPUT);

  attachInterrupt(digitalPinToInterrupt(SENSOR_A_PIN), onPulseA, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_B_PIN), onPulseB, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_C_PIN), onPulseC, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_D_PIN), onPulseD, FALLING);

  Serial.println("Sensors:");
  Serial.printf("  START1  - GPIO %d (Start Lane 1)\n", SENSOR_A_PIN);
  Serial.printf("  START2  - GPIO %d (Start Lane 2)\n", SENSOR_B_PIN);
  Serial.printf("  PIT_IN  - GPIO %d (Pit Entry)\n", SENSOR_C_PIN);
  Serial.printf("  PIT_OUT - GPIO %d (Pit Exit)\n", SENSOR_D_PIN);
  Serial.println("\nWaiting for cars...\n");
}

void loop() {
  processSensor(sensorA);
  processSensor(sensorB);
  processSensor(sensorC);
  processSensor(sensorD);
  delay(1);
}
