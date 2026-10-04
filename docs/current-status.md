**V1 complete — 2026-10-04.** See [release scope and known limits](v1-release.md).

# Current project status

Updated: 2026-10-04 (America/Asuncion).
This is the current P4 prototype summary; the dated progress log retains
earlier experiments. Recommendations below are not purchases or installed changes.

## Working prototype

Active hardware: Wireless-Tag WT9932P4-TINY, ESP32-P4 revision 1.3, 16 MB flash.
Firmware: p4-bringup/ (PlatformIO esp32-p4 environment).
Arturia MiniLab MkII: USB VID 1C75 / PID 0289, observed MIDI channel 1.
Audio: one MAX98357A and one passive speaker; three-note polyphony mixed to mono.
Display: seller-labeled 2.4-inch ST7789, 320 x 240 landscape, EC11 plus KEY0.

The keyboard produces notes, the speaker changes pitch, and ADSR releases
notes correctly. TFT menus, the large encoder and its press work. User
reports the backlight now works after direct TFT supply wiring.

The last uploaded firmware includes the white menu theme, KEY0 volume shortcut,
MiniLab defaults, opt-in local-input trace and **50% startup volume**, with signal amplitude7200 (latest controlled loudness increment; upload validation below).
Upload on2026-10-04 succeeded, flash hashes verified; live status confirmed
Volume50%, I2S/HUSB ready, MiniLab detected, fault0 and no dropped MIDI/UI.
Capture: p4-volume-50-2026-10-04.txt.

## Firmware features and verification

- V1: three-note polyphony, independent ADSR/velocity per note, shared master volume.
- Envelope graph now draws zero-time Attack/Decay/Release vertically;
  build/upload verified, physical visual check pending.
- Three oscillators: sine, triangle, square, saw; level, coarse/fine tuning.
- Eight sound presets; Sounds, Envelope, Output, OSC1/2/3 and MIDI menus.
- MIDI Learn with absolute pickup and five absolute/relative modes.
- MiniLab factory Preset 1 mappings: 16 rotary controls and two clicks.
  Hold Shift and tap Pad 1 on the keyboard to select this memory.
- CC1 modulation strip: vibrato depth, 5 Hz up to +/-50 cents.
  A learned CC1 assignment overrides this default.
- Pitch bend: 14-bit messages, +/-2 semitones, per MIDI channel.
- RGB status indications for USB/MIDI, firmware faults and unverified MAX/TFT.
  Write-only I2S/SPI initialization cannot prove module presence or wiring.
- Native ADSR, controls, sound, slider and profile tests passed during
  implementation; subsequent UI/trace firmware built/uploaded successfully.
  No new tests or upload were run for this documentation update.
- Live status after the most recent upload: I2S/HUSB ready, keyboard detected,
  TFT initialized, no firmware faults or dropped MIDI/UI events.
- Startup master volume is now **50%**, uploaded and verified in live serial.
  Runtime volume may differ: the user's knob 16
  sweep reached CC72 value 126 during testing. Do not confuse startup default
  with the last runtime value.
- Parameters, controller mappings and manual profile selection remain RAM-only.

Current audio output is mono duplicated into both I2S channels. Stereo
oscillator panning/effects and a second output channel are not implemented.

## MiniLab controls

| Control | Assignment |
|---|---|
| Knob 1 | Sound preset |
| Knobs 2 / 3 / 4 / 5 | Attack / Decay / Sustain / Release |
| Knobs 6 / 7 / 8 | OSC1 waveform / level / fine tuning |
| Knob 9 | OSC3 waveform |
| Knobs 10 / 11 / 12 / 13 | OSC2 waveform / level / coarse / fine tuning |
| Knobs 14 / 15 | OSC3 level / fine tuning |
| Knob 16 | Master volume |
| Press knob 1 | Reload current sound; preserve volume and mappings |
| Press knob 9 | Panic: silence notes and reset pitch/modulation |
| Modulation strip | Vibrato depth unless learned elsewhere |
| Pitch strip | Pitch bend |

Both click actions were physically verified: CC113 and CC115 press 127/
release 0 produced reload and panic respectively. CC72 volume changed after
pickup. All default assignments were checked in live status; not every
rotary control has been physically swept. Relative knobs 1/9 use REL SIGN.
See [complete profile and CC table](midi-controller-profiles.md).

## Local controls

Large encoder: browse/edit and press to select/finish. Hold PUSH starts Learn
on parameter pages; on MIDI Control it changes the binding mode.

Small control: tap KEY0 to open Volume editing, turn the large encoder to
adjust, tap KEY0 again to open the main menu. Hold KEY0 for panic. During
Learn, a KEY0 tap cancels.

User reports the small control also rotates physically. During a live test,
only KEY0 press/release changed; no A/B rotation events appeared on connected
inputs. Independent small-control rotation is not supported by the observed
wiring. Console e observes GPIO18/19/20/21 for 60 seconds without driving them.

## Current wiring

| Destination | P4 connection |
|---|---|
| TFT VCC | Direct short connection to 3.3V |
| TFT GND | Direct GND connection |
| TFT SCL / SDA | IO9 / IO10 |
| TFT CS / DC / RES | IO14 / IO15 / IO16 |
| EC11 A / B / PUSH | IO18 / IO19 / IO20 |
| Small control KEY0 | IO21 |
| TFT BLK | Open |
| MAX VIN / GND | 5V / common GND |
| MAX BCLK / LRC / DIN | IO11 / IO12 / IO13 |
| MAX SD/EN | IO17, newly configured; physical move not yet confirmed |
| Built-in RGB LED | GPIO51 |

MAX SD/EN is an enable/mode input, not its power supply. GPIO17 enable firmware
was built/uploaded and live status confirmed HIGH with I2S ready and50% volume.
It drives LOW during setup until I2S initializes. HIGH selects left channel;
mono is currently duplicated to both channels. Physical SD->IO17 connection
and audible test are not yet confirmed. Never also tie SD to a power rail.
No MAX breakout schematic has yet been confirmed.

The board has one 3.3V header pin, one 5V pin and four GND pins.
LDO_VO4 belongs to the SD voltage domain and is not a substitute 3.3V rail.
See [wiring guide](display-encoder.md) and the
[manufacturer pin table](https://wiki.wireless-tag.com/docs/en/WT9932P4-TINY/board_features.html).

## TFT brightness findings

Correct labeled black/white/colors and normal contrast were observed, but
backlighting was uniformly dim. Direct TFT VCC/GND wiring with MAX SD/EN
removed from the shared branch fixed the reported dimness. No library switch
was needed. User associates continuous audio with brightness variation;
its magnitude and electrical cause have not been measured.

Current LCD settings: Adafruit ST7789, SPI mode 0, 10 MHz, rotation 1, INVOFF,
white background/dark text. BLK stays open; an earlier BLK-to-3.3V test made
the screen go dark and was reverted.

Seller labels the TFT module 3.3V/5V-compatible. That does not establish
5V-safe ESP32 connections: supplied A/B/PUSH/KEY0 schematics pull signals up
to module VCC. A change to 5V requires checking the module supply circuit
and level shifting those return signals, or separately powering its backlight.
Current working setup remains 3.3V. No multimeter is available.

See [full brightness investigation](tft-investigation.md).

## USB power and proposed distribution

User reports the existing power-injection/split cable powers both the P4 and
MiniLab with one board USB connector in use. Exact cable topology has not
been traced; this is a user-observed working arrangement.

A second board USB connector is useful for simultaneous upload/logging,
but not inherently required for normal MIDI-host operation. Native USB
host/OTG wiring and a suitable power path remain necessary; a UART-only
USB connector cannot host the keyboard.

Proposed solder-free distribution: separate 5-port lever connectors for 5V
and GND, optional separate 3.3V connector, short 20–22 AWG copper power wires.
One regulated 5V/3A source is a preliminary target, not a measured load budget.
5V feeds board, MAX VIN and keyboard injector; board-regulated 3.3V feeds TFT.
Keep separate branches rather than sending TFT power/ground through MAX wiring.
Do not join independent laptop and charger 5V outputs.

No distribution parts have been selected from a verified AliExpress listing,
purchased or installed. See [proposed shopping/wiring plan](power-distribution-plan.md).

## Speaker and additional audio board decision

One MAX98357A drives one speaker; its +/− terminals form one differential
speaker output. Two MAX boards can provide left/right speaker channels when
configured accordingly, with stereo firmware work still needed.

A larger/better speaker can improve sound without adding a second MAX.
A 4-ohm, 5W, approximately 2–3-inch speaker was suggested as a starting option;
MAX output remains limited to roughly 3W at 5V and speaker sensitivity/
enclosure matter. No speaker purchase or replacement is confirmed.

User has now identified the former “Pam5102” as **GY-PCM5102 / PCM5102A**,
a stereo line-output DAC. Also owns a Bluetooth/PAM8403 stereo amplifier;
its analog input availability remains to be confirmed. The advertised 2×5W
is inconsistent with PAM8403 specifications at 5V/4Ω (about 2.5W/channel
at 1% THD). Neither alternate board is installed.

Current speaker is user-identified **AIYIMA 40mm, 4Ω/3W** (2-piece pack);
the previous approximate 1-inch/8Ω identification is superseded. Also owns
two notebook cavity speakers, selected 8Ω/2W, 2.0mm terminals.
See [exact inventory and loudness options](hardware-inventory.md).

Sources:
- [MAX98357A](https://www.analog.com/en/products/max98357a.html)
- [PCM5102A](https://www.ti.com/product/PCM5102A)

## USB-only loudness goal

User wants the complete system powered from one USB cable: wall adapter, car
USB port or power bank. Recommend generic5V operation, a source targeting3A,
and first testing one efficient4-ohm,5–10W,approximately3-inch speaker with
the existing MAX (roughly3W output). A second MAX gives two3W channels, not
6W into one speaker. Boost/PD approaches need a new power budget and cannot
guarantee full output on weak legacy USB ports. The earlier10–20W amplifier
suggestion is not the selected default direction.
See [USB-only power/audio plan](usb-audio-power-plan.md).

## Louder signal test

User requested more loudness. Raised the shared signal-amplitude default900
to3600, retaining50% master and GPIO17 enable. This is approximately+12dB
electrical signal amplitude, not a measured acoustic/power-output result.
Existing ADSR, sound, slider, controls and profile tests passed; build/upload
succeeded with flash hashes verified. Live status confirms50% volume, I2S/HUSB
ready, one MiniLab, fault0 and no dropped MIDI/UI events.
Capture: p4-louder-signal-2026-10-04.txt. User confirms no distortion and no TFT dimming with the louder signal,
but loudness remains too low. The earlier visual 1-inch/8Ω estimate is superseded by the purchased AIYIMA listing: 40mm/4Ω/3W. Sensitivity remains unknown.

## Next work


1. Compare the next digital-level increase on the existing 4Ω speaker.
   Confirm whether the Bluetooth/PAM board exposes an analog AUX input.
2. Select power-distribution parts and one suitable 5V source; check exact
   injector/adapter wiring before assembling it.
3. Check brightness stability while playing, then measure supply/ground
   voltage drops when a multimeter is available.
4. Physically validate remaining MiniLab defaults; custom memories use Learn.
5. Add stereo processing only if the hardware direction calls for it.

The earlier ESP32-S3 startup issue remains unresolved electrically; its
diagnosis/software workaround is saved in reset-startup.md. The P4 is the
active prototype. No commit, push or PR was created for this status update.

## Owned hardware / next loudness increment

All five audio purchase links and selected variants are saved in hardware-inventory.md.
Shared signal amplitude is now7200 with50% startup master: peak ceiling3600
at50%,7200 at100%. This is +6dB from the prior3600 signal; physical loudness,
distortion and supply stability require a new listening test.
Build/upload result is recorded separately after validation.

### Validation of the 7200-level update

Existing native ADSR, three-oscillator sounds, slider, controls and MiniLab
profile tests passed with strict compiler warnings. P4 build/upload succeeded;
flash hashes verified. Live serial confirms50% volume, I2S/HUSB ready,
MiniLabVID1C75/PID0289, fault0, no dropped MIDI/UI events, GPIO17 HIGH.
Capture: [p4-louder-7200-2026-10-04.txt](p4-louder-7200-2026-10-04.txt).
This verifies firmware operation, not acoustic output, wiring or supply voltage.
Listening check and Bluetooth-board input labels remain pending.

## Knob feedback popup (2026-10-04)

Local encoder parameter edits and all assigned MIDI CC knob movements now
show a high-contrast popup on the right of the current page: parameter name,
current formatted value with units, and MIDI channel/CC or Encoder.
It refreshes while turning and disappears1.8seconds after the last event.
Absolute controls waiting for pickup say “Turn to match” and retain the
actual current value; unassigned CCs suggest MIDI Learn. Default CC1 slider
shows vibrato percentage. Browsing with the local encoder still navigates.

Feedback is carried by value in the existing UI snapshot; SPI remains on
the UI task. Covered content jobs wait until expiry, while status/header/
footer remain live. The current page is restored without a full-screen clear.
Firmware build/upload and physical visual validation are recorded separately.

Popup build/upload succeeded with flash hashes verified. Live status:
I2S/HUSB ready, MiniLab connected,50% master, fault0, no dropped MIDI/UI
events. Capture: p4-knob-popup-2026-10-04.txt. Physical popup layout,
continuous turning and dismissal still require user visual confirmation.

## V1 validation and repository handoff

All seven native suites passed with clang++17/Wall/Wextra/Werror, including
three-pitch spectral verification and voice-stealing safety. Active P4 build
and upload succeeded; flash hashes verified. Live capture confirms MiniLab,
I2S/HUSB ready,50% startup volume, three-voice telemetry, fault0 and no dropped
MIDI/UI events. Actual listening confirmation was requested separately.
Capture: [p4-v1-polyphony-2026-10-04.txt](p4-v1-polyphony-2026-10-04.txt).
User requested v1 complete, code documentation, commit and push to origin/main.
