#include <Arduino.h>

// Phototransistor connected to this GPIO (must support interrupts)
// Wiring (common emitter with noise filter):
//
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
const int IR_SENSOR_PIN = 4;

// ========== TEST MODE ==========
// Set to true to enable test signal output
// Connect IR LED: GPIO 5 --[220Ω]-- LED anode, cathode to GND
// Point LED at phototransistor to simulate car passing
const bool TEST_MODE = true;
const int TEST_LED_PIN = 5;
int testCarIndex = 0;  // Which car to simulate (0-5)

// Debug mode - shows real-time frequency measurements
const bool DEBUG_MODE = false;  // Set true for verbose output

// Scalextric Digital car frequencies (Hz)
// Car 1: 5500 Hz, Car 2: 4400 Hz, Car 3: 3700 Hz
// Car 4: 3100 Hz, Car 5: 2800 Hz, Car 6: 2400 Hz
const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const int FREQUENCY_TOLERANCE = 300;  // Hz tolerance for matching

// Valid interval range for car frequencies (2400-5500 Hz = 182-417 us)
const unsigned long MIN_VALID_INTERVAL = 150;  // ~6667 Hz max (with margin)
const unsigned long MAX_VALID_INTERVAL = 450;  // ~2222 Hz min (with margin)

// Timing variables (volatile for ISR access)
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
volatile int pulseCount = 0;
volatile bool newPulseData = false;

// Detection state
const int MIN_PULSES_FOR_ID = 10;     // Need several pulses to confirm frequency
const unsigned long DETECTION_TIMEOUT = 50000;  // 50ms - car has passed
const int CONFIRM_COUNT = 3;          // Need this many consistent readings to confirm car

// Pulse history for frequency calculation
const int HISTORY_SIZE = 10;
volatile unsigned long intervalHistory[HISTORY_SIZE];
volatile int historyIndex = 0;

// Test mode variables - auto cycles through all cars
const unsigned long TEST_BURST_DURATION = 10000;  // 10 second pulse burst
const unsigned long TEST_GAP_DURATION = 2000;     // 2 seconds between tests
unsigned long lastTestTime = 0;
bool testBurstActive = false;
bool toneInitialized = false;

// ISR for falling edge detection
void IRAM_ATTR onPulseDetected() {
  unsigned long now = micros();

  if (lastPulseTime > 0) {
    pulseInterval = now - lastPulseTime;

    // Filter: only accept intervals in valid range for car frequencies
    if (pulseInterval >= MIN_VALID_INTERVAL && pulseInterval <= MAX_VALID_INTERVAL) {
      // Store in history for averaging
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
  float bestDiff = FREQUENCY_TOLERANCE;  // Must be within tolerance

  for (int car = 0; car < 6; car++) {
    float diff = abs(frequency - CAR_FREQUENCIES[car]);
    if (diff < bestDiff) {
      bestDiff = diff;
      bestCar = car + 1;  // Cars are 1-6, not 0-5
    }
  }
  return bestCar;
}

float calculateMedianFrequency() {
  // Copy valid intervals to temp array for sorting
  unsigned long temp[HISTORY_SIZE];
  int validSamples = 0;

  for (int i = 0; i < HISTORY_SIZE; i++) {
    if (intervalHistory[i] > 0) {
      temp[validSamples++] = intervalHistory[i];
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

  // Get median
  unsigned long medianInterval = temp[validSamples / 2];
  return 1000000.0 / medianInterval;  // Convert microseconds to Hz
}

void resetDetection() {
  pulseCount = 0;
  historyIndex = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) {
    intervalHistory[i] = 0;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nScalextric Car Detector");
  Serial.println("=======================");
  Serial.println("Car frequencies:");
  Serial.println("  Car 1: 5500 Hz");
  Serial.println("  Car 2: 4400 Hz");
  Serial.println("  Car 3: 3700 Hz");
  Serial.println("  Car 4: 3100 Hz");
  Serial.println("  Car 5: 2800 Hz");
  Serial.println("  Car 6: 2400 Hz");
  Serial.println();

  pinMode(IR_SENSOR_PIN, INPUT);
  // FALLING edge: light detected pulls GPIO low (common emitter config)
  attachInterrupt(digitalPinToInterrupt(IR_SENSOR_PIN), onPulseDetected, FALLING);

  Serial.printf("Listening on GPIO %d...\n\n", IR_SENSOR_PIN);

  if (DEBUG_MODE) {
    Serial.println("*** DEBUG MODE ENABLED - showing real-time frequency ***\n");
  }

  if (TEST_MODE) {
    pinMode(TEST_LED_PIN, OUTPUT);
    // Initialize LEDC channel to avoid "not initialized" warning
    tone(TEST_LED_PIN, 1000);
    delay(10);  // Let LEDC fully initialize
    noTone(TEST_LED_PIN);
    toneInitialized = true;
    Serial.println("AUTO-TEST: Cycling through all cars every 2 seconds");
    Serial.println("Point IR LED at phototransistor\n");
  }
}

void runAutoTest() {
  if (!TEST_MODE) return;

  unsigned long now = millis();

  // Start a new test burst every TEST_GAP_DURATION
  if (!testBurstActive && (now - lastTestTime > TEST_GAP_DURATION)) {
    testCarIndex = (testCarIndex + 1) % 6;  // Cycle 0-5
    Serial.printf("\n--- Testing Car %d (%d Hz) for 10 seconds ---\n",
                  testCarIndex + 1, CAR_FREQUENCIES[testCarIndex]);
    tone(TEST_LED_PIN, CAR_FREQUENCIES[testCarIndex]);
    toneInitialized = true;
    testBurstActive = true;
    lastTestTime = now;
  }

  // End the burst after TEST_BURST_DURATION
  if (testBurstActive && (now - lastTestTime > TEST_BURST_DURATION)) {
    if (toneInitialized) {
      noTone(TEST_LED_PIN);
    }
    testBurstActive = false;
  }
}

void loop() {
  static unsigned long lastActivityTime = 0;
  static bool detecting = false;
  static int lastCarDetected = 0;
  static int candidateCar = 0;
  static int confirmCount = 0;

  // Run auto-test (cycles through all cars)
  runAutoTest();

  // Check for new pulse data
  if (newPulseData) {
    newPulseData = false;
    lastActivityTime = micros();

    if (!detecting) {
      detecting = true;
      if (DEBUG_MODE) {
        Serial.println("IR pulses detected...");
      }
    }

    // Debug: show real-time pulse info
    if (DEBUG_MODE && pulseCount > 0 && pulseCount % 10 == 0) {
      float freq = calculateMedianFrequency();
      Serial.printf("  [DEBUG] pulses: %d, interval: %lu us, freq: %.0f Hz\n",
                    pulseCount, pulseInterval, freq);
    }
  }

  // Check if we have enough data to identify
  if (detecting && pulseCount >= MIN_PULSES_FOR_ID) {
    float freq = calculateMedianFrequency();
    int car = identifyCar(freq);

    if (car > 0) {
      // Confirmation logic: require consistent readings
      if (car == candidateCar) {
        confirmCount++;
      } else {
        candidateCar = car;
        confirmCount = 1;
      }

      // Only report when confirmed and different from last
      if (confirmCount >= CONFIRM_COUNT && car != lastCarDetected) {
        if (TEST_MODE) {
          int simCar = testCarIndex + 1;
          const char* result = (car == simCar) ? "OK" : "MISMATCH";
          Serial.printf("Simulated: Car %d (%d Hz) | Detected: Car %d (%.0f Hz) [%s]\n",
                        simCar, CAR_FREQUENCIES[testCarIndex], car, freq, result);
        } else {
          Serial.printf("CAR %d detected (freq: %.0f Hz)\n", car, freq);
        }
        lastCarDetected = car;
      }
    }
  }

  // Check for timeout (car has passed)
  if (detecting && (micros() - lastActivityTime > DETECTION_TIMEOUT)) {
    float freq = calculateMedianFrequency();

    // Only show "passed" message in debug mode or if detection failed
    if (DEBUG_MODE) {
      if (lastCarDetected > 0) {
        Serial.printf("Car %d passed (final freq: %.0f Hz, pulses: %d)\n", lastCarDetected, freq, pulseCount);
      } else if (pulseCount > 0) {
        Serial.printf("Unknown signal (freq: %.0f Hz, pulses: %d)\n", freq, pulseCount);
      }
      // Debug: show interval histogram
      if (pulseCount > 0) {
        Serial.print("  [DEBUG] intervals (us): ");
        for (int i = 0; i < HISTORY_SIZE && intervalHistory[i] > 0; i++) {
          Serial.printf("%lu ", intervalHistory[i]);
        }
        Serial.println("\n");
      }
    } else if (lastCarDetected == 0 && pulseCount > 0) {
      // Always warn about failed detections
      Serial.printf("Unknown signal (freq: %.0f Hz, pulses: %d)\n", freq, pulseCount);
    }

    detecting = false;
    lastCarDetected = 0;
    candidateCar = 0;
    confirmCount = 0;
    lastPulseTime = 0;
    resetDetection();
  }

  delay(1);
}
