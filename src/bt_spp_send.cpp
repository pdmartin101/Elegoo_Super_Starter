#include <Arduino.h>
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32 Bluetooth SPP Sender");

  // Initialize Bluetooth with device name
  if (!SerialBT.begin("ESP32-BT-SPP")) {
    Serial.println("Bluetooth init failed!");
    while (1);
  }

  Serial.println("Bluetooth started");
  Serial.println("Device name: ESP32-BT-SPP");
  Serial.println("Pair with this device, then connect via COM port");
}

void loop() {
  static int counter = 0;
  static unsigned long lastSend = 0;

  // Check if a client is connected
  if (SerialBT.connected()) {
    // Send data every 2 seconds
    if (millis() - lastSend > 2000) {
      String message = "Hello from ESP32! Count: " + String(counter++);

      SerialBT.println(message);
      Serial.println("Sent: " + message);

      lastSend = millis();
    }

    // Echo any received data
    while (SerialBT.available()) {
      String received = SerialBT.readStringUntil('\n');
      Serial.println("Received: " + received);
      SerialBT.println("ACK: " + received);
    }
  }

  delay(100);
}
