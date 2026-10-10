#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Integer reductions emitted by GCC's fixed-size memcmp expansion."""
import os, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def main():
    source = ['.text', '.global _start', '_start:', 'adr x9, input', 'adr x10, output']
    cases = 0
    for operation in ['smaxp', 'sminp', 'umaxp', 'uminp']:
        for bits, letter in [(8, 'b'), (16, 'h'), (32, 's')]:
            for width in [64, 128]:
                for destination in [0, 2]:
                    arrangement = f'{width // bits}{letter}'
                    source += ['ldp q0, q1, [x9]', f'{operation} v{destination}.{arrangement}, v0.{arrangement}, v1.{arrangement}', f'str q{destination}, [x10], #16']
                    cases += 1
    source += ['adr x1, output', f'mov x2, #{cases * 16}', 'mov x0, #1', 'mov x8, #64', 'svc #0', 'mov x0, #0', 'mov x8, #93', 'svc #0', '.data', '.balign 16', 'input:', '.byte 0,255,128,127,1,254,129,126,254,1,255,0,127,128,126,129', '.byte 255,0,127,128,254,1,126,129,1,254,0,255,128,127,129,126', '.bss', '.balign 16', f'output: .skip {cases * 16}']
    with tempfile.TemporaryDirectory(prefix='trames-pairwise-') as folder:
        p = Path(folder); (p/'case.S').write_text('\n'.join(source)+'\n')
        subprocess.run([os.getenv('AARCH64_AS', 'aarch64-linux-gnu-as'), '-o', str(p/'case.o'), str(p/'case.S')], check=True)
        subprocess.run([os.getenv('AARCH64_LD', 'aarch64-linux-gnu-ld'), '-o', str(p/'case'), str(p/'case.o')], check=True)
        a = subprocess.run([str(ROOT/'target/debug/trames-run'), '--memory', str(p/'case')], capture_output=True, timeout=15)
        b = subprocess.run([os.getenv('QEMU_AARCH64', 'qemu-aarch64'), '-cpu', 'cortex-a53', str(p/'case')], capture_output=True, timeout=15)
        if a.returncode != 0 or len(b.stdout) != cases * 16 or (a.returncode, a.stdout, a.stderr) != (b.returncode, b.stdout, b.stderr):
            raise AssertionError((a.returncode, a.stderr, b.returncode, b.stderr, a.stdout.hex(), b.stdout.hex()))
    print(f'Pairwise integer SIMD passed: {cases} snapshots in 1 QEMU comparison')
if __name__ == '__main__': main()
