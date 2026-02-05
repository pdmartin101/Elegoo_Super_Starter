#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);  // SDA=21, SCL=22

  Serial.println("\nI2C Scanner");
  Serial.println("Scanning...");
}

void loop() {
  byte error, address;
  int devices = 0;

  for(address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      devices++;
    }
  }

  if (devices == 0)
    Serial.println("No I2C devices found!\n");
  else
    Serial.println("Scan complete.\n");

  delay(5000);  // Scan every 5 seconds
}
