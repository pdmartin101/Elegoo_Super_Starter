#include <Arduino.h>

//www.elegoo.com
//2025.11.20


#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

int tempPin = 35;  // ADC input pin for NTC thermistor (ESP32 ADC1 channel)

// OLED display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1   // Reset pin not used with ESP32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Series resistor used in the voltage divider with the NTC thermistor
const float seriesResistor = 10000.0;  // 10kΩ

void setup() {
  Serial.begin(115200);

  // Initialize I2C with SDA=21 and SCL=22
  Wire.begin(21, 22);

  // Initialize the OLED display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1);
  }

  delay(2000);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(15, 0);
}

/**
 * Reads the thermistor value, applies noise filtering,
 * and calculates temperature using the Beta-parameter equation.
 *
 * Returns:
 *  - Temperature in °C (float)
 *  - -1.0 indicates an invalid or out-of-range reading
 */
float calculateTemperature() {

  // --- 1. Take multiple ADC samples for noise reduction ---
  int tempReading = 0;
  for (int i = 0; i < 5; i++) {
    int val = analogRead(tempPin);

    // Constrain ADC reading to avoid extreme outliers (0 or 4095)
    tempReading += constrain(val, 10, 4085);
    delay(3);
  }
  tempReading /= 5;  // Average value

  // --- 2. Convert ADC reading to thermistor resistance ---
  // ESP32 uses 12-bit ADC (0–4095 range)
  float ratio = (4095.0 / tempReading) - 1.0;
  float ntcResistance = seriesResistor * ratio;

  // Safety check: prevent log(0) or log of negative values
  if (ntcResistance <= 0) {
    return -1.0;  // Error flag
  }

  // --- 3. Apply the Steinhart-Hart thermistor equation ---
  double lnR = log(ntcResistance);
  double tempK = 1.0 / (
        0.001129148 +
        (0.000234125 + 0.0000000876741 * lnR * lnR) * lnR
      );

  float tempC = tempK - 273.15;  // Convert Kelvin → Celsius

  // Limit temperature to a realistic range (-10°C to 80°C)
  return constrain(tempC, -10.0, 80.0);
}

void loop() {
  float tempC = calculateTemperature();

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 3);
  display.print("Temp:");

  display.setCursor(30, 25);

  // Display error message if the reading was invalid
  if (tempC == -1.0) {
    display.print("Err");
  } else {
    display.print(tempC, 1);
    display.print("C");
  }

  display.display();
  delay(1000);
}
