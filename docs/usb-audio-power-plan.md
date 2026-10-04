# USB-only audio power target

Updated 2026-10-04. User wants one USB cable supplying the complete synth
from a wall USB adapter, car USB port, or power bank, with small-JBL-like
loudness. Higher-voltage wall-only amplification is not the selected direction.

## Recommended first build

Keep the MAX98357A and buy one efficient 4-ohm full-range speaker, approximately
3 inches, rated 5–10W, with an appropriately designed enclosure. MAX output at
5V is about 3W maximum into 4 ohms; the manufacturer's 3.2W figure is at 10%
THD. A speaker's 5–10W rating is handling capacity, not power consumption or
the power this amplifier supplies. Speaker sensitivity, enclosure and bass
processing determine perceived sound as well as electrical watts.

Use a regulated 5V source. A 3A source is the preliminary target for the whole
system; 2A may suffice at a lower/verified load, but consumption is unmeasured.
Do not promise full output on every car or laptop USB port. Some only provide
500mA/1A, and advertised multi-port charger capacity may be shared.

Power distribution: one 5V input with separate short branches to P4 power,
MAX VIN and existing keyboard injector; common ground with separate returns.
TFT remains on direct P4-regulated 3.3V, BLKopen. Do not raise TFT VCC to 5V
with the connected encoder/button pullups feeding P4 GPIO.

## Output options

- Current mono MAX: one speaker, up to roughly 3W electrical audio output.
- Two MAX boards: two speakers, approximately 3W per channel (6W total max).
  Requires channel selection, stereo processing for actual spatial sound,
  and sufficient USB source/cable current. This does not make one speaker 6W.
- Higher mono power while retaining 5V USB input: boost conversion and a
  suitable amplifier, or an integrated boosted amplifier, with power-budget
  checks. Extra watts cannot exceed input power less system load/losses.
  No boost/alternate amplifier module selected or purchased.
- USB-PD can provide higher voltage/power with compatible sources but cannot
  be assumed available on arbitrary car USB ports/power banks. Generic 5V is
  the compatibility baseline; no 10–20W-output promise is made for all sources.

Previous JBL references: Go4 4.2W RMS, Clip5 7W, Flip7 25W woofer+10W tweeter.
These electrical ratings do not directly establish equal loudness. Firmware's
test signal amplitude is still below full scale (now3600, previously900, on a signed16-bit scale), so
the current prototype has not exercised MAX full output.

## Volume change

User requested startup volume 50%, replacing25%. SynthConfig DEFAULT_VOLUME
is now0.50. Existing controls, sound-output bounds and slider pickup tests
updated; ADSR/controls/sounds/sliders/profile tests pass. P4 firmware built.
Uploaded after FUSB reconnection; flash hashes verified. Live serial confirms
Volume50%, I2S/HUSB ready, MiniLab detected, fault0 and no dropped events.
Capture: p4-volume-50-2026-10-04.txt.
Subsequently, user requested more loudness: signal amplitude900->3600,
master retained50%. Output bounds are now1800 at50% and3600 at100%,
rather than450/900. Presets preserve master volume; MIDI/local volume remain adjustable.

Sources:
- https://www.analog.com/en/products/max98357a.html
- https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts
- https://www.jbl.com/CLIP-5.html
- https://www.jbl.com/FLIP-7.html

## Louder signal physical result

User confirms TFT does not dim and audio has no distortion with amplitude3600,
master50%. Loudness is still too low. Speaker is approximately1-inch diameter
and8ohms by user report; model, power handling and sensitivity are unknown.
This verifies present power/display stability at the tested level, not at
the amplifier's full power. Current digital peak ceiling at50% is1800/32767;
sustain/velocity can lower it further. No additional gain increase made yet.
Next practical check: knob16 to100% after pickup (double signal relative50%),
and read any printed W rating before selecting stronger output/speaker.
At5V/8ohms, Adafruit reports1.4W at1%THD or1.8W at10%THD;4ohms permits more.

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
