#ifndef SCALEXTRIC_PROTOCOL_H
#define SCALEXTRIC_PROTOCOL_H

#include <stdint.h>

// Scalextric Car Detector - Shared Protocol & Constants
// Used by both parent and child nodes

// ========== ESP-NOW MESSAGE STRUCTURES ==========

struct __attribute__((packed)) CarEvent {
  uint8_t nodeId;
  uint8_t sensorId;
  uint8_t carNumber;
  uint16_t frequency;
  uint32_t timestamp;
};

struct __attribute__((packed)) ProbeMsg {
  uint8_t magic;    // PROBE_REQUEST_MAGIC or PROBE_RESPONSE_MAGIC
  uint8_t nodeId;
  uint8_t channel;  // Parent's WiFi channel (set in response, 0 in request)
};

// ========== PROTOCOL CONSTANTS ==========

const uint8_t PROBE_REQUEST_MAGIC = 0xAA;
const uint8_t PROBE_RESPONSE_MAGIC = 0xBB;
const uint8_t PARENT_NODE_ID = 255;

// ========== SENSOR CONFIGURATION ==========

const int SENSOR_PINS[] = {4, 5, 18, 19};
const int NUM_SENSORS = 4;

// ========== CAR DETECTION PARAMETERS ==========

const int CAR_FREQUENCIES[] = {5500, 4400, 3700, 3100, 2800, 2400};
const float FREQUENCY_TOLERANCE_PCT = 0.08;  // 8% of target frequency
const unsigned long MIN_VALID_INTERVAL = 150;
const unsigned long MAX_VALID_INTERVAL = 450;
const int MIN_PULSES_FOR_ID = 6;
const unsigned long DETECTION_TIMEOUT = 50000;
const int CONFIRM_COUNT = 3;
const int HISTORY_SIZE = 10;

#endif
