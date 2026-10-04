# MAX and TFT status diagnostics

The existing MAX98357A connection is output-only I2S. The TFT header exposes
write-only SPI, with no MISO/readback wire. Neither connection acknowledges a
working module. A successful I2S/SPI initialization cannot prove that a module
is present, powered, or wired correctly. The firmware therefore starts both
modules as **unverified**, even when they are working.

## RGB blink codes

| Pattern | Meaning |
|---|---|
| Two dim amber flashes | MAX/audio has not been verified this boot |
| Three dim purple flashes | TFT has not been verified this boot |
| Two bright amber flashes, repeating | I2S initialization/write fault, user-reported MAX problem, or missing enabled MAX presence wire |
| Three bright purple flashes, repeating | UI task allocation/stall fault, user-reported TFT problem, or missing enabled TFT presence wire |
| Rapid red flashing | Other USB/queue firmware error |
| Green pulse | USB device attached; main loop alive |
| Blue | Recent MIDI activity |
| Slow amber blink | Waiting for USB attachment |

Unverified codes occupy the first four seconds of an eight-second cycle;
USB/MIDI indications occupy the remaining four. A single module fault repeats
every two seconds. When both modules have faults their codes alternate.
Fault codes take priority over MIDI activity. The dim codes are reminders of
missing verification, not proof of disconnected hardware.

The TFT check watches a three-second UI task heartbeat. It cannot detect a
blank, dim, miswired or physically absent LCD if SPI calls still complete.
The MAX check catches I2S initialization and short-write failures; an absent
MAX usually does not cause either failure.

## Console confirmation and reported problems

Use the FUSB serial console at 115200 baud:

| Command | Action |
|---|---|
| A | Confirm that you heard normal audio from the MAX |
| F | Confirm that the TFT menu and controls work |
| 1 / 2 | Report a MAX / TFT problem and show its bright blink code |
| ! | Clear confirmations and user-reported problems |
| s | Print module states and firmware status |
| H / h | Enable / disable optional presence monitoring |

Confirmation is a human observation, not automatic electrical detection.
It lasts until restart, clearing, enabling/disabling presence monitoring, or a
detected presence-wire disconnect. Confirmation is rejected while the relevant
software error or missing/unstable enabled presence connection exists.
Clearing user reports does not clear real software errors.

## Optional unplug detection

No extra wires are needed for the current firmware. Presence monitoring is
**disabled by default**, and IO22/IO23 are untouched until explicitly enabled.

For connector/ground-return sensing, with power disconnected:

| ESP32-P4 header | Extra wire to module |
|---|---|
| IO22 | MAX board GND |
| IO23 | TFT/encoder board GND |

Keep the existing module power and GND connections. Add these as separate
return wires to the module GND, ideally through the same removable connector.
Do not connect either sense input to 5V, SD/EN, BLK, or a data signal.
The manufacturer [WT9932P4-TINY V1.3 schematic](https://res.8ms.xyz/wiki_assets/WT9932P4-TINY/WT9932P4-TINY_SCH_V1.3.pdf)
shows IO22 and IO23 on the headers; neither is assigned to this project's audio,
display, encoder or USB wiring.

After installing both optional wires, send H. The pins use input pull-ups;
200 ms debounce filters brief changes. Low means ground-return present;
high means presence wire missing. Reconnection returns to unverified and
requires a new human confirmation. Send h to disable monitoring.

This proves only the extra ground-return path. It does not verify module
power, every connector pin, audio, pixel output, or backlight. A sense wire
that remains grounded after a module disconnect cannot detect that disconnect.
Monitoring and confirmations are RAM-only; monitoring must be enabled again
after restart.

## Validation

Host tests cover default unknown states, confirmation rejection, reported
faults, presence debounce, disconnect/reconnect, timer wraparound, blink counts,
brightness, and priority over MIDI. Hardware presence wiring and actual
physical unplugging remain untested. No display experiment or setting change
is part of this feature; startup volume remains 25%.

Build and upload succeeded. Live serial separately verified both reported
fault paths and their clearing, then restored default unverified states with
presence monitoring disabled. I2S/HUSB remained ready with one MIDI keyboard
and no dropped events; Volume25%. Capture: p4-module-status-2026-10-04.txt.
LED timing/color tests passed in software; physical LED appearance and unplug
sensing have not been checked by a person.
