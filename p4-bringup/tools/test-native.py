"""Compile/run all portable synth tests; requires clang++ or CXX.

Temporary binaries stay outside the repository. Assertions remain enabled;
strict warnings catch interface drift. These tests do not replace hardware
listening, MIDI USB enumeration or TFT visual checks.
"""
import os
from pathlib import Path
import subprocess
import tempfile

tests = Path(__file__).resolve().parent.parent / "tests"
with tempfile.TemporaryDirectory(prefix="bolokosynth-tests-") as temporary:
    for source in sorted(tests.glob("*_test.cpp")):
        executable = Path(temporary) / source.stem
        subprocess.run([os.environ.get("CXX", "clang++"), "-std=c++17",
                        "-Wall", "-Wextra", "-Werror", str(source),
                        "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
