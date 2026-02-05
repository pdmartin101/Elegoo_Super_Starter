#include "esp32-hal-gpio.h"
#include "esp32-hal-spi.h"
#include "Arduino.h"
#include "SPI.h"
#include "RC522_control.h"  // Include custom header file

// ------------------- Global Variable Definitions (Correspond to declarations in .h) -------------------
// Authorized UID list (Store your actual authorized card UIDs here)
const byte authorizedUIDs[][4] = {
  {0x83, 0xE8, 0x8D, 0x04},
  {0x31, 0x1A, 0xCE, 0x05}
};

// Number of authorized cards (Calculated automatically, no manual modification needed)
const int AUTHORIZED_COUNT = sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0]);

// Global object instantiation (Correspond to declarations in .h)
MFRC522 mfrc522(SS_PIN, RST_PIN);  // RC522 module object (SS_PIN: Slave Select, RST_PIN: Reset)
MFRC522::MIFARE_Key key;           // MIFARE card key object (for authentication)
Servo doorServo;                   // Servo motor object (controls door lock/unlock)

// ------------------- Function Implementations (Correspond to declarations in .h) -------------------
/**
 * @brief Initialize RC522 RFID module
 * @details Initialize SPI bus (communication interface between ESP32 and RC522)
 *          and RC522 hardware to enter standby mode for card reading
 */
void initRC522() {
  SPI.begin();         // Initialize SPI bus
  pinMode(RST_PIN, INPUT_PULLUP);
  mfrc522.PCD_Init();  // Initialize RC522 module hardware

  Serial.println(F("RC522 RFID Module Initialized Successfully"));
}

/**
 * @brief Initialize door lock servo motor
 * @details Attach servo to specified pin, set initial state to locked,
 *          and wait for servo to stabilize in position
 */
void initServo() {
  doorServo.attach(SERVO_PIN);          // Attach servo to the specified GPIO pin (SERVO_PIN)
  doorServo.write(LOCKED_ANGLE);        // Initial state: Locked (set to locked angle)
  delay(500);                           // Wait 500ms for servo to reach target position stably
  Serial.println(F("Door Servo Initialized (Locked State)"));
}

/**
 * @brief Initialize MIFARE card authentication key
 * @details Set default factory key (0xFFFFFFFFFFFF) for most MIFARE IC cards
 *          (Key is required for RC522 to communicate with MIFARE cards securely)
 */
void initMifareKey() {
  // Default factory key for MIFARE cards: 0xFFFFFFFFFFFF
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;  // Assign each byte of the 6-byte key
  }
  Serial.println(F("MIFARE Default Key Initialized"));
}

/**
 * @brief Read and print the UID of the detected IC card via serial port
 * @details UID (Unique Identifier) is the unique 4-byte (or 7-byte) code of the IC card
 *          Print format is unified (pad with 0 for values < 0x10) for easy debugging
 */
void printCardUID() {
  Serial.print(F("\nCard Detected - UID: "));
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    // Unified format: pad with leading 0 if the byte value is less than 0x10
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(mfrc522.uid.uidByte[i], HEX);  // Print UID byte in hexadecimal
  }
  Serial.println();  // New line for readability
}

/**
 * @brief Verify if the current card's UID is in the authorized list
 * @param cardUid Pointer to the UID array of the current detected card
 * @return true: Authorized card (UID matches), false: Unauthorized card (no match)
 */
bool checkAuthorization(byte* cardUid) {
  // Traverse all authorized UIDs and compare byte by byte
  for (int i = 0; i < AUTHORIZED_COUNT; i++) {
    // memcmp: Efficiently compare 4 bytes of UID (return 0 if fully matched)
    if (memcmp(cardUid, authorizedUIDs[i], 4) == 0) {
      return true;  // Authorized: Matching UID found in the whitelist
    }
  }
  return false;  // Unauthorized: No matching UID
}

/**
 * @brief Execute door unlock → delay → auto-lock process
 * @details Control servo to rotate to unlocked angle, maintain for specified duration,
 *          then rotate back to locked angle to complete the access control cycle
 */
void unlockDoor() {
  Serial.println(F("Unlocking Door..."));
  doorServo.write(UNLOCKED_ANGLE);  // Rotate servo to unlocked angle
  delay(UNLOCK_DURATION);           // Maintain unlocked state for specified duration (e.g., 3000ms)
  Serial.println(F("Auto-Locking Door..."));
  doorServo.write(LOCKED_ANGLE);    // Rotate servo back to locked angle
}