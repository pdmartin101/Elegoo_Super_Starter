#include <Arduino.h>

#define LED_RED   2
#define LED_GREEN 4
#define LED_BLUE  5

void setup() {
    Serial.begin(115200);

    // Initialize all LED pins
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);

    // Turn off green and blue
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_BLUE, LOW);

    Serial.println();
    Serial.println("ESP32 Blink Started");
}

void loop() {
    digitalWrite(LED_RED, HIGH);
    Serial.println("LED ON");
    delay(1000);

    digitalWrite(LED_RED, LOW);
    Serial.println("LED OFF");
    delay(1000);
}
