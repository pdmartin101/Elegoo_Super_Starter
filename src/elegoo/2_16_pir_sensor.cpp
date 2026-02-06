#include <Arduino.h>

//www.elegoo.com
//2016.12.9

int ledPin = 2;  // LED on Pin 2
int pirPin = 18; // Input for HC-S501

int pirValue;     // Current PIR value
int lastPirValue = LOW; // Previous PIR value for change detection

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(pirPin, INPUT);
  digitalWrite(ledPin, LOW);
  Serial.println("\nPIR Sensor Test");
  Serial.println("Waiting for motion...\n");
}

void loop() {
  pirValue = digitalRead(pirPin);
  digitalWrite(ledPin, pirValue);

  // Report only when state changes
  if (pirValue != lastPirValue) {
    if (pirValue == HIGH) {
      Serial.printf("[%lu ms] Motion DETECTED\n", millis());
    } else {
      Serial.printf("[%lu ms] Motion ENDED\n", millis());
    }
    lastPirValue = pirValue;
  }

  delay(100);  // Small delay to avoid flooding serial
}
