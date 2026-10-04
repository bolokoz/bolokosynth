# BolokoSynth v1 complete

Release scope agreed by the user on 2026-10-04. Active target:
WT9932P4-TINY pre-rev300 / revision1.3, 16MB flash, Arturia MiniLab MkII,
ST7789 TFT/EC11, one MAX98357 and AIYIMA4Ω/3W speaker.

## Included

- Three simultaneous notes, each with three oscillators, independent ADSR
  and velocity. Mono mix duplicated to left/right I2S slots.
- Eight patches; waveform, oscillator levels, coarse/fine tuning, ADSR and
  volume menus. Default startup master50%.
- MiniLab factory Preset1 defaults,16 rotations/two click actions, channel-aware
  MIDI Learn with absolute pickup and relative modes.
- Pitch bend±2semitones and CC1 slider vibrato depth.
- White TFT menus, vertical zero-time envelope segments, right-side parameter
  popup with units/pickup feedback, local encoder and KEY0 volume shortcut.
- RGB status, fault diagnostics and truthful limits on MAX/TFT detection.
- Hardware inventory, power plan, wiring and chronological diagnostic captures.

## Polyphony behavior and headroom

Idle voices are allocated first, then the oldest releasing voice, then the
oldest held voice. A fourth key truncates a voice; stolen notes do not resume.
A stolen key's eventual Note Off cannot release its replacement. Identical
notes on different MIDI channels are independent. Repeated Note On for the
same channel/key retriggers one voice. All Sound Off cuts tails; Panic clears
keys, voices and controller state.

The fixed three-voice mix divides the sum by three. This preserves the
7200-count ceiling at100% master and3600-count ceiling at50%, including
aligned notes. A solo note is consequently about9.5dB lower than the former
mono firmware; this deliberate headroom prevents chord clipping and gain
pumping when tails end. Actual amplifier watts/acoustic loudness are unmeasured.
The TFT/serial voice count includes release tails; held count includes physical
keys whose voices were stolen. Pitch/stage telemetry describes a representative
newest sounding voice, not all three envelopes.

## Nuances and known limits

Code files begin with purpose, ownership and relevant hardware/workaround notes.
MonoSynth remains the tested one-voice DSP primitive; PolySynth manages three
instances. Audio state is owned by the loop, UI state by its task; USB callbacks
and encoder ISR communicate through queues/captured deltas.

ADSR zero times use a one-sample floor in DSP to avoid division by zero.
A hard voice steal cuts an existing tail and can click, especially with zero
attack; there is no extra crossfade voice. Triangle is not band-limited.
No sustain-pedal feature is included. Custom patches and Learn bindings are
RAM-only. The small TFT control has only verified KEY0 button feedback.

I2S/SPI initialization cannot detect correct MAX/TFT wiring. Brightness was
resolved by power wiring, not changing libraries. The S3 serial-port cold-start
issue remains unresolved electrically and its startup script is a workaround.
Bluetooth/PAM analog-input availability is not confirmed; alternate stereo
hardware is not part of active v1. Legacy AudioKit/S2/S3 files are retained,
with comment-only changes for this release, and are not validated by P4 tests.

## Verification

Native tests cover ADSR, oscillator/preset output, controls/Learn, MiniLab
profiles, sliders, module health and polyphony. The polyphony test measures
three distinct pitch components, independent release, fourth-note stealing,
stale Note Off safety, MIDI channels, panic and output bounds.
Build/upload and live-board results follow after validation; physical listening
and popup appearance still require user confirmation.

Run the native suite:
`python3 p4-bringup/tools/test-native.py`

Build/upload active target:
`~/.platformio/penv/bin/pio run -d p4-bringup -e esp32-p4 -t upload --upload-port PORT`

## V1 validation and repository handoff

All seven native suites passed with clang++17/Wall/Wextra/Werror, including
three-pitch spectral verification and voice-stealing safety. Active P4 build
and upload succeeded; flash hashes verified. Live capture confirms MiniLab,
I2S/HUSB ready,50% startup volume, three-voice telemetry, fault0 and no dropped
MIDI/UI events. Actual listening confirmation was requested separately.
Capture: [p4-v1-polyphony-2026-10-04.txt](p4-v1-polyphony-2026-10-04.txt).
User requested v1 complete, code documentation, commit and push to origin/main.
