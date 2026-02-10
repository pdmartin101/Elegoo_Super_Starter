# Scalextric Digital Car Detector

Detects and identifies Scalextric Digital cars (1-6) by their unique IR pulse frequencies using a distributed ESP32 network.

## Architecture

```
  [Child 0]  [Child 1]  [Child 2]  ...
      |           |           |
      +--- ESP-NOW (wireless) ---+
                  |
             [Parent]
              /     \
         WiFi        Local sensors
          |
    WebSocket :81
          |
     [C# Client]
```

- **Parent** (`scalextric_parent`): Connects to WiFi, runs WebSocket server, detects cars on its own sensors, receives events from children via ESP-NOW, and forwards everything to connected clients.
- **Child** (`scalextric_child`): Zero-config. Detects cars on its sensors and broadcasts events to the parent via ESP-NOW. Only setting: `NODE_ID` (0, 1, 2, etc.).
- **C# Client** (`ScalextricClient`): Connects to the parent via WebSocket (auto-discovered via mDNS) and displays all events.

## Car Frequencies

| Car | Frequency |
|-----|-----------|
| 1   | 5500 Hz   |
| 2   | 4400 Hz   |
| 3   | 3700 Hz   |
| 4   | 3100 Hz   |
| 5   | 2800 Hz   |
| 6   | 2400 Hz   |

## Sensor Circuit

### Phototransistor Detector (Common Emitter Configuration)

```
        3.3V
          |
         [4.7kΩ]
          |
          +----------- GPIO (sensor pin)
          |
    +-----+-----+
    |           |
  [22nF]    Collector (short leg)
    |        TEFT4300
   GND      Emitter (long leg)
                |
               GND
```

**Component values:**
- Pull-up resistor: 4.7kΩ (provides bias current and creates voltage divider)
- Filter capacitor: 22nF - filters high-frequency noise on collector side
- Phototransistor: TEFT4300 (or similar NPN phototransistor)

**How it works:**
1. When no IR light: phototransistor is OFF, GPIO reads HIGH (3.3V through resistor)
2. When IR light detected: phototransistor conducts, pulls GPIO LOW
3. Pulsed IR from car creates falling edges that trigger interrupts
4. Frequency of pulses identifies which car (1-6)

### Sensor GPIO Pins

Each node (parent or child) supports up to 4 sensors:

| Sensor | GPIO |
|--------|------|
| 0      | 4    |
| 1      | 5    |
| 2      | 16   |
| 3      | 17   |

Unused sensor pins use `INPUT_PULLUP` to prevent false triggers - no external resistors needed on unconnected pins.

## WebSocket Message Format

The parent ESP32 runs a WebSocket server on port 81. It broadcasts car detection events as plain text messages to all connected clients.

### Message format

```
NODE:SENSOR:CAR:FREQ:TIME
```

| Field  | Type       | Description |
|--------|------------|-------------|
| NODE   | 0-254      | Child node ID (set per child ESP32) |
|        | 255        | Parent node (local sensors) |
| SENSOR | 0-3        | Sensor index on that node (GPIO 4, 5, 16, 17) |
| CAR    | 1-6        | Detected car number |
| FREQ   | int        | Measured IR pulse frequency in Hz |
| TIME   | HH.MM.SS.mmm | NTP time (UTC), falls back to millis if NTP unavailable |

### Examples

```
255:0:3:3704:14.23.05.123    Parent, Sensor 0, Car 3, 3704 Hz, 14:23:05.123 UTC
0:1:1:5512:14.23.06.450      Child 0, Sensor 1, Car 1, 5512 Hz, 14:23:06.450 UTC
2:0:5:2803:14.23.07.891      Child 2, Sensor 0, Car 5, 2803 Hz, 14:23:07.891 UTC
```

### Connection

- **Discovery:** mDNS service `_ws._tcp` advertised as `scalextric.local`
- **URL:** `ws://<parent-ip>:81`
- **Protocol:** Plain WebSocket text frames, one event per message
- **Lines starting with `#`** are comment/status messages (not car events)

## ESP-NOW Channel Discovery

Child nodes automatically find the parent's WiFi channel without needing WiFi credentials:

1. Child scans channels 1-13, sending a 2-byte probe packet (`0xAA` + node ID) on each
2. Parent detects the probe and responds with a unicast reply (`0xBB` + parent ID)
3. Child locks to the channel where it received the response
4. If no parent found after 3 rounds, child defaults to channel 1

## PlatformIO

### Build & Upload

**Parent:**
```
pio run -e scalextric_parent
pio run -e scalextric_parent -t upload
```

**Child:**
```
pio run -e scalextric_child
pio run -e scalextric_child -t upload
```

### Configuration

- **Parent**: Set WiFi credentials in `src/wifi_credentials.h`
- **Child**: Set `NODE_ID` on line 18 of `src/scalextric_child.cpp` (0, 1, 2, etc.)
- **Max children**: 10 (set by `MAX_CHILDREN` in parent)

## OLED Display

Both parent and child support a 128x64 SSD1306 OLED (I2C: SDA=21, SCL=22). The display shows:

- Header line (yellow band): node info, channel, TX/RX stats
- Large car number (cyan zone)
- Frequency and sensor ID
- Recent event log

The display is optional - nodes work without it.
