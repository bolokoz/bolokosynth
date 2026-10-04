# MIDI sliders and learned knob modes

The user's live slider capture sends **CC1**, channel 1, across absolute
positions (observed 1 through 126). Its chosen default function is vibrato depth.

## Slider behavior

- Unassigned CC1 controls vibrato: 0 is off, 127 is +/-50 cents, rate 5 Hz.
  It affects all three oscillators together, preserving their coarse/fine ratios.
- Modulation depth is smoothed over roughly 10 ms. Each MIDI channel has its
  own modulation state; only the active note's channel controls the mono voice.
- A MIDI Learn binding on CC1 takes priority. Learning or using that binding
  clears the channel's default vibrato, preventing an old depth from remaining.
- To use the slider for another parameter, learn it in **ABS** mode. Match or
  cross the parameter's current value to acquire pickup; assignment alone
  deliberately does not jump the value.
- Console j starts Volume slider Learn in ABS, even if the last knob used a
  relative mode. The TFT Learn display shows the actual console-selected target.
- Separate MIDI pitch-bend messages are supported automatically: 14-bit,
  center 8192, range +/-2 semitones, per channel. Pitch changes glide briefly
  without restarting ADSR. This is independent of the CC1 modulation slider.
- MIDI CC121 resets pitch bend and modulation on its channel. Panic/USB
  disconnect clears all channel pitch/modulation state as well as notes.

Pitch-bend message structure was checked against
[MIDI Association's message overview](https://midi.org/about-midi-part-3midi-messages)
and the [Java MIDI interface specification](https://docs.oracle.com/en/java/javase/24/docs/api/java.desktop/javax/sound/midi/MidiChannel.html).
No pitch-bend messages were captured from the user's slider in the first test;
that control is confirmed CC1.

## A learned knob does not change the value

Learning a channel/CC and interpreting its values are separate operations.
An endless relative encoder learned in ABS can remain waiting for pickup.
A real absolute knob/slider also waits until its position reaches or crosses
the current value.

The MIDI menu now corrects an existing binding's mode:

1. Open MIDI Control and select the parameter already assigned to the knob.
2. Hold PUSH to cycle **ABS -> REL OFFSET -> REL 2'S -> REL 16 -> REL SIGN -> ABS**.
3. The existing channel/CC assignment is retained, and the displayed control
   mode changes immediately. Turn the knob to test it.

Previously this action changed only the next Learn mode, so an existing
incorrect assignment stayed incorrect. Relative bindings start responding
without pickup; absolute bindings rearm pickup when their mode changes.
The firmware does not guess a knob's relative encoding from a few values.
An isolated 64 message is insufficient to identify a mode. Initial live capture
contained notes and CC1 slider traffic, with no rotary-knob CC traffic captured.

Console m provides the same mode correction for the selected assigned target.
When unassigned or during active Learn it selects the mode for the upcoming
assignment. Mode changes and bindings remain RAM-only.

## Validation

Native tests cover 14-bit pitch endpoints/center/LSB precision, channel
independence, bend before Note On and during release, exact silence, controller
reset, bounded +/-50-cent modulation, absolute slider Learn/pickup and changing
an already learned ABS binding to REL16 without losing its CC. Existing
ADSR, sound and MIDI-control suites cover compatibility. Live captures are
stored alongside this guide; physical knob encoding still requires observation.


The MiniLab factory defaults and new REL SIGN mode are described in
[midi-controller-profiles.md](midi-controller-profiles.md). Factory profile
source identifies knobs 1/9 as RelativeSM; MIDI Learn now uses this hint unless
the user explicitly chooses a different mode.
