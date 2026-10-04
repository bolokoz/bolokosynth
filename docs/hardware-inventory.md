# Owned audio hardware and loudness plan

Updated 2026-10-04. Purchase identities and selected variants below are user-reported. AliExpress pages could not be retrieved during this check; exact PCB circuitry, chip markings, speaker sensitivity and power ratings have not been measured. Seller-generated AI summaries are not electrical specifications.

## Inventory

| Part | Reported purchase / variant | Role and limits |
| --- | --- | --- |
| [AIYIMA round speakers](https://www.aliexpress.com/item/32790211440.html) | One order of a 2-piece pack; 40 mm / 1.5 inch, 4Ω, 3W | User identifies this as the current speaker. Matches one MAX output at 5V. This replaces the earlier visual estimate of 1 inch / 8Ω. |
| [Notebook cavity speakers](https://pt.aliexpress.com/item/1005005699882165.html) | Two pieces; selected **8 ohms 2.0 terminal**; listing rates 8Ω version at 2W | Already have small enclosures. 2.0 refers to connector pitch, not wattage. Other listing variants are 4Ω/3W and 1.25 mm terminals; those are not the reported selection. |
| [TENSTAR MAX98357 breakout](https://pt.aliexpress.com/item/1005006382608935.html) | Selected **MAX98357 BGA** | Current mono I2S DAC + power amplifier. Exact clone schematic is unverified. Manufacturer family operates at 2.5–5.5V; ignore the listing AI claim of a 1V supply. |
| [GY-PCM5102 / PCM5102A](https://pt.aliexpress.com/item/1005006104038963.html) | I2S stereo DAC module, seller describes a 5V-powered board | Resolves the previous “PAM5102” name. Analog line output, not a passive-speaker amplifier. Chip supply and board input supply differ; verify module labels/jumpers before wiring. |
| [Bluetooth + PAM8403 board](https://pt.aliexpress.com/item/1005010789710222.html) | Seller describes Bluetooth 5.0, DC5V, 2×5W | PAM8403 is a stereo analog power amplifier. Actual chip specs are about 2.5W/channel at 1% THD into 4Ω at 5V, or 1.4W/channel into 8Ω. Analog AUX/input availability and exact installed chip remain unverified. |

No alternate audio board is installed yet. No new speaker purchase is needed for the next test.

## First solution: improve the existing MAX setup

Keep the current 4Ω/3W round speaker connected between MAX speaker + and −. MAX VIN needs a solid 5V branch; SD/EN is a logic signal, currently assigned GPIO17. Keep separate power branches for TFT and amplifier. Speaker terminals must not be grounded.

Before this update, normalized oscillator output was limited to 3600 counts at 100% volume, or 1800 counts at 50% on a signed 16-bit scale. That 50% ceiling is only about 5.5% of digital full scale; it does not mean half of the amplifier's rated watts. ADSR sustain and key velocity can reduce it further.

The next controlled test doubles signal amplitude to **7200**, keeping **50% startup volume**. Peak bounds are 3600 at startup and 7200 at 100% volume, before envelope/velocity reductions. This is approximately +6 dB electrical signal level, not a measured SPL increase or amplifier power result. Do not assume this reaches rated amplifier power. Reduce master volume if distortion, rattling, supply resets or display dimming appears.

For this MAX family, published 5V output is about 2.5W into 4Ω at 1% THD, versus 1.4W into 8Ω. The often-advertised 3W is at 10% distortion. Thus the now-identified 4Ω speaker is already a suitable electrical match.

A bare round speaker benefits from a baffle/enclosure separating front and rear radiation. This can improve bass; actual result depends on enclosure and driver specifications, which are unavailable. A 3W handling rating alone says nothing about acoustic loudness.

## Use the other speakers without another amplifier

A matched pair of the reported **8Ω/2W notebook speakers can be connected in parallel** to one MAX, giving a nominal 4Ω load: both speaker positives to MAX +, both negatives to MAX −. Each shares approximately half the total amplifier power. Power down before changing wiring.

This remains mono and does not double amplifier watts. Compare its acoustic result against the round speaker; sensitivity is unknown. Do not parallel two 4Ω speakers or mix one 4Ω and one 8Ω speaker on this MAX: those loads are below 4Ω. Never join separate amplifier outputs.

## Stereo option using existing PCM5102A + PAM8403

Conditional path:

ESP32 I2S → PCM5102A line L/R → PAM8403 analog inputs → one speaker per amplifier channel.

This works only if the Bluetooth amplifier exposes a usable analog input (AUX or IN L/R/GND) and supports selecting it without contention from its Bluetooth receiver. User has been asked for printed input labels; no photos are required. No board-specific wiring is prescribed yet. Use a direct analog path for live instrument audio.

With two 4Ω AIYIMA speakers this could provide roughly 2×2.5W at 1% distortion on 5V; two 8Ω notebook speakers give roughly 2×1.4W. It offers two channels, not more power into a single driver. Current firmware duplicates mono into left/right; genuine stereo synthesis is a separate future feature.

PCM5102A outputs are line-level (2.1Vrms full-scale chip specification, designed for loads at least 1kΩ), so they cannot directly drive these speakers. Its maximum line level may overload the PAM input; begin with reduced source volume. MAX accepts digital I2S, not PCM analog output. MAX speaker output must not feed PAM analog inputs.

## USB power and expectation

Continue with one regulated 5V USB source, separate 5V/GND distribution branches and sufficiently short/thick power wiring. A 5V/3A source is a preliminary whole-system target, not a measured consumption budget. Available output from weak car/laptop USB ports may be lower. Distribution connectors add connection points, not power.

Try existing 4Ω speakers and increased digital level before buying a stronger amplifier. These small drivers may remain limited in bass and acoustic output compared with a finished JBL enclosure. No JBL-equivalent loudness has been demonstrated.

## Primary references

- [MAX98357A manufacturer](https://www.analog.com/en/products/max98357a.html)
- [MAX output/load and gain guide](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts) — Adafruit board wiring is not proof of the TENSTAR clone's exact circuitry.
- [PCM5102A manufacturer](https://www.ti.com/product/PCM5102A)
- [PAM8403 manufacturer datasheet](https://www.diodes.com/datasheet/download/PAM8403.pdf), electrical characteristics page 4.

### Validation of the 7200-level update

Existing native ADSR, three-oscillator sounds, slider, controls and MiniLab
profile tests passed with strict compiler warnings. P4 build/upload succeeded;
flash hashes verified. Live serial confirms50% volume, I2S/HUSB ready,
MiniLabVID1C75/PID0289, fault0, no dropped MIDI/UI events, GPIO17 HIGH.
Capture: [p4-louder-7200-2026-10-04.txt](p4-louder-7200-2026-10-04.txt).
This verifies firmware operation, not acoustic output, wiring or supply voltage.
Listening check and Bluetooth-board input labels remain pending.
