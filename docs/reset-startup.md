# ESP32-S3 startup diagnosis and workaround

## Confirmed behavior (2026-10-03)

With every external module removed and the standalone boot diagnostic installed:
- Wall charger through USB/OTG: starts automatically.
- Same charger through USB-to-serial: requires RESET.
- Green diagnostic and serial heartbeat run after manual reset.
- A software watchdog reset from the Mac also starts the diagnostic successfully.

The charger tests identify a connector-specific startup issue. They do not
establish which electrical signal fails. EN/reset timing, GPIO0/BOOT behavior,
and the connector power path still need measurement before a permanent repair.
The laptop is not required to reproduce the serial-connector symptom.

## Start without pressing RESET when attached to the Mac

For a board stuck in download mode:

```sh
~/.platformio/penv/bin/python usb-host-s3/tools/start-board.py
```

If needed, pass `--port /dev/cu.usbmodem1101` (the number may change).
The command reads flash identity and performs a watchdog reset; it does not
rewrite flash or eFuses. It requires PlatformIO's Python/esptool environment.
It is intended for an already stalled bootloader, not a running application.

The installed platform supports `board_upload.after_reset = watchdog-reset`,
now configured for both environments, so future uploads use the reset method
that started this board during testing.

This is a laptop workaround. It does not make cold startup through the serial
connector work automatically from a wall charger.

## Firmware selection

- Default `esp32s3`: existing MAX98357A audio + USB MIDI diagnostic.
- `boot-diagnostic`: green heartbeat, reset reason/GPIO0 logs; no external modules.

Select a diagnostic upload explicitly with `pio run -e boot-diagnostic -t upload`.
Native USB serial may disappear when the audio/MIDI firmware takes over the USB
controller for host mode; that disappearance alone is not evidence of a crash.

## Backup and later hardware test

Pre-diagnostic flash offsets 0..0xFFFFF are saved in
`usb-host-s3/.pio/pre-boot-diagnostic-backup.bin`. Keep this until restoration is
verified. It is a partial flash backup covering the upload's modified region.

When parts or measuring tools are available, investigate the serial connector's
reset/BOOT/power-up circuitry. Espressif documents 1–10 µF capacitance between
EN and GND for reliable reset on some third-party boards. A temporary capacitor
test was proposed but not performed; the user has no capacitor or multimeter.
This is not yet a confirmed hardware fix.

Reference: [Espressif boot/reset guidance](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html).
