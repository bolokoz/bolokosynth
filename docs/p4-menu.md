# P4 synth menu and MIDI Learn

This guide describes the implemented controls in `p4-bringup/`. The P4 audio/MIDI path has been verified with the MAX98357A. The user confirmed the Envelope screen, encoder rotation, and encoder press work. Live MIDI Learn still needs a hands-on check. Display investigation is paused at the user's request; see [saved TFT findings](tft-investigation.md). Display compatibility is under investigation: INVON white was reported black, while INVOFF white was reported evenly gray. Seller identifies ST7789; the actual chip has not been read. Console b runs a steady white-screen test for 30 seconds; i toggles inversion. Both run in the UI task. Use the [current P4 wiring table](display-encoder.md).

## Power and wiring reminder

TFT **VCC now connects directly to the P4 3.3V pin**; the successful brightness test removed MAX SD/EN from the shared branch. MAX **VIN uses 5V**, and both modules share GND. TFT **BLK can remain open** by the seller schematic. A BLK-to-3.3V test made the user report a dark screen; it has been reverted. Keep BLK open while investigating brightness. Keep MAX audio on IO11/12/13, TFT on IO9/10/14/15/16, and encoder/buttons on IO18/19/20/21.

## Navigation

The screen starts on **Sounds**, with Preset selected. It now uses a white
background, black text and light gray cards. The large EC11 provides rotation
and PUSH. The smaller control's exposed KEY0 signal is its press switch.

**Tap the smaller control to open Volume in EDIT, then turn the large knob
to change volume. Tap the smaller control again to open the main menu.**
Only KEY0 is exposed for the smaller control in the supplied pinout; its
physical rotation cannot be decoded through that single button signal.

| Where you are | Turn the large knob | Tap PUSH | Tap KEY0 |
|---|---|---|---|
| Parameter page, BROWSE | Select a parameter | Enter EDIT | Open Volume editing |
| Parameter page, EDIT | Change the selected value | Finish editing | Open Volume editing |
| Volume shortcut | Change Volume while editing | Finish/resume editing | Open main menu |
| Main menu | Choose Envelope, Sounds, Output, OSC1/2/3, or MIDI | Open chosen page | Open Volume editing |
| MIDI Control | Choose any of the 18 parameters | Start MIDI Learn | Open Volume editing |
| MIDI Learn active | Choose the MIDI message mode | No action | Cancel Learn |

Values change immediately in EDIT; PUSH finishes editing without a separate save.
Hold PUSH about700ms on a parameter page to Learn its selected control.
Hold PUSH on MIDI Control to change the binding mode.
Hold KEY0 about one second from any page to silence all notes and cancel Learn.
To reach another page, tap KEY0 to open Volume and again to open the main menu,
then turn the large encoder and press PUSH. Initial volume is now50%.

## Three-oscillator sounds

The firmware now starts on Sounds. It adds eight presets and three oscillator
pages; see [sounds and navigation](p4-sounds.md). The Output waveform controls
oscillator 1, while Volume controls the full mix. The original Envelope and
MIDI Learn controls below remain available.

## Parameters

| Parameter | Range | Startup value | What it changes |
|---|---|---|---|
| Attack | 0–2000 ms | 5 ms | Time to reach the initial peak after pressing a key |
| Decay | 0–3000 ms | 50 ms | Time to fall from the peak to Sustain |
| Sustain | 0–100% | 70% | Envelope level while a key remains held |
| Release | 0–5000 ms | 200 ms | Fade to silence after the last held key is released |
| Waveform | Square, Saw, Triangle, Sine | Square | Oscillator shape |
| Volume | 0–100% | 50% | Output gain, also multiplied by key velocity and ADSR |

The envelope graph uses log-scaled widths for positive stage times. Attack,
Decay or Release at0ms occupy no horizontal distance and display a vertical
level transition; all-zero times are handled without division by zero.

Time controls use a nonlinear range so short times have finer adjustment. Sustain and Volume move by approximately one percentage point per encoder detent. Waveform cycles through the four shapes.

The synth starts silent with volume at 50% for this initial test phase. It plays one note at a time with the most recently held note taking priority. Releasing that key returns to another held key, or starts Release when none remain. A longer Release setting deliberately leaves a longer tail.

## MIDI Learn

The recognized MiniLab MkII now loads [default controller assignments](midi-controller-profiles.md). Other controllers start unassigned. Select a parameter, start Learn, choose the mode matching the keyboard knob, and turn that knob. The first valid CC message captures its **channel and CC number** without changing the parameter. Subsequent movement controls the parameter.

Bindings distinguish MIDI channels. Ordinary CC numbers **0–119** can be assigned; channel-mode CC numbers **120–127** cannot. Assigning the same channel/CC to another parameter moves the binding from its previous parameter. Cancelling Learn preserves any existing binding.

| Screen mode | Knob encoding | Example movement |
|---|---|---|
| `ABS` | Absolute position, 0–127 | Values span the parameter range |
| `REL OFFSET` | Relative delta around 64 | 63 decreases; 65 increases |
| `REL 2'S` | Relative two's-complement delta | 127 decreases; 1 increases |
| `REL 16` | Relative delta around 16 | 15 decreases; 17 increases |
| `REL SIGN` | Sign magnitude | 65 decreases; 1 increases |

Relative modes apply movement directly and ignore neutral messages. These include the zero messages sent between relative movements by MiniLab MkII. The MiniLab factory profile supplies mode hints during Learn; explicitly cycling the mode overrides the hint. Custom keyboard memories can require another mode. The first three relative encodings correspond to the modes in the [Arturia MiniLab MkII manual, section 4.8.4](https://downloads.arturia.com/products/minilab-mkII/manual/MiniLabmkII_Manual_1_0_7_EN.pdf).

Absolute knobs use **pickup**. After learning, move the knob until its position matches or crosses the current parameter value. The screen shows a pickup reminder until then, keeping the parameter from jumping to an unrelated knob position. Editing the parameter with the local encoder rearms pickup. For Waveform, entering the currently selected waveform's value range also satisfies pickup.

The MIDI page shows the selected parameter's value, binding, channel, and mode. Holding PUSH changes the selected existing binding mode immediately while retaining its channel/CC. For an unassigned parameter or during Learn it chooses the upcoming assignment mode. See [sliders and knob troubleshooting](p4-midi-sliders.md).

## Settings and recovery

Settings and MIDI bindings are currently **RAM only**. Resetting or powering off restores sound defaults and the recognized controller profile; custom Learn assignments are lost. There is no flash-save action in this version.

Hold KEY0 to clear stuck notes. USB disconnect and a MIDI queue overflow also clear held notes. MIDI CC123 releases notes on its channel; CC120 silences that channel immediately. Console `p` provides the same global panic/silence action.

The top status line shows audio readiness, USB attachment, and the number of held notes. The onboard RGB LED also reports MAX/TFT verification: two amber flashes for MAX and three purple for TFT, dim when unverified and bright for a fault or missing enabled presence wire. See [module diagnostics](module-diagnostics.md) for limits, confirmation and optional unplug sensing.

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

## V1 three-note polyphony

Active P4 now has three independent note/envelope voices, each with three
oscillators. Fourth-note allocation prefers a released voice, then steals the
oldest held note. Stolen keys do not resume. Mix uses fixed divide-by-three
headroom: a solo note is quieter than the previous mono test; chord peak limits
remain7200 at100% master and3600 at50%. See [v1 release](v1-release.md)
for behavior, verification and remaining hardware limitations.
