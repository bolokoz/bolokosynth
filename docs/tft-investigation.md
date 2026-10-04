# TFT investigation — resumed 2026-10-04

Current board: WT9932P4-TINY ESP32-P4. Module is seller-reported 2.4-inch
ST7789, 240x320 native resolution, shown in 320x240 landscape. Controller
identity has not been read electronically; the SPI hookup has no MISO.

## Findings

| Setup / test | User observation |
|---|---|
| Adafruit ST7789, 40MHz SPI, default INVON | ADSR menu appears; encoder rotation and PUSH work; display very dim |
| BLK connected directly to 3.3V | Screen did not turn on |
| BLK restored open | Menu returned; ESP32 RGB LED on; user described one light behind panel |
| Repeated RGB565 white fill with INVON | Black screen |
| Same white fill after INVOFF | Evenly gray |

The first timed white test was reported as the ADSR menu; timing could explain
that report. A later repeated test was reported black.

These observations do not establish a defective panel or the wrong library.
They support checking initialization, contrast and backlight power separately.
The P4 kept running during the tests: I2S and USB host ready, MIDI notes and
release observed, no reported faults or dropped MIDI/UI events.

## Current saved and uploaded state

- Adafruit ST7735/ST7789 library 1.11.0.
- Explicit SPI clock 10MHz, SPI mode 0, rotation 1.
- Explicit INVOFF at initialization, with a reversible console inversion toggle.
- Original wiring remains: TFT VCC 3.3V, shared GND, BLK open.
- Menu runs normally at startup. Diagnostic commands do not run automatically.
- Console b: steady white for 30 seconds; i: toggle inversion;
  t: labeled black/white/color pattern for 30 seconds.
- The 10MHz / INVOFF labeled pattern was implemented and uploaded, but was not
  triggered or visually verified before the user asked to leave the screen alone.

The user explicitly resumed this investigation on2026-10-04. Keep module
VCC3.3V while checking: encoder/button pullups share VCC. Existing wiring
remains unchanged. Labeled contrast/color test is the first resumed check.

## Deferred next diagnostic

When resumed, run the stationary labeled pattern at the current slow SPI speed.
Check whether the BLACK and WHITE halves have correct contrast and whether
the RGB bars match their labels. If colors remain washed out, compare a known
extended ST7789 initialization profile before replacing the rendering library.

Adafruit's generic profile uses reset/sleep-out/color/window/inversion/display
commands; Arduino_GFX also supplies power, VCOM and gamma values. This is a
possible initialization difference, not evidence that either profile matches
the purchased panel. Arduino_GFX is a candidate fallback, with P4 SPI support
in its transport; its driver selects SPI mode 3 and its IPS flag affects
inversion semantics. Do not assume identical settings across libraries.

Sources:
- [Seller listing](https://pt.aliexpress.com/item/1005008766561044.html), plus
  the rear-label/input/backlight screenshots supplied by the user.
- [Adafruit ST7789 initialization](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/Adafruit_ST7789.cpp).
- [Adafruit SPI speed diagnostic](https://github.com/adafruit/Adafruit-ST7735-Library/blob/master/examples/displayOnOffTest/displayOnOffTest.ino).
- [Arduino_GFX ST7789 profiles](https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_ST7789.h).
- [Arduino_GFX ST7789 driver](https://github.com/moononournation/Arduino_GFX/blob/master/src/display/Arduino_ST7789.cpp).
- [Sitronix ST7789V datasheet](https://dl.espressif.com/dl/schematics/ST7789V_SPEC_V1.0.pdf).

See [wiring](display-encoder.md) and [menu use](p4-menu.md).

## Resumed labeled-pattern observation

At10MHz/mode0/INVOFF, user sees the black/white split and all labeled colors,
but reports the whole screen very dim evenly. This suggests a backlight or
power issue more strongly than gross color/inversion failure; it does not
prove initialization is correct or that the panel is defective. Next requested
check: external flashlight readability and availability of a multimeter for
TFT VCC-to-GND measurement. Keep VCC3.3V/BLKopen; raising module VCC also
raises its encoder/button pullups. No voltage/wiring modification performed.

## Contrast versus backlight follow-up

User reports weak backlighting with fine contrast; BLK remains disconnected.
No multimeter available. Backlight supply/driver is the current leading
hypothesis; changing rendering libraries is not the next test. Requested
power-off rewiring check: remove MAX SD/EN from shared3.3V branch and use
short direct TFT VCC->P4 3.3V, TFT GND->P4 GND, BLKopen. Restore power and
compare brightness. This tests wiring/branch voltage loss without raising
voltage. Result pending. Seller listing/image could not be opened by web tool;
no new evidence establishes5V-safe controller or GPIO levels.

## Backlight improved after direct supply wiring

User reports the screen now works after bypassing the shared3.3V branch,
with MAX SD/EN removed from that branch and TFT VCC/GND connected directly.
User also associates continuous tone playback with screen brightness changes.
The wording does not establish the direction/magnitude of those changes.
This supports a supply/contact/ground-path problem rather than a rendering
library fix. No voltage was measured, so exact fault remains unproven.

MAX SD/EN is a mode/enable input, not the amplifier's main supply.
Amplifier VIN belongs on5V in this setup; TFT VCC remains3.3V.
Use short separate supply and ground branches back to the P4 power pins,
rather than routing TFT power/return through the MAX wiring. Keep BLKopen.
Leave the currently working SD/EN disconnection during diagnosis; breakout
default-enable behavior is board-specific, not a universal IC feature.
If tone-dependent dimming remains, check USB cable/supply and common wiring
before changing LCD initialization. Capacitor/supply measurements deferred
until parts or a meter are available. Firmware unchanged.
