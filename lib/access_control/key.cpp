#include "key.h"
#include <HardwareSerial.h>
#include "RC522_control.h"
// Keypad constant definitions
const long PASSWORD = 123456;  // Preset password
const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26,25,33,32};
Keypad customKeypad = Keypad( makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

// OLED object definition
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Global input buffer (only stores digits 0-9, filters letters/symbols)
String inputBuffer = "";

// Initialize keypad and OLED
void initKeypadAndOled() {
  // Initialize serial port
  Serial.begin(9600);
  
  // Initialize I2C (OLED)
  Wire.begin(21, 22); // ESP32 hardware I2C pins: SDA=21, SCL=22
  
  // Initialize OLED
  if(!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Default OLED I2C address: 0x3C
    Serial.println(F("OLED initialization failed!"));
    while(1); // Halt if initialization fails
  }
  oled.clearDisplay();
  oled.setTextColor(WHITE); // White font color
  oled.setTextSize(2);      // Font size (2x)
  oled.setCursor(0, 0);     // Initial cursor position
  // oled.println("num");
  // oled.display();
  
  Serial.println("Keypad + OLED initialized successfully");
}

// Password verification function
bool checkPassword() {
  // Convert string buffer to integer and compare with preset password
  long inputNum = inputBuffer.toInt();
  return (inputNum == PASSWORD);
}

// Display password verification result
void displayPwdResult(bool isMatch) {
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(0, 10);
  if (isMatch) {
    oled.println("Correct!"); // Password correct
    oled.display();
    unlockDoor();
  } else {
    oled.println("Error!");   // Password incorrect
    oled.display();
  }
  oled.display();
  delay(1500); // Display for 1.5 seconds
  inputBuffer = ""; // Clear input buffer
  displayInputOnOled(); // Return to input display
}

// Read keypad input
void getkeypad() {
  char customKey = customKeypad.getKey();
  
  if (customKey) {
    Serial.print("Key pressed: ");
    Serial.println(customKey);
    
    // Only add digits 0-9 to buffer
    if (customKey >= '0' && customKey <= '9') {
      // Limit input length to 6 digits (matching preset password 123456)
      if (inputBuffer.length() < 6) {
        inputBuffer += customKey; // Add to input buffer
        displayInputOnOled();     // Update OLED display
      }
    }
    // Press # key: Trigger password verification
    else if (customKey == '#') {
      if (inputBuffer.length() == 6) { // Verify only when 6 digits are entered
        bool isCorrect = checkPassword();
        displayPwdResult(isCorrect);
      } else { // Prompt if length is insufficient
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.println("Please enter 6 digits");
        oled.display();
        delay(1000);
        displayInputOnOled();
      }
    }
    // Press * key: Delete last digit (core modification)
    else if (customKey == '*') {
      if (inputBuffer.length() > 0) { // Delete only if there is input
        inputBuffer.remove(inputBuffer.length() - 1); // Remove last digit
        displayInputOnOled(); // Update OLED display
        Serial.println("Deleted last digit, current input: " + inputBuffer);
      }
    }
 
  }
}

// Display entered digits on OLED (no modification)
void displayInputOnOled() {
  oled.clearDisplay();       
  oled.setTextSize(2);      
  oled.setCursor(0, 0);  
  oled.println("PASSWORD");
  oled.setTextSize(2);       
  oled.setCursor(0, 30);     
  oled.println(inputBuffer); // Display input buffer
  oled.display(); // Refresh display
}