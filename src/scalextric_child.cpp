#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "scalextric_protocol.h"
#include "car_detection.h"

// Scalextric Car Detector - ESP-NOW Child Node
// Detects cars and broadcasts events via ESP-NOW (zero-config)
// Displays last detection on 128x64 OLED (I2C: SDA=21, SCL=22)
//
// Output format: NODE:SENSOR:CAR:FREQ:TIME
// e.g., 0:2:3:3704:12345 = Node 0, Sensor 2, Car 3, 3704 Hz, timestamp
//
// Only config needed: set NODE_ID (0, 1, 2, etc.) for each child

// ========== CONFIGURATION ==========
const uint8_t NODE_ID = 0;  // Change this for each child (0, 1, 2, etc.)

// Broadcast address - no parent MAC needed
const uint8_t BROADCAST[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// OLED display
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool hasDisplay = false;

// Display state
volatile bool displayNeedsUpdate = false;
uint8_t lastDetectedSensor = 0;
uint8_t lastDetectedCar = 0;
uint16_t lastDetectedFreq = 0;
int sendOkCount = 0;
int sendFailCount = 0;

// Recent events log
const int LOG_SIZE = 3;
struct LogEntry {
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
};
LogEntry eventLog[LOG_SIZE];
int logCount = 0;

volatile bool probeResponseReceived = false;
uint8_t foundChannel = 0;

esp_now_peer_info_t peerInfo;
bool espNowAvailable = false;

// Parent discovery retry
unsigned long lastScanAttempt = 0;
const unsigned long SCAN_RETRY_INTERVAL = 30000;  // 30 seconds

void sendCarEvent(uint8_t sensorId, int car, float freq) {
  CarEvent event;
  event.nodeId = NODE_ID;
  event.sensorId = sensorId;
  event.carNumber = car;
  event.frequency = (uint16_t)freq;
  event.timestamp = millis();

  if (espNowAvailable) {
    esp_err_t result = esp_now_send(BROADCAST, (uint8_t*)&event, sizeof(event));
    if (result == ESP_OK) {
      sendOkCount++;
      Serial.printf("SENT: %d:%d:%d:%d:%lu\n", NODE_ID, sensorId, car, (int)freq, event.timestamp);
    } else {
      sendFailCount++;
      Serial.printf("SEND FAILED (err %d): %d:%d\n", result, NODE_ID, sensorId);
    }
  } else {
    Serial.printf("LOCAL: %d:%d:%d:%d:%lu\n", NODE_ID, sensorId, car, (int)freq, event.timestamp);
  }

  // Update display state
  lastDetectedSensor = sensorId;
  lastDetectedCar = car;
  lastDetectedFreq = (uint16_t)freq;
  for (int i = LOG_SIZE - 1; i > 0; i--) {
    eventLog[i] = eventLog[i - 1];
  }
  eventLog[0] = {sensorId, (uint8_t)car, (uint16_t)freq};
  if (logCount < LOG_SIZE) logCount++;
  displayNeedsUpdate = true;
}

void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  // Optional: handle send confirmation
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(ProbeMsg) && data[0] == PROBE_RESPONSE_MAGIC) {
    ProbeMsg response;
    memcpy(&response, data, sizeof(response));
    foundChannel = response.channel;
    probeResponseReceived = true;
  }
}

bool findParentChannel() {
  ProbeMsg probe;
  probe.magic = PROBE_REQUEST_MAGIC;
  probe.nodeId = NODE_ID;
  probe.channel = 0;

  for (int round = 0; round < 3; round++) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

      if (hasDisplay) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.printf("Child Node %d", NODE_ID);
        display.setCursor(0, 16);
        display.printf("Scanning Ch: %d", ch);
        display.setCursor(0, 28);
        display.printf("Round %d/3", round + 1);
        display.display();
      }

      probeResponseReceived = false;
      esp_now_send(BROADCAST, (uint8_t*)&probe, sizeof(probe));

      unsigned long start = millis();
      while (millis() - start < 150) {
        if (probeResponseReceived) {
          // foundChannel already set by onDataReceived from parent's response
          esp_wifi_set_channel(foundChannel, WIFI_SECOND_CHAN_NONE);
          Serial.printf("Parent found on channel %d\n", foundChannel);
          return true;
        }
        delay(1);
      }
    }
  }
  return false;
}

void updateDisplay() {
  display.clearDisplay();

  // Header: node + channel + TX stats (single line, matches parent layout)
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("N%d Ch:%d ok:%d f:%d", NODE_ID, foundChannel, sendOkCount, sendFailCount);

  // Big car number (size 3 = 18x24px)
  display.setTextSize(3);
  display.setCursor(0, 12);
  display.printf("Car %d", lastDetectedCar);

  // Frequency and sensor (size 1)
  display.setTextSize(1);
  display.setCursor(0, 44);
  display.printf("%d Hz  Sensor %d", lastDetectedFreq, lastDetectedSensor);

  // Recent events log
  display.setCursor(0, 56);
  for (int i = 0; i < logCount && i < LOG_SIZE; i++) {
    display.printf("S%d=C%d ", eventLog[i].sensorId, eventLog[i].carNumber);
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nScalextric Child Node %d\n", NODE_ID);
  Serial.println("========================");

  // Init OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    hasDisplay = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.printf("Child Node %d", NODE_ID);
    display.setCursor(0, 16);
    display.println("Waiting for cars...");
    display.display();
    Serial.println("OLED: OK");
  } else {
    Serial.println("OLED: not found (continuing without display)");
  }

  // Init WiFi in station mode for ESP-NOW (no WiFi connection needed)
  WiFi.mode(WIFI_STA);
  Serial.print("MAC Address: ");
  Serial.println(WiFi.macAddress());

  // Init ESP-NOW
  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataReceived);

    // Register broadcast peer
    memcpy(peerInfo.peer_addr, BROADCAST, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      espNowAvailable = true;
    } else {
      Serial.println("Failed to add broadcast peer");
    }
  } else {
    Serial.println("ESP-NOW init failed! Local sensors only.");
  }

  // Find parent by probing all channels
  if (espNowAvailable) {
    Serial.println("Scanning for parent...");
    if (findParentChannel()) {
      Serial.printf("Locked to channel %d\n", foundChannel);
      if (hasDisplay) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("Child Node %d", NODE_ID);
        display.setCursor(0, 16);
        display.printf("Parent Ch: %d", foundChannel);
        display.setCursor(0, 28);
        display.println("Waiting for cars...");
        display.display();
      }
    } else {
      Serial.println("Parent not found! Staying on channel 1");
      esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
      if (hasDisplay) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.printf("Child Node %d", NODE_ID);
        display.setCursor(0, 16);
        display.println("Parent NOT FOUND");
        display.setCursor(0, 28);
        display.println("Using Ch 1");
        display.display();
      }
    }
  }

  // Setup sensors
  Serial.println("\nSensors:");
  initSensors();
  for (int i = 0; i < NUM_SENSORS; i++) {
    Serial.printf("  %d:%d - GPIO %d\n", NODE_ID, i, SENSOR_PINS[i]);
  }

  Serial.println("\nFormat: NODE:SENSOR:CAR:FREQ:TIME");
  Serial.println("Waiting for cars...\n");
}

void loop() {
  // Retry parent discovery periodically if not found
  if (espNowAvailable && foundChannel == 0 && millis() - lastScanAttempt > SCAN_RETRY_INTERVAL) {
    lastScanAttempt = millis();
    Serial.println("Retrying parent scan...");
    if (findParentChannel()) {
      Serial.printf("Parent found on channel %d\n", foundChannel);
    }
  }

  for (int i = 0; i < NUM_SENSORS; i++) {
    processSensor(sensors[i], sendCarEvent);
  }
  if (hasDisplay && displayNeedsUpdate) {
    displayNeedsUpdate = false;
    updateDisplay();
  }
  delay(1);
}
