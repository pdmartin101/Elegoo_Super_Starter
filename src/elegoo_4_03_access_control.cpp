#include <Arduino.h>

#include "RC522_control.h"  
#include "key.h"

int buzzer = 5;//the pin of the active buzzer

#define RFID_CHECK_INTERVAL 100

unsigned long lastRFIDCheck = 0;  // Record last RC522 check time (for frequency limiting)

void setup() {
  Serial.begin(9600);
  while (!Serial);  // Wait for serial port to initialize (for Leonardo/Mega/ESP32)
  pinMode(buzzer,OUTPUT);//initialize the buzzer pin as an output
  Serial.println(F("=== IR + RC522 Dual-Control Access Control System (Optimized Version) ==="));

  // Initialize all modules
  initKeypadAndOled();
 
  initRC522();         // Initialize RC522 RFID module (SPI communication)
  initServo();         // Initialize door lock servo motor
  initMifareKey();     // Initialize MIFARE card authentication key
}



// --------------------- RC522 Processing Function (Frequency-Limited) ---------------
/**
 * @brief Handle RC522 RFID card detection, UID reading and authorization verification
 * @details Check for new cards at limited frequency, verify UID authorization, trigger unlock if valid
 */
void handleRFID() {
  // Detect if a new card is present and read its serial number (UID)
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

    printCardUID();  // Print detected card UID via serial port

    // Only support 4-byte UID (standard MIFARE IC card)
    if (mfrc522.uid.size == 4) {
      // Verify if the card UID is in the authorized list
      if (checkAuthorization(mfrc522.uid.uidByte)) {
        digitalWrite(buzzer,HIGH);
        delay(20);//wait for 2ms
        digitalWrite(buzzer,LOW);
        delay(20);//wait for 2ms
        Serial.println(F("RFID Authorization Passed - Unlocking Door!"));
        unlockDoor();  // Trigger door unlock if authorized
        

      } else {
        Serial.println(F("RFID Unauthorized Card!"));
      }
    } else {
      Serial.println(F("Unsupported UID Length (Only 4-byte UID Supported)"));
    }

    // Halt the card and stop cryptographic communication (required for RC522 normal operation)
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
}

void loop() {

  // -------------  IR Priority Processing (Ensure Sensitivity) ------------
 
  getkeypad();
  // -------------  Check RC522 Every 100ms --------------
  unsigned long now = millis();
  if (now - lastRFIDCheck >= RFID_CHECK_INTERVAL) {
    lastRFIDCheck = now;
    handleRFID();     // Limit SPI communication frequency to avoid interfering IR
  }
}