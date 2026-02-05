#include <Arduino.h>

// LED GPIO pins
#define LED_RED   2   // Built-in LED on most ESP32 DevKits
#define LED_GREEN 4
#define LED_BLUE  5

// PWM configuration
#define PWM_FREQ     5000
#define PWM_RES      8     // 8-bit resolution (0-255)
#define PWM_CH_RED   0
#define PWM_CH_GREEN 1
#define PWM_CH_BLUE  2

#define FADE_DELAY   25   // ms between brightness steps

void setup() {
    Serial.begin(115200);

    // Configure PWM channels
    ledcSetup(PWM_CH_RED, PWM_FREQ, PWM_RES);
    ledcSetup(PWM_CH_GREEN, PWM_FREQ, PWM_RES);
    ledcSetup(PWM_CH_BLUE, PWM_FREQ, PWM_RES);

    // Attach pins to PWM channels
    ledcAttachPin(LED_RED, PWM_CH_RED);
    ledcAttachPin(LED_GREEN, PWM_CH_GREEN);
    ledcAttachPin(LED_BLUE, PWM_CH_BLUE);

    Serial.println();
    Serial.println("ESP32 RGB LED Fade Test Started");
}

void fadeLed(uint8_t channel, const char* name) {
    Serial.printf("Fading %s: 0 -> 255\n", name);

    for (int brightness = 0; brightness <= 255; brightness++) {
        ledcWrite(channel, brightness);
        delay(FADE_DELAY);
    }

    // Turn off after fade completes
    ledcWrite(channel, 0);
    Serial.printf("%s complete\n", name);
}

void loop() {
    fadeLed(PWM_CH_RED, "RED");
    delay(500);

    fadeLed(PWM_CH_GREEN, "GREEN");
    delay(500);

    fadeLed(PWM_CH_BLUE, "BLUE");
    delay(500);
}
