# Scalextric Digital Car Detector

Detects and identifies Scalextric Digital cars (1-6) by their unique IR pulse frequencies.

## Car Frequencies

| Car | Frequency |
|-----|-----------|
| 1   | 5500 Hz   |
| 2   | 4400 Hz   |
| 3   | 3700 Hz   |
| 4   | 3100 Hz   |
| 5   | 2800 Hz   |
| 6   | 2400 Hz   |

## Circuit Diagram

### Phototransistor Detector (Common Emitter Configuration)

```
        3.3V
          |
         [4.7kΩ]
          |
          +----------- GPIO 4 (IR_SENSOR_PIN)
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

### Test LED Circuit (for auto-test mode)

```
        GPIO 5 (TEST_LED_PIN)
          |
         [220Ω]
          |
          +-----+
          | LED |  Anode = Long leg (to resistor)
          +-----+  Cathode = Short leg (to GND)
          |
         GND
```

**For testing:** Point the IR LED at the phototransistor. The ESP32 will pulse the LED at each car's frequency to simulate cars passing.

## Wiring Summary

| Component | Pin | ESP32 GPIO |
|-----------|-----|------------|
| Phototransistor Collector | Short leg | GPIO 4 (via 4.7kΩ to 3.3V) |
| Phototransistor Emitter | Long leg | GND |
| Filter Capacitor 22nF | - | Collector/GPIO 4 junction to GND |
| Test IR LED Anode | Long leg | GPIO 5 (via 220Ω) |
| Test IR LED Cathode | Short leg | GND |

## PlatformIO Environment

Build with: `pio run -e scalextric_car_detect`

Upload with: `pio run -e scalextric_car_detect -t upload`

## Usage

1. Set `TEST_MODE = true` to test with IR LED (auto-cycles through all 6 cars)
2. Set `DEBUG_MODE = true` for real-time frequency output
3. Set both to `false` for production use with real Scalextric cars

## Expected Output

```
Scalextric Car Detector
=======================
Car frequencies:
  Car 1: 5500 Hz
  Car 2: 4400 Hz
  Car 3: 3700 Hz
  Car 4: 3100 Hz
  Car 5: 2800 Hz
  Car 6: 2400 Hz

Listening on GPIO 4...

*** DEBUG MODE ENABLED - showing real-time frequency ***

*** AUTO-TEST MODE ENABLED ***
Test LED on GPIO 5
Wiring: GPIO 5 --[220R]-- IR LED --> GND
Point LED at phototransistor
Will auto-cycle through all 6 cars every 2 seconds

--- Auto-test: Simulating Car 1 (5500 Hz) ---
IR pulses detected...
  [DEBUG] pulses: 5, interval: 181 us, freq: 5525 Hz
[2100 ms] CAR 1 detected (freq: 5512 Hz)
Car 1 passed (final freq: 5501 Hz, pulses: 547)
  [DEBUG] intervals (us): 181 182 181 182 181 182 181 181 182 181
```
