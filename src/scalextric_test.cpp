#include <Arduino.h>
#include <Keypad.h>

// Scalextric IR LED Test
// Press 1-6 to pulse IR LED at that car's frequency
// A = set 2s duration, B = set 5s duration (default 0.5s)
// C = rotate through all 6 cars (0.5s each)
//
// IR LED on GPIO 5 (via tone())
//
// 4x4 Keypad wiring:
//   Rows: GPIO 18, 19, 21, 22
//   Cols: GPIO 27, 26, 25, 33

const int IR_LED_PIN = 5;
const unsigned long DEFAULT_DURATION = 500;

const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};

unsigned long pulseDuration = DEFAULT_DURATION;
unsigned long pulseStartTime = 0;
bool pulsing = false;

// Rotate mode (C key)
bool rotating = false;
int rotateIndex = 0;
bool rotateGap = false;
unsigned long gapStartTime = 0;
const unsigned long GAP_DURATION = 150;  // ms gap between rotated cars (detector needs 50ms to reset)

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

void startPulse(int carIndex, unsigned long duration) {
  int car = carIndex + 1;
  Serial.printf("Car %d (%d Hz) - %lums\n", car, CAR_FREQUENCIES[carIndex], duration);
  tone(IR_LED_PIN, CAR_FREQUENCIES[carIndex]);
  pulsing = true;
  pulseDuration = duration;
  pulseStartTime = millis();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nScalextric IR LED Test");
  Serial.println("======================");
  Serial.println("Press 1-6: pulse at car frequency (0.5s)");
  Serial.println("  1 = 5500 Hz");
  Serial.println("  2 = 4400 Hz");
  Serial.println("  3 = 3700 Hz");
  Serial.println("  4 = 3100 Hz");
  Serial.println("  5 = 2800 Hz");
  Serial.println("  6 = 2400 Hz");
  Serial.println("A = 2s duration, B = 5s duration");
  Serial.println("C = rotate all 6 cars (0.5s each)\n");

  pinMode(IR_LED_PIN, OUTPUT);
}

unsigned long nextDuration = DEFAULT_DURATION;

void loop() {
  // Handle gap between rotated cars
  if (rotateGap && (millis() - gapStartTime > GAP_DURATION)) {
    rotateGap = false;
    rotateIndex++;
    if (rotateIndex < 6) {
      startPulse(rotateIndex, DEFAULT_DURATION);
    } else {
      rotating = false;
      Serial.println("Rotate done");
    }
  }

  // Handle pulse timeout
  if (pulsing && (millis() - pulseStartTime > pulseDuration)) {
    noTone(IR_LED_PIN);
    pulsing = false;

    if (rotating) {
      // Start gap before next car so detector can reset
      rotateGap = true;
      gapStartTime = millis();
    } else {
      Serial.println("Off");
    }
  }

  if (pulsing || rotateGap) {
    delay(1);
    return;
  }

  char key = keypad.getKey();
  if (key) {
    if (key >= '1' && key <= '6') {
      startPulse(key - '1', nextDuration);
      nextDuration = DEFAULT_DURATION;
    } else if (key == 'A') {
      nextDuration = 2000;
      Serial.println("Duration: 2s");
    } else if (key == 'B') {
      nextDuration = 5000;
      Serial.println("Duration: 5s");
    } else if (key == 'C') {
      rotating = true;
      rotateIndex = 0;
      Serial.println("Rotating all 6 cars...");
      startPulse(0, DEFAULT_DURATION);
    }
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '6') {
      startPulse(c - '1', nextDuration);
      nextDuration = DEFAULT_DURATION;
    } else if (c == 'a' || c == 'A') {
      nextDuration = 2000;
      Serial.println("Duration: 2s");
    } else if (c == 'b' || c == 'B') {
      nextDuration = 5000;
      Serial.println("Duration: 5s");
    } else if (c == 'c' || c == 'C') {
      rotating = true;
      rotateIndex = 0;
      Serial.println("Rotating all 6 cars...");
      startPulse(0, DEFAULT_DURATION);
    }
  }

  delay(1);
}
