#!/usr/bin/env python3
"""Verify the iPad CoreMark C port on the development host; never an iPad score."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default="cc")
parser.add_argument("--timed", action="store_true", help="Also run upstream auto-calibrated performance and validation")
args = parser.parse_args()
package = ROOT / "apps/ios/NativeCoreMark"
source = package / "Sources/CCoreMark"
manifest = json.loads((package / "upstream.json").read_text())
for name, digest in manifest["files"].items():
    data = (source / Path(name).name).read_bytes()
    if name == "posix/core_portme.c":
        data = data.replace(b"clock_gettime(CLOCK_MONOTONIC, _t)", b"clock_gettime(CLOCK_REALTIME, _t)")
    assert hashlib.sha256(data).hexdigest() == digest, name
out = ROOT / "target/native-coremark"
out.mkdir(parents=True, exist_ok=True)
library = out / "libcoremark-probe.so"
subprocess.run([args.cc, "-shared", "-fPIC", "-O2", "-fno-lto", "-pthread",
                "-Dmain=trames_coremark_main", *map(str, sorted(source.glob("*.c"))),
                "-o", str(library)], check=True)
native = ctypes.CDLL(str(library))
run = native.trames_coremark_run
run.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
run.restype = ctypes.c_int
buffer = ctypes.create_string_buffer(16384)
assert run(1, 0, None, 0) == -1
assert run(100000001, 0, buffer, len(buffer)) == -1
short = ctypes.create_string_buffer(2)
assert run(1, 0, short, len(short)) == -3
assert short.raw[-1] == 0
for validation in [False, True]:
    assert run(1, validation, buffer, len(buffer)) == 0
    text = buffer.value.decode()
    assert "Must execute for at least 10 secs" in text
    assert "Correct operation validated" not in text
    assert not any(f"ERROR! {algorithm} crc" in text for algorithm in ["list", "matrix", "state"]), text
    assert ("0x18f2" if validation else "0xe9f5") in text
    assert "-O2 -fno-lto" in text
    if args.timed:
        assert run(0, validation, buffer, len(buffer)) == 0
        text = buffer.value.decode()
        (out / ("validation.log" if validation else "performance.log")).write_text(text)
        assert "Correct operation validated" in text, text
        print(text, flush=True)
print("Native CoreMark source identity, bounded API, CRCs and minimum-time rejection passed.")
