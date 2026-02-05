#include <Arduino.h>

//www.elegoo.com
//2018.10.25
// Rewritten to use Adafruit DHT library

#include <DHT.h>

#define DHT_SENSOR_PIN 25
#define DHT_TYPE DHT11

DHT dht(DHT_SENSOR_PIN, DHT_TYPE);

void setup() {
  Serial.begin(9600);
  dht.begin();
  Serial.println("DHT11 Temperature & Humidity Sensor");
}

void loop() {
  delay(3000);  // Wait between measurements

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.print("T = ");
  Serial.print(temperature, 1);
  Serial.print(" deg. C, H = ");
  Serial.print(humidity, 1);
  Serial.println("%");
}
