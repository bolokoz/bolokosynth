# ESP32 Digital Synth MVP

This project implements a minimal digital synthesizer using an ESP32 AudioKit v2.2 and an ESP32-S3 as a USB Host for a MIDI keyboard.

## Architecture

**MIDI Keyboard (Arturia MiniLab Mk2)** --[USB]--> **ESP32-S3 (USB Host)** --[UART]--> **ESP32-A1S (AudioKit)** --[I2S]--> **Audio Output**

## Components

### 1. USB Host (ESP32-S3)
- **Folder**: `usb-host-s3/`
- **Function**: Acts as a USB Host for the MIDI keyboard. Reads MIDI events and forwards them via UART (Serial1) to the AudioKit.
- **Hardware**: ESP32-S3 DevKit.
- **Connections**:
  - USB OTG Port -> MIDI Keyboard.
  - GPIO 17 (TX) -> RX on AudioKit (Check `usb-host-s3/src/main.cpp` for exact pins).
  - GPIO 18 (RX) -> TX on AudioKit.
  - GND -> GND.

### 2. Audio Synth (ESP32-A1S)
- **Folder**: `audio-synth-a1s/`
- **Function**: Generates audio based on MIDI input.
- **Hardware**: ESP32 AudioKit v2.2 (ES8388 Codec).
- **Connections**:
  - RX (Check `audio-synth-a1s/src/main.cpp` for exact pins) -> TX on ESP32-S3.
  - TX -> RX on ESP32-S3.

> **Note**: On ESP32 AudioKit v2.2, some GPIOs are shared with onboard keys or SD card. Ensure GPIO 22/23 (if used) do not conflict with your board revision. Check your schematic.

## Building

Use PlatformIO to build and upload the firmware.

```bash
cd usb-host-s3
pio run -t upload
```

```bash
cd audio-synth-a1s
pio run -t upload
```
