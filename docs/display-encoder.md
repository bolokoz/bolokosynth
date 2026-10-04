# 2.4-inch TFT + EC11 wiring for WT9932P4-TINY

The current firmware is `p4-bringup/` for the **Wireless-Tag WT9932P4-TINY ESP32-P4**. It configures a 2.4-inch ST7789 display in 320 × 240 landscape orientation, an EC11 rotary encoder, its push switch, and the separate KO/KEY0 button. The user confirmed the Envelope screen, encoder rotation, and encoder press work. Direct TFT supply wiring fixed the reported backlight dimness; tone-dependent brightness stability remains to check.

## Purchased module and seller screenshots

[Purchased module listing, product 1005008766561044](https://pt.aliexpress.com/item/1005008766561044.html).

The seller screenshots supplied in chat identify the purchased **2.4-inch ST7789, 320 × 240** version and show VCC marked **3V/5V**. The 1.8-inch option in the same listing uses a different controller and resolution.

Use **3.3V for this setup**. The seller input schematic shows encoder/button pull-ups to VCC, so powering the module at 3.3V keeps those signals within the ESP32's 3.3V logic range.

The seller also states that the backlight is fully enabled by default and **BLK may be left unconnected**. Its schematic shows an SS8050 transistor driver with a 10k pull-up to VCC and a 1k base resistor. The current firmware does not drive BLK. A direct BLK-to-3.3V test produced a reported dark screen and was reverted; do not treat that test as a confirmed brightness fix.

## Current P4 connections

Disconnect USB and MIDI injector power before changing wires. Header labels such as `IO9` mean **GPIO9**, not the ninth physical header position.

| TFT/encoder label | WT9932P4-TINY connection | Function |
|---|---|---|
| `VCC` / `VDD` | **3.3V power pin** | Module power and input pull-ups |
| `GND` | **GND** | Shared ground |
| `SCL` / `DCL` | **IO9** | SPI clock / SCK |
| `SDA` | **IO10** | SPI data / MOSI |
| `CS` | **IO14** | Display chip select |
| `DC` | **IO15** | Data/command select |
| `RES` / `RST` | **IO16** | Display reset |
| `A` / `TRA` | **IO18** | Encoder phase A |
| `B` / `TRB` | **IO19** | Encoder phase B |
| `PUSH` / `PSH` | **IO20** | Pressing the encoder knob |
| `KO` / `KEY0` / `KD` | **IO21** | Separate BACK button |
| `BLK` | **Leave open** | Module backlight control |

Earlier handwritten transcriptions used `DCL` for the clock label and `KD` for the separate button. These refer to the seller's SPI clock and KO/KEY0 functions. In the earlier reported rear-header order, the pins were:

`KD`, `PUSH`, `B`, `A`, `BLK`, `CS`, `DC`, `RES`, `SDA`, `DCL`, `VCC`, `GND`.

`SDA` here is SPI MOSI, not an I²C data connection. This display is write-only; it does not need a MISO wire. `RES` connects to **IO16**, not the P4 board's system RESET pin.

## Shared power with the MAX98357A

The TFT and amplifier use the same P4 power rails and ground:

| P4 connection | Connected destinations |
|---|---|
| **3.3V** | TFT `VCC` directly, with a short wire |
| **5V** | MAX `VIN` / `VCC` |
| **GND** | TFT `GND` **and** MAX `GND` |

**Updated after the successful brightness test:** direct TFT 3.3V/GND wiring
with MAX SD/EN removed from the shared branch made the screen work.
Latest firmware configures **MAX SD/EN onIO17**: LOW until I2S initializes,
then HIGH. Move SD/EN there with all power disconnected; do not also connect
it to3.3V. Physical move is awaiting user confirmation.
SD/EN is not the amplifier power input. MAX VIN stays on5V. Give the TFT
and amplifier separate short power/ground branches back to the P4 pins.
Tone-dependent brightness changes suggest supply/contact/return voltage
drop but have not been measured. See [backlight findings](tft-investigation.md).

Audio keeps its existing pins: MAX **BCLK → IO11**, **LRC → IO12**, **DIN → IO13**. These do not overlap the display or encoder signals. The onboard RGB status LED uses GPIO51. Keep the speaker across the MAX's speaker +/− terminals; neither speaker terminal connects to ESP32 ground.

The laptop remains on **FUSB**. The externally powered MIDI keyboard data connection remains on **HUSB**. See [P4 audio and USB wiring](p4-tone-test.md) and [menu controls](p4-menu.md).

## First display check

After wiring with power off, reconnect power. The firmware should open the **Envelope** page. Confirm text/graph visibility, then turn the knob, tap PUSH, and tap KO/KEY0. A successful firmware display initialization does not prove that the panel is connected or showing pixels; the user has confirmed the screen and encoder press/rotation work. The separate BACK switch and backlight brightness remain to be checked.

## Legacy ESP32-S3 mapping

This is the earlier S3 proposal, retained only for reference. It is **not** the current P4 wiring.

| Module signal | Earlier ESP32-S3 GPIO |
|---|---:|
| Clock / SCL | 4 |
| SDA / MOSI | 5 |
| CS | 6 |
| DC | 7 |
| RES | 8 |
| A | 14 |
| B | 15 |
| PUSH | 16 |
| KO / KEY0 | 17 |

The earlier optional BLK GPIO18 proposal is superseded by leaving BLK open for the seller-documented module. S3 audio used GPIO11–13; its startup issue is documented separately in [reset-startup.md](reset-startup.md).

## Sources

- [Seller listing](https://pt.aliexpress.com/item/1005008766561044.html) and its pinout, input, and backlight circuit screenshots supplied in chat.
- [WT9932P4-TINY manufacturer schematic](https://res.8ms.xyz/wiki_assets/WT9932P4-TINY/WT9932P4-TINY_SCH_V1.3.pdf).
- Firmware pin constants in `p4-bringup/src/SynthUi.h` and `p4-bringup/src/main.cpp`.

## Small control live check (2026-10-04)

User rotated the small control, then pressed/released it during an active
60-second input trace. Only KEY0 (IO21) changed; A/B/PUSH stayed high and no
rotary UI action appeared. Press correctly selected Volume. With the connected
header, the smaller control supplies a usable press signal only. Its possible
unconnected rotation signals remain unidentified. Console e repeats this
read-only trace. Capture: p4-small-control-trace-2026-10-04.txt.
