#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the same real static BusyBox binary in Trames and QEMU."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def execute(command, cwd, data=b""):
    result = subprocess.run(command, input=data, capture_output=True, cwd=cwd,
                            env={"PATH": "/usr/bin:/bin", "LANG": "C"}, timeout=15)
    return result.returncode, result.stdout, result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", default=str(ROOT / "target/debug/trames-run"))
    parser.add_argument("--busybox", default=str(ROOT / "target/busybox-build/busybox"))
    args = parser.parse_args()
    runner = str(Path(args.runner).resolve())
    busybox = str(Path(args.busybox).resolve())
    qemu = os.environ.get("QEMU_AARCH64", "qemu-aarch64")
    data = b"alpha beta\nsecond line\nthird\nlast\n"
    cases = [
        (["true"], b""), (["false"], b""),
        (["echo", "Hello", "BusyBox"], b""),
        (["echo", "-e", "a\\tb\\n"], b""),
        (["printf", "%s:%d\\n", "value", "37"], b""),
        (["basename", "/data/thing.txt", ".txt"], b""),
        (["dirname", "/data/thing.txt"], b""),
        (["cat"], b"binary\0\xff\n"),
        (["cat", "data/input.txt"], b""),
        (["cat", "data/missing"], b""),
        (["wc", "-l", "data/input.txt"], b""),
        (["wc", "-c"], b"a\0b\n"),
        (["head", "-n", "2", "data/input.txt"], b""),
        (["tail", "-n", "2", "data/input.txt"], b""),
        (["pwd"], b""), (["ls", "data"], b""), (["ls", "-a", "data"], b""),
        (["ls", "data/input.txt"], b""),
    ]
    with tempfile.TemporaryDirectory(prefix="trames-busybox-") as directory:
        folder = Path(directory); (folder / "data").mkdir()
        source = folder / "data/input.txt"; source.write_bytes(data)
        (folder / "data/second.bin").write_bytes(b"\0\xff")
        imports = ["--file", f"/data/input.txt={source}", "--file", f"/data/second.bin={folder / 'data/second.bin'}"]
        for guest_args, stdin in cases:
            actual = execute([runner, "--memory", *imports, busybox, *guest_args], folder, stdin)
            reference = execute([qemu, busybox, *guest_args], folder, stdin)
            expected_code = 1 if guest_args in [["false"], ["cat", "data/missing"]] else 0
            if reference[0] != expected_code:
                raise AssertionError(f"reference BusyBox failed for {guest_args!r}: {reference!r}")
            # A virtual root has its own cwd; normalize this one known host prefix.
            if guest_args == ["pwd"]:
                reference = (reference[0], reference[1].replace(str(folder).encode() + b"\n", b"/\n"), reference[2])
            if actual != reference:
                raise AssertionError(f"{guest_args!r}: Trames {actual!r} != QEMU {reference!r}")
            print(f"PASS {' '.join(guest_args)}: exit/stdout/stderr matched", flush=True)
        # Guest absolute paths cannot acquire host file access by naming it.
        host_only = folder / "host-only"; host_only.write_bytes(b"must stay outside guest")
        code, out, err = execute([runner, "--memory", busybox, "cat", str(host_only)], folder)
        if code != 1 or out or b"No such file" not in err:
            raise AssertionError("guest acquired a host path without explicit import")
        print("PASS host path is unavailable without explicit file import", flush=True)
    print(f"BusyBox integration passed: {len(cases)} QEMU comparisons, 1 host-scope check")


if __name__ == "__main__":
    main()
