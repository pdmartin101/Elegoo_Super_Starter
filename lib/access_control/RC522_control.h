#ifndef RC522_CONTROL_H
#define RC522_CONTROL_H

#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>

/*
 * Typical pin layout used:
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       ESP32     Arduino    Arduino          Arduino
 *             Reader/PCD   Uno                     Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             34         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            2        D10        10               10
 * SPI MOSI    MOSI         11 / ICSP-4   23        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   19        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   18        D13        ICSP-3           15
*/

// ------------------- Pin Macro Definitions -------------------
#define RST_PIN       34     // RC522 reset pin
#define SS_PIN        2      // RC522 chip select pin (SPI SS)
#define SERVO_PIN     4      // Servo motor signal pin (PWM compatible)

// ------------------- Servo Configuration Macros -------------------
#define LOCKED_ANGLE   0     // Door locked angle (degrees)
#define UNLOCKED_ANGLE 30    // Door unlocked angle (degrees)
#define UNLOCK_DURATION 3000 // Unlock hold duration (milliseconds, 3 seconds)

// ------------------- Authorized UID Configuration -------------------
extern const byte authorizedUIDs[][4];  // External declaration: List of authorized card UIDs (defined in .cpp)
extern const int AUTHORIZED_COUNT;      // External declaration: Number of authorized cards (defined in .cpp)

// ------------------- Global Object Declarations -------------------
extern MFRC522 mfrc522;          // External declaration: RC522 instance (instantiated in .cpp)
extern MFRC522::MIFARE_Key key;  // External declaration: MIFARE key instance
extern Servo doorServo;          // External declaration: Servo motor instance

// ------------------- Function Declarations -------------------
void initRC522();                  // Initialize RC522 RFID reader
void initServo();                  // Initialize servo motor
void initMifareKey();              // Initialize MIFARE encryption key
void printCardUID();               // Print detected card UID to serial port
bool checkAuthorization(byte* cardUid); // Verify if card UID is in authorized list
void unlockDoor();                 // Door unlock logic (shared by infrared and RFID)

#endif