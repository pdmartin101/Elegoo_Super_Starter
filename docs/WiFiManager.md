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

## Current Project Approach

This project currently uses a simpler approach with `wifi_credentials.h`:
- Credentials are in a header file that's gitignored
- Requires recompilation to change networks
- Good for development where you control the environment

WiFiManager would be the next step for a more portable solution.
