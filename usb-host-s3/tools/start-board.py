"""Start the tested ESP32-S3 from ROM download mode without rewriting flash.

Uses no-reset before connecting and watchdog-reset afterward: RTS reset did
not reliably start this board from its serial connector. This is a workaround,
not a hardware repair. Discovery requires exactly one Espressif native port;
use --port when other boards are connected. Requires PlatformIO pyserial and
esptool, and deliberately checks the esp32s3 chip identity.
"""
import argparse
import subprocess
import sys

try:
    from serial.tools.list_ports import comports
except ImportError:
    raise SystemExit("Run this script with PlatformIO's Python: ~/.platformio/penv/bin/python")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--port", help="Serial port; otherwise find one Espressif native USB device.")
args = parser.parse_args()
port = args.port
if not port:
    candidates = [p.device for p in comports() if (p.vid, p.pid) == (0x303A, 0x1001)]
    if len(candidates) != 1:
        raise SystemExit("Expected one Espressif USB port; specify --port explicitly.")
    port = candidates[0]

# Connect only to an existing bootloader; avoid putting a running app into download.
# Read flash identity, then use the proven watchdog reset instead of RTS hard-reset.
command = [
    sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
    "--connect-attempts", "2", "--before", "no-reset", "--after", "watchdog-reset",
    "--no-stub", "flash-id",
]
raise SystemExit(subprocess.run(command).returncode)
