# Three-oscillator sounds

The P4 synth now has three independent oscillators feeding one velocity-sensitive
ADSR envelope. It remains monophonic with last-held-note priority. These are
original starting sounds inspired by the oscillator workflow of 3x Osc.

## Navigation

Startup opens **SOUNDS / 3 OSC** with Init Square selected and output volume 50%.

1. Press the encoder to edit the preset.
2. Turn it to audition the next/previous sound while playing the MIDI keyboard.
3. Press again to finish editing.
4. Tap BACK to open the main menu.

The main menu scrolls through Envelope, Sounds, Output, Oscillator 1,
Oscillator 2, Oscillator 3 and MIDI Control. Three entries fit on each screen;
keep turning to reach the next group. Press to open an entry.

Each oscillator page has Shape, Level, Coarse and Fine. Turn to choose a row,
press to edit, turn to change, and press to finish. BACK first finishes an edit,
then returns to the menu. Hold PUSH on a parameter to assign a MIDI keyboard
knob through MIDI Learn. Hold BACK for panic/all notes off.

| Control | Range | Encoder movement |
|---|---|---|
| Shape | Square, Saw, Triangle, Sine | One waveform per detent |
| Level | 0–100% | One percentage point per detent |
| Coarse | -24 to +24 semitones | One semitone per detent |
| Fine | -100 to +100 cents | One cent per detent |

The Output waveform is the same control as Oscillator 1 Shape. Output Volume
remains the master level for the entire mix. MIDI Control includes every new
oscillator parameter and the Preset selector.

## Starting sounds

| Sound | Oscillators / character |
|---|---|
| Init Square | Original single square tone |
| Soft Sine | Single sine, gentler attack and longer release |
| Sub Bass | Triangle with a sine one octave below |
| Detuned Saw | Three saws at 0 / -8 / +8 cents |
| Warm Pad | Triangle, quiet detuned saw and upper sine, slow attack/release |
| Organ | Sines at fundamental, octave and octave-plus-fifth |
| Pluck | Saw and upper triangle with decay to zero sustain |
| Hollow Lead | Square, upper detuned square and lower triangle |

Changing a preset loads oscillator settings and ADSR, preserves your current
master volume and MIDI assignments, and rearms absolute-knob pickup. It does
not generate or retrigger a note: play a new key to hear its complete envelope.
Changing oscillator/envelope settings marks the patch with *. Selecting a
preset again restores its original settings. Preset and manual changes are
RAM-only, so power cycling returns to Init Square.

Oscillator 1 defaults to level 100%; oscillators 2 and 3 default to zero.
The mix divides by the total oscillator levels when that sum exceeds one,
preventing three full-level sources from tripling the output. Low total levels
still reduce volume. Level edits use a 10 ms slew, shape edits a 5 ms crossfade,
and coarse/fine edits a brief pitch glide. MIDI note changes select pitch
immediately. Square/saw retain polyBLEP edge correction. Tuning targets are
limited to 0.45 of the sample rate for extreme high-note/octave combinations.

## Reference review and scope

The project reference listed in references.md,
[cjdell/esp32s3-midi](https://github.com/cjdell/esp32s3-midi), implements an
ESP32-S3 USB MIDI device using Rust/Embassy, GPIO and WiFi note sources.
It does not provide an oscillator/synth sound engine, and its device role differs
from this P4's MIDI host.

[Image-Line's 3x Osc manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/3x%20Osc.htm)
describes independent oscillator shapes, coarse/fine tuning and mix ratios.
That guided this feature's workflow. The code and patches here are original.
The additional [ESP32-Synth project](https://github.com/The-Shreyas-M/ESP32-Synth)
was reviewed as general oscillator/envelope/voice architecture context, with
no code imported or new library dependency.

This version uses mono output duplicated to the two I2S slots, matching the
MAX98357A setup. Stereo panning/detune, rounded saw, noise/custom samples,
oscillator AM, filters, effects and polyphony are not implemented.
CC1 vibrato and pitch-bend support were added next; see [MIDI sliders](p4-midi-sliders.md).
It is an initial three-oscillator instrument, not a full FL Studio clone.

## Validation

Existing ADSR/audio and MIDI controls suites pass. New sound tests measure
the actual zero-crossing frequencies of isolated oscillators with octave/fine
tuning, compare eight preset output streams, check bounded quiet output,
release silence, all-source mute, high-note limits, tuning ranges, MIDI shape
pickup and binding/volume preservation across preset changes.
No LCD inversion, SPI speed, BLK wiring or other brightness setting changed.

P4 build/upload passed with flash hash verification. Serial cycled all eight
starting sounds and confirmed oscillator configurations, Volume25%, I2S/HUSB
ready, one keyboard and no dropped events/faults. Restored Init Square and
silence. Capture: p4-sounds-2026-10-04.txt. The new physical menu interactions
and audible preset quality have not yet been confirmed by the user.

## V1 three-note polyphony

Active P4 now has three independent note/envelope voices, each with three
oscillators. Fourth-note allocation prefers a released voice, then steals the
oldest held note. Stolen keys do not resume. Mix uses fixed divide-by-three
headroom: a solo note is quieter than the previous mono test; chord peak limits
remain7200 at100% master and3600 at50%. See [v1 release](v1-release.md)
for behavior, verification and remaining hardware limitations.
