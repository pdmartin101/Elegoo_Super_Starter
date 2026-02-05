# ELEGOO Super Starter Kit for ESP32

PlatformIO project with examples from the ELEGOO Super Starter Kit for ESP32, plus custom sketches.

## Project Structure

```
├── platformio.ini          # 30 build environments
├── src/                    # Source files (.cpp)
│   ├── blink.cpp           # Simple LED blink
│   ├── rgb_fade.cpp        # RGB LED PWM fade
│   └── elegoo_*.cpp        # ELEGOO kit examples
└── docs/                   # PDF documentation
    ├── part1/              # Setup guides
    ├── part2/              # Module tutorials
    ├── part3/              # Combination projects
    └── part4/              # Advanced projects
```

## Available Sketches

### Custom
- `blink` - Simple LED blink on GPIO 2
- `rgb_fade` - RGB LED PWM fade (GPIO 2, 4, 5)

### Part 2: Module Learning
- `elegoo_2_02_rgb_led` - RGB LED control
- `elegoo_2_03_digital_inputs` - Button inputs
- `elegoo_2_05_active_buzzer` - Active buzzer
- `elegoo_2_06_passive_buzzer` - Passive buzzer with tones
- `elegoo_2_07_tilt_ball_switch` - Tilt sensor
- `elegoo_2_08_servo` - Servo motor control
- `elegoo_2_09_ultrasonic` - HC-SR04 distance sensor
- `elegoo_2_10_keypad` - 4x4 membrane keypad
- `elegoo_2_11_dht11` - Temperature/humidity sensor
- `elegoo_2_12_joystick` - Analog joystick
- `elegoo_2_13_ir_receiver` - IR remote receiver
- `elegoo_2_14_oled` - I2C OLED display
- `elegoo_2_15_mpu6050` - Gyroscope/accelerometer
- `elegoo_2_16_pir_sensor` - Motion sensor
- `elegoo_2_17_rfid` - RC522 RFID reader
- `elegoo_2_18_74hc595_led` - Shift register LED control
- `elegoo_2_19_serial_monitor` - Serial communication
- `elegoo_2_20_dc_motor` - DC motor with L293D
- `elegoo_2_21_stepper` - Stepper motor

### Part 3: Combination Projects
- `elegoo_3_01_thermometer` - DHT11 + OLED thermometer
- `elegoo_3_02_segment_display` - 7-segment with 74HC595
- `elegoo_3_04_photocell` - Light sensor
- `elegoo_3_05_four_digit` - 4-digit 7-segment display
- `elegoo_3_06_relay` - Relay control
- `elegoo_3_07_stepper_remote` - IR-controlled stepper

### Part 4: Comprehensive Projects
- `elegoo_4_01_weather_station` - DHT11 + OLED weather display
- `elegoo_4_02_snake_game` - Snake game on OLED
- `elegoo_4_03_access_control` - RFID + keypad + servo lock

## Usage

1. Open in VSCode with PlatformIO extension
2. Click environment selector in status bar (bottom)
3. Choose a sketch (e.g., `elegoo_2_11_dht11`)
4. Click Upload (→) to flash to ESP32

Or via terminal:
```bash
pio run -e elegoo_2_11_dht11 -t upload
pio device monitor
```

## Hardware

- Board: ESP32 DevKit (esp32dev)
- Framework: Arduino
- Monitor speed: 115200 baud

## Libraries

Libraries are automatically installed per-environment:
- Adafruit DHT, SSD1306, GFX, MPU6050
- ESP32Servo, Keypad, IRremoteESP8266
- MFRC522 (RFID), Stepper
