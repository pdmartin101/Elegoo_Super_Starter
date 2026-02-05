// OLED_Display.h
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET -1

class OLED_Display {
public:
    OLED_Display();
    void setupOLED();
    void displayWaterLever(int value);
    void displayTemperatureHumidity(float temperature, float humidity);
    void cleanOled();
    void updatedisplay();
private:
    Adafruit_SSD1306 oled;
};

#endif // OLED_DISPLAY_H