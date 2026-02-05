// OLED_Display.cpp
#include "OLED_Display.h"
#include <Arduino.h>



OLED_Display::OLED_Display() : oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET) {}

void OLED_Display::setupOLED() {
    Serial.begin(115200);
    Wire.begin(21, 22); // SDA=21, SCL=22
    if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    delay(2000);
    oled.clearDisplay();
}

void OLED_Display::cleanOled(){

  oled.clearDisplay();
}

void OLED_Display::displayTemperatureHumidity(float temperature, float humidity) {
    
    oled.setTextSize(1);
    oled.setTextColor(WHITE);
    oled.setCursor(0, 0);
    oled.print("Temp: ");
    oled.print(temperature);
    oled.print(" C");
    oled.setCursor(0, 20);
    oled.print("Humidity: ");
    oled.print(humidity);
    oled.print(" %");
    //display.display(); 
}


void OLED_Display::displayWaterLever(int value){
    
    oled.setTextSize(1);
    oled.setTextColor(WHITE);
    oled.setCursor(0, 40);
    oled.print("rainfall:");
    oled.print(value);
    //display.display();

}

void OLED_Display::updatedisplay(){

  oled.display();
}