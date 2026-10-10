#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ARMv8 integer, memory, SIMD transfer and exclusive-monitor differentials."""
import argparse
import os
from pathlib import Path
import random
import subprocess
import tempfile
from run import ROOT, execute


def generated(seed):
    rng = random.Random(seed)
    code = [".global _start", ".text", "_start:"]
    for r in range(8):
        for index in range(4):
            code.append(f"{'movz' if index == 0 else 'movk'} x{r}, #{rng.randrange(65536)}, lsl #{index * 16}")
    code.append("msr nzcv, xzr")
    for _ in range(96):
        d, n, m, a = [rng.randrange(8) for _ in range(4)]
        wide = rng.choice([True, False]); w = "x" if wide else "w"; bits = 64 if wide else 32
        group = rng.randrange(11)
        if group == 0:
            op = rng.choice(["and", "orr", "eor", "ands"])
            mask = (1 << rng.randrange(1, bits)) - 1; rotation = rng.randrange(bits)
            mask = ((mask >> rotation) | (mask << (bits - rotation))) & ((1 << bits) - 1)
            line = f"{op} {w}{d}, {w}{n}, #{mask:#x}"
        elif group == 1:
            line = f"{rng.choice(['sbfm','bfm','ubfm'])} {w}{d}, {w}{n}, #{rng.randrange(bits)}, #{rng.randrange(bits)}"
        elif group == 2:
            line = f"extr {w}{d}, {w}{n}, {w}{m}, #{rng.randrange(bits)}"
        elif group == 3:
            line = f"{rng.choice(['csel','csinc','csinv','csneg'])} {w}{d}, {w}{n}, {w}{m}, {rng.choice(['eq','ne','lt','ge','lo','hs','mi','pl','gt','le','al','nv'])}"
        elif group == 4:
            rhs = f"#{rng.randrange(32)}" if rng.choice([True, False]) else f"{w}{m}"
            line = f"{rng.choice(['ccmp','ccmn'])} {w}{n}, {rhs}, #{rng.randrange(16)}, {rng.choice(['eq','ne','lt','ge','lo','hs','mi','pl'])}"
        elif group == 5:
            line = f"{rng.choice(['madd','msub'])} {w}{d}, {w}{n}, {w}{m}, {w}{a}"
        elif group == 6:
            op = rng.choice(['smaddl','umaddl','smsubl','umsubl','smulh','umulh'])
            line = f"{op} x{d}, x{n}, x{m}" if op.endswith('h') else f"{op} x{d}, w{n}, w{m}, x{a}"
        elif group == 7:
            line = f"{rng.choice(['udiv','sdiv','lslv','lsrv','asrv','rorv'])} {w}{d}, {w}{n}, {w}{m}"
        elif group == 8:
            line = f"{rng.choice(['rbit','rev16','rev','clz','cls'] + (['rev32'] if wide else []))} {w}{d}, {w}{n}"
        elif group == 9:
            line = f"{rng.choice(['adc','adcs','sbc','sbcs'])} {w}{d}, {w}{n}, {w}{m}"
        else:
            line = f"{rng.choice(['add','adds','sub','subs'])} {w}{d}, {w}{n}, w{m}, {rng.choice(['uxtb','uxth','uxtw','sxtb','sxth','sxtw'])} #{rng.randrange(5)}"
        code.append(line)
    code.extend(["mrs x10, nzcv", "adrp x9, snapshot", "add x9, x9, :lo12:snapshot"])
    code.extend(f"str x{r}, [x9, #{r * 8}]" for r in range(8))
    code.extend(["str x10, [x9, #64]", "mov x0, #1", "mov x1, x9", "mov x2, #72", "mov x8, #64", "svc #0", "mov x0, #0", "mov x8, #93", "svc #0", ".bss", ".balign 8", "snapshot: .skip 72"])
    return "\n".join(code) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runner", default=str(ROOT / "target/debug/trames-run"))
    parser.add_argument("--seeds", type=int, default=64)
    args = parser.parse_args()
    if args.seeds < 1:
        parser.error("--seeds must be positive")
    runner = str(Path(args.runner).resolve())
    qemu = os.environ.get("QEMU_AARCH64", "qemu-aarch64")
    assembler = os.environ.get("AARCH64_AS", "aarch64-linux-gnu-as")
    linker = os.environ.get("AARCH64_LD", "aarch64-linux-gnu-ld")
    subprocess.run(["bash", str(ROOT / "scripts/build-fixtures.sh")], check=True)
    with tempfile.TemporaryDirectory(prefix="trames-m1-") as directory:
        folder = Path(directory)
        for seed in range(args.seeds):
            source, obj, binary = (folder / name for name in ["generated.S", "generated.o", "generated"])
            source.write_text(generated(seed))
            subprocess.run([assembler, "-o", str(obj), str(source)], check=True)
            subprocess.run([linker, "-static", "--build-id=none", "-z", "noexecstack", "-o", str(binary), str(obj)], check=True)
            actual = execute([runner, str(binary)])
            reference = execute([qemu, "-cpu", "cortex-a53", str(binary)])
            if actual != reference or actual[0] != 0 or len(actual[1]) != 72:
                raise AssertionError(f"M1 seed {seed}: Trames {actual!r} != QEMU {reference!r}")
        print(f"PASS {args.seeds} M1 generated programs: integer/register/NZCV snapshots matched", flush=True)
        for name in ["m1-memory", "m1-exclusive", "m1-files"]:
            binary = ROOT / "tests/fixtures/bin" / name
            actual = subprocess.run([runner, str(binary)], cwd=folder, capture_output=True, timeout=10)
            reference = subprocess.run([qemu, "-cpu", "cortex-a53", str(binary)], cwd=folder, capture_output=True, timeout=10)
            a = actual.returncode, actual.stdout, actual.stderr
            r = reference.returncode, reference.stdout, reference.stderr
            if a != r or a[0] != 0 or not a[1]:
                raise AssertionError(f"{name}: Trames {a!r} != QEMU {r!r}")
            print(f"PASS {name}: memory and syscall observations matched", flush=True)
    print(f"M1 differential passed: {args.seeds + 3} QEMU comparisons")


if __name__ == "__main__":
    main()
