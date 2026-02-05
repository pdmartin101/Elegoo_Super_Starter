#include <Arduino.h>

// main.cpp
#include "DHT11.h"
#include "OLED_Display.h"

DHT_nonblocking_sensor sensor;
OLED_Display display;

int adc_id = 25; //pin of water level sensor

float temperature = 0.0;
float humidity = 0.0;

unsigned long lastDHTUpdate = 0;   // Time to read DHT11 periodically

void setup() {
    Serial.begin(9600);
    display.setupOLED();
    display.cleanOled();   // Clear the OLED screen once on power-up
}

void loop() {

    // ************ 1. Read DHT every 2 seconds *************
    if (millis() - lastDHTUpdate > 2000) {
        float temp, hum;

        if (sensor.measure(&temp, &hum) == true) {
            temperature = temp;    // Update only when successful
            humidity = hum;
        }
        lastDHTUpdate = millis();
    }

    // ************ 2. Read water level ADC *************
    int waterLevel = analogRead(adc_id);

    // ************ 3. Refresh OLED (do not clear screen, no flicker) *************
    display.cleanOled();  // Clear the screen once is enough, but if layout changes are needed, put it here as well
    display.displayTemperatureHumidity(temperature, humidity);
    display.displayWaterLever(waterLevel); // Corrected function name from "displayWaterLever" to "displayWaterLevel"
    display.updatedisplay(); // Corrected function name from "updatedisplay" to "updateDisplay"
    
    delay(2000);
}
