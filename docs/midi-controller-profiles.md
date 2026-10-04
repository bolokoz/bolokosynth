# MIDI controller profiles: Arturia MiniLab MkII

The synth now separates controller-specific defaults from the sound engine.
MiniLab MkII is recognized by the user's observed USB VID 1C75 / PID 0289.
It loads its profile automatically. Unrecognized keyboards use Generic / Learn.

Use **MIDI Preset 1** on the MiniLab (hold Shift and tap Pad 1).
Custom memories may send different CCs/modes; use MIDI Learn or Generic mode
for those instead of assuming that the factory table applies.

Arturia documents sixteen rotary encoders, with additional click actions on
1 and 9. The profile therefore defines sixteen knobs plus two clicks.
The touch strips are additional pitch-bend and modulation controls.

## Default knob assignments

| Knob | MIDI CC | Synth control | Mode |
|---|---|---|---|
| 1 | 112 | Sound preset | REL SIGN |
| 2 | 74 | Attack | ABS |
| 3 | 71 | Decay | ABS |
| 4 | 76 | Sustain | ABS |
| 5 | 77 | Release | ABS |
| 6 | 93 | OSC1 waveform | ABS |
| 7 | 73 | OSC1 level | ABS |
| 8 | 75 | OSC1 fine tune | ABS |
| 9 | 114 | OSC3 waveform | REL SIGN |
| 10 | 18 | OSC2 waveform | ABS |
| 11 | 19 | OSC2 level | ABS |
| 12 | 16 | OSC2 coarse tune | ABS |
| 13 | 17 | OSC2 fine tune | ABS |
| 14 | 91 | OSC3 level | ABS |
| 15 | 79 | OSC3 fine tune | ABS |
| 16 | 72 | Master volume | ABS |
| Press knob 1 | 113 | Reload the current sound's original settings | Momentary |
| Press knob 9 | 115 | Panic / silence all notes | Momentary |

Defaults use MIDI channel 1. Reload preserves master volume and mappings;
panic clears notes and pitch/modulation state. Click actions fire once per
press, requiring release before firing again. OSC1/OSC3 coarse tuning remain
available through the TFT and can be reassigned with MIDI Learn.

**Absolute controls use pickup.** Sweep the control until it reaches or crosses
the current value; the binding shows pickup while waiting. This protects the
50% startup volume from jumping to a stale controller position. Relative knobs
1 and 9 respond immediately to movement and ignore neutral 0/64 messages.

The modulation strip remains CC1 vibrato depth (0 to +/-50 cents, 5 Hz).
Pitch strip messages bend +/-2 semitones. A learned CC1 assignment takes
priority over default vibrato.

## Knob decoding and MIDI Learn

The local factory integration identifies knobs 1/9 as **RelativeSM**
(sign magnitude). The new REL SIGN decoder uses 1..63 for positive movement,
65..127 for negative movement, and ignores 0/64. This differs from both
two's complement and the 64-centered relative mode.

When learning a factory MiniLab CC, the application selects its profile mode
automatically. Cycling the mode during active Learn explicitly overrides that
hint. Learning another controller remains generic. Learning a CC already
assigned to a different parameter moves that assignment.

On MIDI Control, hold PUSH to cycle the selected existing binding's mode while
retaining its channel/CC. The modes are ABS, REL OFFSET, REL 2'S, REL 16 and
REL SIGN. The controller name and actual binding mode appear on this page.

Same-model USB reconnect retains customized bindings. A different VID/PID loads
that controller's defaults, clearing the old mappings but retaining the sound
settings/master volume. Bindings and manual profile choices remain RAM-only.

Console K restores MiniLab defaults and locks that profile until restart;
G clears mappings and locks Generic / Learn. Both replace existing assignments.
Console m corrects the selected existing binding mode. q logs the next 96 raw
CC events for diagnosis. j explicitly starts ABS slider Learn for Volume.

## Adding future controllers

The pure C++ ControllerProfiles.h table contains USB matching, control
definitions, mode hints and click actions. Add another profile there; the
oscillator, envelope, TFT and MIDI binding implementation remain generic.
Custom mappings use SynthControls::setBinding or the existing MIDI Learn UI.
No keyboard settings are written over SysEx by this firmware.

## Sources and validation

Physical controls and memory selection:
[Arturia MiniLab MkII manual](https://downloads.arturia.com/products/minilab-mkII/manual/MiniLabmkII_Manual_1_0_7_EN.pdf)
and [Arturia shortcuts FAQ](https://support.arturia.com/hc/en-us/articles/4405748007570-MiniLab-MkII-General-Questions).

Preset 1 CC numbers and RelativeSM modes were read from the installed Apple
GarageBand device integration:
Applications/GarageBand.app/Contents/Frameworks/MACore.framework/Versions/A/
Resources/MIDI Device Scripts/Arturia/Arturia MiniLab mkII.device/config.lua.
It explicitly labels the expected preset MIDI Preset 1. No integration code
was imported; synth targets and profile implementation are original.

Tests verify all default entries/unique assignments, correct identity matching,
signed relative movement/neutral messages, protected absolute volume, click
edges, Learn overrides, and generic-controller operation. Mode-cycle tests were
updated for the fifth mode. Raw captures initially showed only CC112 value64;
neutral values alone do not prove an encoder's movement behavior. Physical
direction/mode validation is distinguished from these source and unit checks.

Firmware built and uploaded successfully; flash hashes verified. Live serial
confirms MiniLab MkII VID1C75/PID0289, all sixteen expected bindings, REL SIGN
for CC112/114, Volume25%, I2S/HUSB ready, one device, no firmware faults or
dropped MIDI/UI events. The current sound was Pluck and the envelope idle.
Capture: p4-minilab-profile-2026-10-04.txt. This status check confirms loaded
assignments, not an audible/physical check of every rotary control.

Live press verification: CC113 press127/release0 reloaded the current sound;
CC115 press127/release0 panicked/silenced notes. User confirmed both presses
during the active capture. CC72 volume also swept and changed after pickup.
Capture: p4-minilab-click-trace-2026-10-04.txt.
