# WiFiManager for ESP32

WiFiManager is a library that simplifies WiFi credential management on ESP32. Instead of hardcoding credentials, it provides a web-based configuration portal.

## How It Works

### First Boot (or no saved credentials)

1. ESP32 starts in Access Point (AP) mode
2. Creates a network like "ESP32-Setup"
3. You connect to it with your phone or laptop
4. A captive portal serves a web page (usually at 192.168.4.1)
5. The page shows available WiFi networks
6. You select your network and enter the password
7. Credentials are saved to NVS (non-volatile storage in flash)
8. ESP32 reboots and connects to your network

### Every Subsequent Boot

1. ESP32 reads saved credentials from NVS
2. Attempts to connect to the saved network
3. If successful, proceeds normally - no AP, no web page
4. If connection fails (wrong password, network unavailable, out of range), falls back to AP mode for reconfiguration

### Resetting Credentials

Common approaches:
- Hold a button during boot
- Call `wifiManager.resetSettings()` in code
- This clears NVS and forces AP mode again

## Usage in Code

```cpp
#include <WiFiManager.h>

void setup() {
    WiFiManager wifiManager;

    // Automatically connects using saved credentials
    // or starts config portal if none saved
    wifiManager.autoConnect("ESP32-Setup");

    // If we get here, WiFi is connected
    Serial.println("Connected to WiFi!");
    Serial.println(WiFi.localIP());
}
```

## PlatformIO Dependency

Add to `platformio.ini`:

```ini
lib_deps = https://github.com/tzapu/WiFiManager.git
```

## Features

- Automatic network scanning
- Captive portal (auto-redirects to config page)
- Custom parameters (add your own config fields)
- Timeout options (don't block forever in AP mode)
- Callbacks for connection events
- Works on ESP32 and ESP8266

## When to Use

- Devices that may connect to different networks
- Products shipped to end users
- Avoiding credential recompilation
- Projects shared publicly (no hardcoded passwords)

## Scalextric Communications Architecture

### Overview

```
┌─────────┐  ESP-NOW   ┌─────────┐  WebSocket   ┌─────────┐
│  Child  │──────────→│ Parent  │─────────────→│  C# App │
│  Node 0 │  broadcast │  Node   │  WiFi/mDNS   │  (PC)   │
└─────────┘           │         │              └─────────┘
┌─────────┐  ESP-NOW  │         │
│  Child  │──────────→│         │
│  Node 1 │  broadcast │         │
└─────────┘           └─────────┘
```

### Data Flow

1. **Child nodes** detect cars via phototransistors (4 sensors each)
2. Children broadcast `CarEvent` structs via **ESP-NOW** (peer-to-peer, no WiFi needed)
3. **Parent node** receives ESP-NOW events from all children
4. Parent connects to local WiFi (credentials via WiFiManager captive portal)
5. Parent runs a **WebSocket server** + **mDNS** (`scalextric.local`)
6. **C# app** discovers parent via mDNS and connects to WebSocket
7. Car events forwarded to C# app in `NODE:SENSOR:CAR:FREQ:TIME` format

### Message Format

```
NODE:SENSOR:CAR:FREQ:TIME
e.g., 0:2:3:3704:12345 = Node 0, Sensor 2, Car 3, 3704 Hz, timestamp
```

- Sensor naming/mapping is done in the C# app, not on the ESP32s
- Comments/metadata lines are prefixed with `#`

### ESP-NOW Layer (Child → Parent)

- **Protocol**: ESP-NOW broadcast (no pairing required)
- **Latency**: ~1-2ms
- **No WiFi infrastructure needed** - works peer-to-peer
- **Zero-config**: children just broadcast, parent listens
- Each child only needs its `NODE_ID` set (0, 1, 2, etc.)

```cpp
struct CarEvent {
  uint8_t  nodeId;     // Which ESP32 (0, 1, 2, ...)
  uint8_t  sensorId;   // Which sensor on that node (0-3)
  uint8_t  carNumber;  // Car 1-6
  uint16_t frequency;  // Detected frequency in Hz
  uint32_t timestamp;  // millis() on the child
};
```

### WiFi/WebSocket Layer (Parent → C# App)

- **WiFi provisioning**: WiFiManager captive portal on first boot
  - AP name: `Scalextric-Setup`
  - Credentials saved to NVS (survives reboots/reflashing)
  - Button/GPIO to reset credentials if needed
- **Discovery**: mDNS at `scalextric.local`
- **WebSocket server**: Parent broadcasts car events to all connected clients
- **Dual mode**: ESP32 runs ESP-NOW and WiFi simultaneously

### Components

| File | Role | Communication |
|------|------|---------------|
| `scalextric_child.cpp` | Detects cars, broadcasts events | ESP-NOW broadcast out |
| `scalextric_parent.cpp` | Receives events, serves WebSocket | ESP-NOW in, WebSocket out |
| C# App (separate project) | UI, lap timing, sensor naming | WebSocket client |

### Hardware per Node

- ESP32 dev board
- Up to 4 phototransistors (TEFT4300) per node
- Each sensor needs its own 4.7kΩ pull-up resistor (cannot be shared)
- 22nF filter capacitor per sensor
- Child nodes: no WiFi antenna concerns (ESP-NOW only)
- Parent node: needs decent WiFi reception

## WiFiManager Library

### PlatformIO Dependency

Add to `platformio.ini`:

```ini
lib_deps = https://github.com/tzapu/WiFiManager.git
```

### Features

- Automatic network scanning
- Captive portal (auto-redirects to config page)
- Custom parameters (add your own config fields)
- Timeout options (don't block forever in AP mode)
- Callbacks for connection events
- Works on ESP32 and ESP8266
