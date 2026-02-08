#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Scalextric Car Detector - ESP-NOW Parent Node
// Receives car detection events from child nodes via ESP-NOW
// Outputs to Serial (can be extended to WiFi/WebSocket)

// ESP-NOW message structure (must match child)
struct CarEvent {
  uint8_t nodeId;
  char sensor[12];
  uint8_t carNumber;
  uint16_t frequency;
  uint32_t timestamp;
};

// Track registered children
const int MAX_CHILDREN = 10;
uint8_t childMacs[MAX_CHILDREN][6];
int childCount = 0;

void onDataReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(CarEvent)) {
    Serial.printf("Invalid packet size: %d\n", len);
    return;
  }

  CarEvent event;
  memcpy(&event, data, sizeof(event));

  // Format: NODE:SENSOR:CAR:FREQ:TIME
  Serial.printf("N%d:%s:Car%d:%dHz:%lu\n",
                event.nodeId,
                event.sensor,
                event.carNumber,
                event.frequency,
                event.timestamp);

  // Check if this is a new child
  bool known = false;
  for (int i = 0; i < childCount; i++) {
    if (memcmp(childMacs[i], info->src_addr, 6) == 0) {
      known = true;
      break;
    }
  }

  if (!known && childCount < MAX_CHILDREN) {
    memcpy(childMacs[childCount], info->src_addr, 6);
    childCount++;
    Serial.printf("New child registered: %02X:%02X:%02X:%02X:%02X:%02X (Node %d)\n",
                  info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5],
                  event.nodeId);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nScalextric Parent Node");
  Serial.println("======================");

  // Init WiFi in station mode for ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("Parent MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("(Use this MAC in child nodes)");
  Serial.println();

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed!");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(onDataReceived);

  Serial.println("Listening for child nodes...");
  Serial.println("Output format: NODE:SENSOR:CAR:FREQ:TIME\n");
}

void loop() {
  // Parent just listens - all work done in callback
  delay(10);
}
