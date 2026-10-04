# Proposed power distribution — not yet installed

User requests more power connection points for ESP32-P4, MAX98357A, TFT and
MiniLab MkII. Current TFT direct3.3V/GND wiring improved brightness; voltage
drop/source capacity remains unmeasured.

Recommended solder-free prototype: separate 5-conductor lever splicing
connectors (WAGO221-415 or a verified equivalent), one for5V, one forGND,
optionally one for3.3V expansion. All entries inside each connector are
electrically common: never put5V andGND in the same connector.

Use one regulated5V source, with3A a preliminary combined-system target,
not a measured budget. Higher current rating supplies headroom; devices
draw only their required current. MAX can require roughly650mA near full
output per Adafruit; ESP32, display and keyboard require additional margin.

5V bus: source, P4 approved5V input/USB power path, MAX VIN, keyboard through
the existing injection cable. Ground bus: source, P4, MAX, TFT, injection.
3.3V stays supplied by P4 regulatedoutput to TFT VCC, retaining direct short
wiring that worked. Optional expansion connector can split this later.
Leave TFT BLKopen and keep MAX SD/EN at the working diagnostic arrangement.

Shopping terms for AliExpress (seller authenticity/price not verified):
- WAGO221-415 5 port lever connector (two; third optional)
- regulated5V3A power supply, correct local plug
- USB power screw terminal adapter, matching actual cable, rated>=3A
- 20–22AWG copper stranded power wire
Alternative: passive2-bus DC power-distribution PCB,5V-compatible,>=3A;
verify rail topology. Dupont wires remain for logic, not main current paths.

Do not combine independent laptop/charger5V outputs in the distribution bus.
Upload wiring must retain one selected5V source or isolate the other source's
power while preserving required data/ground. This is a proposed architecture,
not a reviewed cable schematic; identify adapter pins before wiring.
No external3.3V supply into the P4's3.3V output. No5V on TFT VCC/GPIO.

Sources:
https://www.wago.com/us/wire-splicing-connectors/compact-splicing-connector/p/221-415
https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts
https://wiki.wireless-tag.com/docs/en/WT9932P4-TINY/board_features.html
