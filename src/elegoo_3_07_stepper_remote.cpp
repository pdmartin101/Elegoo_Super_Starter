#include <Arduino.h>

#include <Stepper.h>

// Stepper motor parameters (28BYJ-48 + ULN2003 compatible)
const int stepsPerRevolution = 2048;  // Steps per full rotation (standard for 28BYJ-48)
const int motorSpeed = 10;            // Rotation speed (5-15 rpm, higher = faster)

// Motor pin definitions (LN1-LN4 connected to ESP32 pins, optimized for correct direction)
// If rotation direction is incorrect, try swapping the last two pins (e.g., change to 21,19,5,18)
Stepper myStepper(stepsPerRevolution, 5, 19, 18, 21);  // LN1-LN3-LN2-LN4

// IR receiver pin
const int IR_PIN = 25;

// Motor state variables
bool isRunning = false;               // Running state flag
int motorDirection = 1;               // 1=clockwise, -1=counterclockwise
unsigned long lastCmdTime = 0;        // Last command received time
const unsigned long STOP_DELAY = 3000; // Auto-stop delay with no operation (3 seconds)

// Read IR pulse duration (in microseconds)
unsigned long readPulse(int level, unsigned long timeout = 20000) {
  unsigned long start = micros();
  while (digitalRead(IR_PIN) == level) {
    if (micros() - start > timeout) return 0; // Return 0 on timeout
  }
  return micros() - start;
}

// Process IR code and control motor
void handleIRCode(unsigned long code) {
  Serial.print("IR Code: 0x");
  Serial.println(code, HEX); // Print raw code for debugging

  switch (code) {
    case 0xFF629D:  // VOL+ code (update with your actual code from serial monitor)
      motorDirection = 1;    // Clockwise rotation
      isRunning = true;
      lastCmdTime = millis();
      Serial.println("Command: VOL+ → Clockwise");
      break;
    case 0xFFA857:  // VOL- code (update with your actual code from serial monitor)
      motorDirection = -1;   // Counterclockwise rotation
      isRunning = true;
      lastCmdTime = millis();
      Serial.println("Command: VOL- → Counterclockwise");
      break;
    case 0xFFFFFFFF: // Long press repeat code
      if (isRunning) {
        lastCmdTime = millis(); // Extend running time
        Serial.println("Command: Long Press → Continue Running");
      }
      break;
    default:
      Serial.println("Undefined key, ignored");
      break;
  }
}

void setup() {
  // Initialize motor
  myStepper.setSpeed(motorSpeed);
  // Initialize IR pin
  pinMode(IR_PIN, INPUT);
  // Initialize serial communication (baud rate 9600)
  Serial.begin(9600);
  Serial.println("=== System Started Successfully ===");
  Serial.println("Operation Guide:");
  Serial.println("VOL+ → Motor Clockwise");
  Serial.println("VOL- → Motor Counterclockwise");
  Serial.println("Auto-stop after 3 seconds of inactivity");
  Serial.println("-------------------");
}

void loop() {
  // IR signal decoding
  if (digitalRead(IR_PIN) == LOW) { // Detect IR start low level
    // Verify leader code (9ms low level)
    unsigned long lowTime = readPulse(LOW);
    if (lowTime < 8000 || lowTime > 10000) return;

    // Verify 4.5ms high level after leader code
    unsigned long highTime = readPulse(HIGH);
    if (highTime < 4000 || highTime > 5000) return;

    // Read 32-bit data code
    unsigned long code = 0;
    for (int i = 0; i < 32; i++) {
      // Bit start low level (560us)
      unsigned long bitLow = readPulse(LOW);
      if (bitLow < 400 || bitLow > 700) return;

      // Bit data high level (560us=0, 1680us=1)
      unsigned long bitHigh = readPulse(HIGH);
      if (bitHigh == 0) return;

      code <<= 1;
      if (bitHigh > 1000) code |= 1; // High level >1000us is considered 1
    }

    handleIRCode(code); // Process decoded command
    delay(50); // Debounce delay
  }

  // Motor operation control
  if (isRunning) {
    // Check for auto-stop timeout
    if (millis() - lastCmdTime > STOP_DELAY) {
      isRunning = false;
      Serial.println("Status: Auto-stopped due to timeout");
    } else {
      // Continuous rotation (1 step at a time to avoid blocking program)
      myStepper.step(motorDirection);
    }
  }
}
