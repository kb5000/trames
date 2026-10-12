#!/usr/bin/env python3
"""Compare real executions, including binary register/memory state snapshots."""
import argparse
import os
from pathlib import Path
import random
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RUNNER = os.getenv("TRAMES_RUNNER", str(ROOT / "target/debug/trames-run"))


def execute(command):
    result = subprocess.run(command, capture_output=True, timeout=10)
    return result.returncode, result.stdout, result.stderr


def compare(runner, qemu, binary, args=(), expected=None):
    actual = execute([runner, "--memory", str(binary), *args])
    reference = execute([qemu, str(binary), *args])
    if actual != reference:
        raise AssertionError(f"{binary.name}: Trames {actual!r} != QEMU {reference!r}")
    if expected is not None and reference != expected:
        raise AssertionError(f"{binary.name}: fixture itself failed: {reference!r}")
    return len(reference[1])


def generated(seed):
    rng = random.Random(seed)
    lines = [".global _start", ".text", "_start:"]
    for register in range(8):
        for index in range(4):
            lines.append(f"{'movz' if index == 0 else 'movk'} x{register}, #{rng.randrange(65536)}, lsl #{16 * index}")
    lines.append("cmp xzr, xzr")  # Explicit flags: Linux process entry does not define NZCV.
    # Independently assembled instruction encodings, reproducible operands and shifts.
    for _ in range(64):
        rd, rn, rm = (rng.randrange(8) for _ in range(3))
        width = rng.choice(["x", "w"])
        operation = rng.choice(["add", "adds", "sub", "subs", "and", "ands", "orr", "eor", "bic", "bics", "orn", "eon"])
        shifts = ["lsl", "lsr", "asr"] if operation in ["add", "adds", "sub", "subs"] else ["lsl", "lsr", "asr", "ror"]
        shift = rng.choice(shifts)
        amount = rng.randrange(64 if width == "x" else 32)
        lines.append(f"{operation} {width}{rd}, {width}{rn}, {width}{rm}, {shift} #{amount}")
    # Capture NZCV via branches without modifying flags, followed by all eight registers.
    lines.append("mov x10, #0")
    for index, condition in enumerate(["pl", "ne", "cc", "vc"]):
        lines.extend([f"b.{condition} flag_{index}", f"add x10, x10, #{1 << (3 - index)}", f"flag_{index}:"])
    lines.extend(["adrp x9, snapshot", "add x9, x9, :lo12:snapshot"])
    lines.extend(f"str x{r}, [x9, #{r * 8}]" for r in range(8))
    lines.extend(["str x10, [x9, #64]", "mov x0, #1", "mov x1, x9", "mov x2, #72", "mov x8, #64", "svc #0", "mov x0, #0", "mov x8, #93", "svc #0", ".bss", ".balign 8", "snapshot: .skip 72"])
    # Linux AArch64 takes the syscall number from x8 for every SVC immediate.
    immediate = [0, 1, 65535][seed % 3]
    return "\n".join(lines).replace("svc #0", f"svc #{immediate}") + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", default=RUNNER)
    parser.add_argument("--seeds", type=int, default=64)
    args = parser.parse_args()
    if args.seeds < 1:
        parser.error("--seeds must be positive")
    runner = str(Path(args.runner).resolve())
    qemu = os.environ.get("QEMU_AARCH64", "qemu-aarch64")
    assembler = os.environ.get("AARCH64_AS", "aarch64-linux-gnu-as")
    linker = os.environ.get("AARCH64_LD", "aarch64-linux-gnu-ld")
    for tool in [runner, qemu, assembler, linker]:
        if not shutil.which(tool):
            raise SystemExit(f"required tool not found: {tool}")
    subprocess.run(["bash", str(ROOT / "scripts/build-fixtures.sh")], check=True)
    fixtures = ROOT / "tests/fixtures/bin"
    cases = [
        ("hello", (), (0, b"Hello from Trames!\n", b"")),
        ("streams", (), (37, b"A\0\xff\n", b"stderr: Trames\n")),
        ("syscalls", (), (0, b"", b"")),
        ("stack", ("abc",), (2, b"abc", b"")),
        ("memory", (), (0, bytes.fromhex("2211443366558877"), b"")),
        ("branches", (), (0, b"", b"")),
    ]
    for name, guest_args, expected in cases:
        count = compare(runner, qemu, fixtures / name, guest_args, expected)
        print(f"PASS {name}: stdout/stderr/exit matched ({count} stdout bytes)", flush=True)
    with tempfile.TemporaryDirectory(prefix="trames-differential-") as directory:
        folder = Path(directory)
        for seed in range(args.seeds):
            source, obj, binary = (folder / n for n in ["generated.S", "generated.o", "generated"])
            source.write_text(generated(seed))
            subprocess.run([assembler, "-o", str(obj), str(source)], check=True)
            subprocess.run([linker, "-static", "--build-id=none", "-z", "noexecstack", "-o", str(binary), str(obj)], check=True)
            try:
                count = compare(runner, qemu, binary)
            except AssertionError as error:
                raise AssertionError(f"generated seed {seed}: {error}") from error
            if count != 72:
                raise AssertionError(f"seed {seed}: missing register snapshot")
        print(f"PASS {args.seeds} generated programs: register/NZCV snapshots matched", flush=True)
    for name, options, expected_code, diagnostic in [
        ("loop", ["--max-instructions", "17"], 125, b"instruction limit exceeded"),
        ("unsupported", [], 132, b"unsupported or unallocated instruction"),
        ("write-text", [], 139, b"Permission"),
    ]:
        code, stdout, stderr = execute([runner, "--memory", *options, str(fixtures / name)])
        if code != expected_code or stdout or diagnostic not in stderr:
            raise AssertionError(f"{name}: incorrect fault: {(code, stdout, stderr)!r}")
        print(f"PASS {name}: bounded execution or typed fault", flush=True)
    # A filename change cannot change execution; a payload change must change output.
    with tempfile.TemporaryDirectory(prefix="trames-payload-") as directory:
        binary = Path(directory) / "arbitrary-name"
        data = (fixtures / "hello").read_bytes()
        if data.count(b"Hello from Trames!\n") != 1:
            raise AssertionError("unique fixture payload required")
        # Keep the payload exactly the same length so segment headers remain valid.
        modified = b"Other ELF payload!\n"
        if len(modified) != len(b"Hello from Trames!\n"):
            raise AssertionError("replacement payload length")
        binary.write_bytes(data.replace(b"Hello from Trames!\n", modified))
        binary.chmod(0o700)
        compare(runner, qemu, binary, expected=(0, modified, b""))
        print("PASS renamed and modified ELF: output follows guest bytes", flush=True)
    print(f"M0 differential checks passed: {len(cases) + args.seeds + 1} QEMU comparisons, 3 fault checks")


if __name__ == "__main__":
    main()
