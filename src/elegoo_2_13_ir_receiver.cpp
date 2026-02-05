#include <Arduino.h>

// ESP32 NEC Infrared Decoding (No External Libraries)
// Decode NEC protocol with strict timing requirements
const int IR = 25;  // IR receiver data pin

/**
 * @brief Read the duration of a specific logic level (HIGH/LOW)
 * @param level Target logic level to read (HIGH/LOW)
 * @param timeout Maximum waiting time in microseconds (default: 20000us = 20ms)
 * @return Duration of the level in microseconds, 0 if timeout
 */
unsigned long readPulse(int level, unsigned long timeout = 20000) {
  unsigned long start = micros();  // Record start time in microseconds
  // Wait until level changes or timeout
  while (digitalRead(IR) == level) {
    if (micros() - start > timeout) return 0;  // Return 0 on timeout
  }
  return micros() - start;  // Return the actual duration of the level
}

void setup() {
  Serial.begin(9600);  // Initialize serial communication at 9600 baud rate
  pinMode(IR, INPUT);  // Set IR pin as input
  Serial.println("NEC decoder started...");
}

void loop() {
  // Wait for NEC leader code LOW (9ms)
  if (digitalRead(IR) == LOW) {
    unsigned long lowTime = readPulse(LOW);
    // Validate leader code LOW duration (8ms ~ 10ms, target: 9ms)
    if (lowTime < 8000 || lowTime > 10000) return;

    // Read leader code HIGH (4.5ms)
    unsigned long highTime = readPulse(HIGH);
    // Validate leader code HIGH duration (4ms ~ 5ms, target: 4.5ms)
    if (highTime < 4000 || highTime > 5000) return;

    // Start reading 32-bit data (16-bit address + 16-bit command)
    unsigned long code = 0;

    for (int i = 0; i < 32; i++) {
      // Each bit starts with 560us LOW
      unsigned long bitLow = readPulse(LOW);
      // Validate bit LOW duration (400us ~ 700us, target: 560us)
      if (bitLow < 400 || bitLow > 700) return;

      // Then read HIGH duration: 560us = 0, 1680us = 1
      unsigned long bitHigh = readPulse(HIGH);
      if (bitHigh == 0) return;  // Return if timeout

      code <<= 1;  // Shift left to make room for new bit
      if (bitHigh > 1000) code |= 1;  // Long HIGH (1680us) = bit 1
      else code |= 0;                 // Short HIGH (560us) = bit 0
    }

    // Print decoded code in hexadecimal format
    Serial.print("Received code: 0x");
    Serial.println(code, HEX);

    // Match code to corresponding button
    switch (code) {
      case 0xFFA25D: Serial.println("POWER"); break;
      case 0xFFE21D: Serial.println("FUNC/STOP"); break;
      case 0xFF629D: Serial.println("VOL+"); break;
      case 0xFF22DD: Serial.println("FAST BACK"); break;
      case 0xFF02FD: Serial.println("PAUSE"); break;
      case 0xFFC23D: Serial.println("FAST FORWARD"); break;
      case 0xFFE01F: Serial.println("DOWN"); break;
      case 0xFFA857: Serial.println("VOL-"); break;
      case 0xFF906F: Serial.println("UP"); break;
      case 0xFF9867: Serial.println("EQ"); break;
      case 0xFFB04F: Serial.println("ST/REPT"); break;
      case 0xFF6897: Serial.println("0"); break;
      case 0xFF30CF: Serial.println("1"); break;
      case 0xFF18E7: Serial.println("2"); break;
      case 0xFF7A85: Serial.println("3"); break;
      case 0xFF10EF: Serial.println("4"); break;
      case 0xFF38C7: Serial.println("5"); break;
      case 0xFF5AA5: Serial.println("6"); break;
      case 0xFF42BD: Serial.println("7"); break;
      case 0xFF4AB5: Serial.println("8"); break;
      case 0xFF52AD: Serial.println("9"); break;
      case 0xFFFFFFFF: Serial.println("REPEAT"); break;
      default: Serial.println("OTHER BUTTON"); break;
    }

    delay(150);  // Debounce delay to avoid duplicate readings
  }
}
