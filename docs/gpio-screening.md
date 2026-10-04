# Adjacent-header GPIO screening

Prepared 2026-10-04 for the seller's 44-pin ESP32-S3 N16R8 board.
With components facing you, antenna at the top and USB connectors at the
bottom, header positions are counted top-to-bottom.

This is a limited screen using input modes and internal weak pull resistors.
It is not a continuity meter and cannot certify that soldering has no shorts.

## Preparation and execution

Disconnect all MAX98357A, TFT, encoder, keyboard, and external debugger wiring.
Connect only laptop USB. Do not move wiring while powered.

Compile/upload the separate environment:

```sh
~/.platformio/penv/bin/pio run -d usb-host-s3 -e gpio-scan -t upload
```

It uses the verified watchdog reset after upload. The scanner does not
initialize the RGB LED, UART, audio, or USB host. No tone or RGB indicator
is expected.

Open native USB serial at 115200 baud and send the character `s`.
Capture everything from `GPIO_SCAN_BEGIN` to `GPIO_SCAN_END`.
The report includes individual-pin readings, every adjacent header pair,
and a coverage summary. Scanned pins return to floating input mode afterward.

The default environment remains `esp32s3`. Restore audio/MIDI firmware after
the test with `pio run -d usb-host-s3 -e esp32s3 -t upload`.

## Coverage

24 ordinary GPIOs are screened individually:
1, 2, 4–18, 21, 38–42, 47.

20 of the 42 adjacent header gaps have two eligible GPIOs.
The remaining 22 are explicitly skipped. Exclusions:
- Supply, ground and RST/EN: cannot safely perform software continuity testing.
- GPIO0, 3, 45, 46: boot strapping.
- GPIO19/20: native USB used for the report.
- GPIO35/36/37: octal PSRAM on this N16R8 hardware.
- GPIO43/44: UART bridge.
- GPIO48: onboard RGB LED circuit.

The two adjacent 3V3 pins and adjacent ground pins are intentionally
connected by the board; continuity between those is not a solder fault.

## Reading results

`NO_ANOMALY` means expected digital readings under these weak pulls.
It does NOT mean no short exists.

`SUSPECT_HELD_OR_LOADED` means an individual input did not follow its pull.
`SUSPECT` means a pair did not give expected repeated readings in one or
more of four bias states (both up, both down, up/down, down/up).
Noise, leakage, board circuitry, or a solder bridge can cause these readings.

Opposing pull resistors on a bridged pair can produce an intermediate voltage
outside guaranteed digital thresholds, so even a bridge may escape detection.
GPIO14 is beside the 5V header pin; software cannot distinguish a stuck-high
reading from a 5V bridge. Internal pulls are not overvoltage protection.

Use a multimeter with all power removed to confirm suspects and check skipped
power/reset/USB/memory areas. No hardware modifications are justified by a
screening flag alone.

Sources:
- [Espressif Arduino GPIO modes and weak pulls](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/gpio.html).
- [ESP32-S3 pin restrictions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/gpio.html).

## Results — 2026-10-04

Initial run: 24 individual GPIOs behaved normally; one of 20 adjacent pairs
was flagged (right positions 9–10, GPIO39/38). Repeat: all 24 GPIOs and all
20 pairs gave expected readings. The anomaly was not reproduced and is not
a confirmed short. No solder rework was recommended. All 22 excluded gaps
remain untested. See progress-2026-10-04.md and both dated raw reports.
Audio/MIDI firmware was restored successfully after capture.
