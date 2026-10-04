# WT9932P4-TINY MAX98357A MIDI synth

The P4 firmware starts silent. A MIDI Note On starts the square-wave oscillator
through an ADSR envelope; releasing the last held key fades it to exact silence.
This supersedes the earlier continuous-tone test. The current firmware also includes
[TFT navigation, waveform/volume controls and MIDI Learn](p4-menu.md).

| Stage | Default |
|---|---|
| Attack | 5 ms |
| Decay | 50 ms |
| Sustain | 70% |
| Release | 200 ms |

The envelope matches the existing AudioKit synth defaults and is defined in
p4-bringup/src/MonoSynth.h. Startup volume is 50% during initial hardware tests and remains adjustable on the Sound page. Velocity also scales volume. This is monophonic with
last-held-note priority: overlapping keys share one voice, and releasing the
newest key returns to another held key. Channels and pitches are matched for
Note Off; velocity-zero Note On also releases. CC123 releases that channel's
notes; CC120 cuts its sound. USB detach, MIDI queue overflow, and console panic
clear held notes and pending MIDI.

## Connections

Disconnect the board's USB power and the injector supply before wiring.

| MAX98357A pin | WT9932P4-TINY header label |
|---|---|
| VIN | 5V |
| GND | GND |
| BCLK | IO11 |
| LRC / LRCLK / WS | IO12 |
| DIN | IO13 |
| SD / EN | IO17 (GPIO-controlled enable) |
| GAIN | Leave unconnected |

Keep VIN and GND connected whenever SD is connected. SD is an enable/channel
selection input, not the amplifier's power input. Firmware keepsIO17 LOW
until I2S initializes, then drives HIGH (3.3V). HIGH selects the left I2S
channel; current firmware duplicates mono into both channels, so sound is
unchanged. Serial status shows output level, not module detection.

Connect the speaker between the MAX's + and - speaker terminals. Neither
speaker terminal connects to board GND. No MCLK connection is needed.

- Laptop data/power cable: FUSB.
- MIDI keyboard data connection with the working power injector: HUSB.
- TFT and encoder: see the [current P4 wiring map](display-encoder.md).
- TFT VCC stays directly on3.3V; MAX SD/EN now usesIO17. Do not also tie SD/EN to3.3V or5V.

The header numbers above are GPIO labels, not physical connector positions.
IO11–13 were checked against the manufacturer schematic. The onboard
single-wire WS2812 RGB LED uses GPIO51, with no external LED wire needed.

## LED meanings

| Appearance | Firmware status |
|---|---|
| Purple briefly at startup | Initializing |
| Amber blinking | Waiting for a USB device |
| Dim green with a bright pulse each second | USB host running with a device attached |
| Blue for about 150 ms after MIDI activity | Receiving valid MIDI packets |
| Two dim / bright amber flashes | MAX unverified / fault or missing enabled presence wire |
| Three dim / bright purple flashes | TFT unverified / fault or missing enabled presence wire |
| Red flashing rapidly | Other USB/queue firmware error |

Module blink codes take priority; see [module diagnostics](module-diagnostics.md).
Green reflects USB device presence; it does not measure amplifier power or
speaker output. Blue includes Note On, Note Off and pressure/controller data.

## Firmware and console

The board was identified as ESP32-P4 revision 1.3 with 16MB flash. Build target
esp32-p4 uses the matching pre-rev.300 libraries. FUSB hardware USB Serial/JTAG
logging stays separate from the high-speed HUSB host.

The test uses 48kHz stereo 16-bit I2S, with identical samples in both channels
and amplitude3600 (raised from900 after the user requested more loudness). This keeps all MIDI
note fundamentals below Nyquist. Empty/reserved USB MIDI records are ignored.
The USB callback queues values; the main loop handles pitch, logs and LED.

From p4-bringup, build/upload:
```sh
pio run -e esp32-p4 -t upload --upload-port /dev/cu.usbmodem1101
```

Use the currently detected macOS serial path if it changes. Monitor at 115200.
Console commands:
- s: current audio/USB status, counters and numeric device IDs.
- p: panic; clear notes and silence.
- c: current settings and controller assignments.
- b: steady white display test for 30 seconds; returns to the menu automatically.
- i: toggle LCD inversion; t: labeled black/white/color pattern for 30 seconds.
  Display investigation is currently deferred; do not run these tests unattended.
- a/d/u/r/w/v: select attack/decay/sustain/release/waveform/volume.
- l: MIDI Learn for the selected parameter; m: next message mode; x: cancel.
- + / -: one encoder-style adjustment of the selected parameter.

The previous MIDI-only diagnostic is retained in src/midi-diagnostic.cpp and
can be built/uploaded with environment midi-diagnostic. The default is the
audio/MIDI synth with TFT controls.

## Regression checks

From p4-bringup:
```sh
clang++ -std=c++17 -Wall -Wextra -Werror tests/adsr_test.cpp -o .pio/adsr-test
.pio/adsr-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/controls_test.cpp -o .pio/controls-test
.pio/controls-test
```

Checks cover envelope timing, release during attack, retriggering from release,
idle silence, note overlap/fallback, channel-specific Note Off/All Sound Off,
zero-velocity release, panic, high MIDI notes, live sustain changes, four waveforms,
rapid waveform transitions and volume mute/unmute.

For the speaker check, press and hold one key, then release it. Expect a short
fade of about 200 ms plus the small I2S buffer delay. Repeat with two overlapping
keys: releasing one should keep the other sounding; releasing both should stop.

## Sources

- [Manufacturer schematic](https://res.8ms.xyz/wiki_assets/WT9932P4-TINY/WT9932P4-TINY_SCH_V1.3.pdf)
- [Manufacturer resources and pin diagram](https://wiki.wireless-tag.com/docs/en/WT9932P4-TINY/board_resources.html)
- [Manufacturer RGB implementation](https://github.com/Wireless-TAG-Maker/WT_BSP/blob/main/components/wt_bsp/boards/WT9932P4-TINY/board.c)
- [Manufacturer USB power limitation](https://wiki.wireless-tag.com/docs/en/WT9932P4-TINY/faq.html)
- [MAX98357A amplifier pin descriptions](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)

## Other DAC or amplifier boards

The current speaker path is ESP32-P4 I2S -> MAX98357A -> speaker.
The MAX98357A is a digital-input Class-D speaker amplifier, so this path does
not require a separate DAC or analog-input amplifier.
[Analog Devices product documentation](https://www.analog.com/en/products/max98357a.html).

The user asked about a board called "PAM52xx"; its exact chip is not identified.
If it is a PCM5102/PCM5122 board, that is a stereo DAC with analog line outputs:
ESP32 I2S -> PCM DAC -> mixer/powered speaker/separate amplifier.
Those DAC outputs do not replace the speaker power amplifier.
[PCM5102A](https://www.ti.com/product/PCM5102A),
[PCM5122](https://www.ti.com/product/PCM5122).

Confirm the printed chip number before assigning a role or wiring that board.

## Update: identified parts and next loudness test (2026-10-04)

Current speaker is now user-identified as AIYIMA40mm/4Ω/3W, superseding
the previous approximate8Ω guess. PCM5102A identity is resolved, and
a Bluetooth/PAM8403 board plus two8Ω notebook speakers are also owned.
See [inventory and connection options](hardware-inventory.md).
The shared amplitude default is now7200, keeping50% startup volume;
peak bounds are3600 at50%,7200 at100%. Listening validation is pending.

## V1 three-note polyphony

Active P4 now has three independent note/envelope voices, each with three
oscillators. Fourth-note allocation prefers a released voice, then steals the
oldest held note. Stolen keys do not resume. Mix uses fixed divide-by-three
headroom: a solo note is quieter than the previous mono test; chord peak limits
remain7200 at100% master and3600 at50%. See [v1 release](v1-release.md)
for behavior, verification and remaining hardware limitations.
