#include <Arduino.h>
#include <Keypad.h>

// Scalextric Car Detector - Interactive Test Mode
// Press 1-6 on keypad or serial monitor to pulse IR LED at that car's frequency
//
// Phototransistor wiring (common emitter):
//         3.3V
//           |
//         [4.7kΩ]
//           |
//           +-------- GPIO 4
//           |
//     +-----+-----+
//     |           |
//   [22nF]    Collector (short leg)
//     |        TEFT4300
//    GND      Emitter (long leg)
//                 |
//                GND
//
// 4x4 Keypad wiring:
//   Rows: GPIO 18, 19, 21, 22
//   Cols: GPIO 27, 26, 25, 33

const int IR_SENSOR_PIN = 4;
const int TEST_LED_PIN = 5;

// Scalextric Digital car frequencies (Hz)
const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const int FREQUENCY_TOLERANCE = 300;

// Valid interval range for car frequencies
const unsigned long MIN_VALID_INTERVAL = 150;
const unsigned long MAX_VALID_INTERVAL = 450;

// Detection state
const int MIN_PULSES_FOR_ID = 10;
const unsigned long DETECTION_TIMEOUT = 50000;
const int CONFIRM_COUNT = 3;

// Pulse history
const int HISTORY_SIZE = 10;
volatile unsigned long intervalHistory[HISTORY_SIZE];
volatile int historyIndex = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile int pulseCount = 0;
volatile bool newPulseData = false;

// Test mode state
const unsigned long PULSE_DURATION = 500;  // 500ms pulse when key pressed
unsigned long pulseStartTime = 0;
bool pulsing = false;
int currentTestCar = 0;

// 4x4 Keypad
const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {18, 19, 21, 22};
byte colPins[COLS] = {27, 26, 25, 33};
Keypad keypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

void IRAM_ATTR onPulseDetected() {
  unsigned long now = micros();
  if (lastPulseTime > 0) {
    pulseInterval = now - lastPulseTime;
    if (pulseInterval >= MIN_VALID_INTERVAL && pulseInterval <= MAX_VALID_INTERVAL) {
      intervalHistory[historyIndex] = pulseInterval;
      historyIndex = (historyIndex + 1) % HISTORY_SIZE;
      pulseCount++;
      newPulseData = true;
    }
  }
  lastPulseTime = now;
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

float calculateMedianFrequency() {
  unsigned long temp[HISTORY_SIZE];
  int validSamples = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (intervalHistory[i] > 0) {
      temp[validSamples++] = intervalHistory[i];
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

void resetDetection() {
  pulseCount = 0;
  historyIndex = 0;
  lastPulseTime = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    intervalHistory[i] = 0;
  }
}

void startPulse(char key) {
  int carIndex = key - '1';  // 0-5
  currentTestCar = carIndex + 1;  // 1-6
  Serial.printf("\n>>> Pulsing Car %d (%d Hz) <<<\n", currentTestCar, CAR_FREQUENCIES[carIndex]);
  tone(TEST_LED_PIN, CAR_FREQUENCIES[carIndex]);
  pulsing = true;
  pulseStartTime = millis();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nScalextric Test - Interactive Mode");
  Serial.println("===================================");
  Serial.println("Press 1-6 on keypad or serial monitor:");
  Serial.println("  1 = 5500 Hz");
  Serial.println("  2 = 4400 Hz");
  Serial.println("  3 = 3700 Hz");
  Serial.println("  4 = 3100 Hz");
  Serial.println("  5 = 2800 Hz");
  Serial.println("  6 = 2400 Hz");
  Serial.println();

  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(TEST_LED_PIN, OUTPUT);

  attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN), onPulseDetected, FALLING);

  // Initialize tone
  tone(TEST_LED_PIN, 1000);
  delay(10);
  noTone(TEST_LED_PIN);

  Serial.println("Ready. Point IR LED at phototransistor.\n");
}

void loop() {
  static unsigned long lastActivityTime = 0;
  static bool detecting = false;
  static int lastCarDetected = 0;
  static int candidateCar = 0;
  static int confirmCount = 0;

  // Check for keypad input
  char key = keypad.getKey();
  if (key >= '1' && key <= '6') {
    startPulse(key);
  }

  // Check for serial input
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '6') {
      startPulse(c);
    }
  }

  // Stop pulse after duration
  if (pulsing && (millis() - pulseStartTime > PULSE_DURATION)) {
    noTone(TEST_LED_PIN);
    pulsing = false;
  }

  // Check for new pulse data
  if (newPulseData) {
    newPulseData = false;
    lastActivityTime = micros();
    if (!detecting) {
      detecting = true;
    }
  }

  // Check if we have enough data to identify
  if (detecting && pulseCount >= MIN_PULSES_FOR_ID) {
    float freq = calculateMedianFrequency();
    int car = identifyCar(freq);

    if (car > 0) {
      if (car == candidateCar) {
        confirmCount++;
      } else {
        candidateCar = car;
        confirmCount = 1;
      }

      if (confirmCount >= CONFIRM_COUNT && car != lastCarDetected) {
        if (currentTestCar > 0) {
          const char* result = (car == currentTestCar) ? "OK" : "MISMATCH";
          Serial.printf("Sent: Car %d | Detected: Car %d (%.0f Hz) [%s]\n",
                        currentTestCar, car, freq, result);
        } else {
          Serial.printf("Detected: Car %d (%.0f Hz)\n", car, freq);
        }
        lastCarDetected = car;
      }
    }
  }

  // Check for timeout
  if (detecting && (micros() - lastActivityTime > DETECTION_TIMEOUT)) {
    detecting = false;
    lastCarDetected = 0;
    candidateCar = 0;
    confirmCount = 0;
    currentTestCar = 0;
    resetDetection();
  }

  delay(1);
}
