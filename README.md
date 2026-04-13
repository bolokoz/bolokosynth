# ESP32-S3 Digital Synth

A single-board digital synthesizer using an ESP32-S3 as USB MIDI Host, audio synthesizer, and I2S output to a PCM5102A DAC with PAM8403 amplifier.

## Architecture (Unified Single-Board)

```
MIDI Keyboard ──[USB]──> ESP32-S3 ──[I2S]──> PCM5102A DAC ──[analog]──> PAM8403 ──> 2x 3W 8Ω Speakers
                                        │
                                    SSD1306 OLED (optional)
```

## Hardware Required

| Component | Notes |
|---|---|
| ESP32-S3-DevKitC-1 | Main MCU: USB Host + synth + I2S |
| PCM5102A DAC module | I2S input, stereo line-level output |
| PAM8403 amplifier | 2x3W class-D, drives 8Ω speakers |
| 2x 3W 8Ω speakers | |
| SSD1306 OLED (128x64, I2C) | Optional display |
| USB MIDI keyboard | e.g. Arturia MiniLab Mk2 |
| USB OTG adapter | To connect MIDI keyboard to ESP32-S3 |
| Buck converter | 5V supply for PAM8403 (if not USB-powered) |
| Jumper wires | |

## Wiring

### ESP32-S3 → PCM5102A (I2S)

| ESP32-S3 | PCM5102A |
|---|---|
| GPIO 4 | BCK |
| GPIO 5 | LRCK (WS) |
| GPIO 6 | DIN |
| 3.3V | VCC |
| GND | GND |
| — | SCK (leave unconnected, uses internal PLL) |

### PCM5102A → PAM8403 → Speakers

| PCM5102A | PAM8403 |
|---|---|
| L OUT | L IN |
| R OUT | R In |
| GND | GND |

| PAM8403 | Speakers |
|---|---|
| L+ / L− | Left speaker |
| R+ / R− | Right speaker |

PAM8403 power: **5V** (via USB or buck converter). Do **not** exceed 5.5V.

### ESP32-S3 → SSD1306 OLED (optional)

| ESP32-S3 | OLED |
|---|---|
| GPIO 8 (SDA) | SDA |
| GPIO 9 (SCL) | SCL |
| 3.3V | VCC |
| GND | GND |

### ESP32-S3 → MIDI Keyboard

USB OTG adapter on the ESP32-S3 USB port → USB cable → MIDI keyboard.

## Building

```bash
cd esp32s3-synth
pio run -t upload
```

Monitor serial output:

```bash
pio device monitor
```

## Legacy Multi-Board Setup

The original two-board architecture (ESP32-S3/S2 USB Host + ESP32-A1S AudioKit) is preserved in:

- `usb-host-s3/` — ESP32-S3 USB Host
- `usb-host-s2-ttgo/` — ESP32-S2 TTGO alternative
- `audio-synth-a1s/` — ESP32 AudioKit synth
