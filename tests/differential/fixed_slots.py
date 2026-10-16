#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent snapshots of warmed, bounded pure-integer regions and their exits."""
import argparse
from pathlib import Path
import random
import tempfile

import coverage_extensions as oracle


def instruction(rng):
    width = rng.choice(('w', 'x'))
    bits = 32 if width == 'w' else 64
    d, a, b, c = [width + str(rng.randrange(4)) for _ in range(4)]
    zero = width + 'zr'
    if rng.randrange(8) == 0:
        d = zero
    op = rng.randrange(10)
    if op == 0:
        return f'{rng.choice(("add", "sub"))} {d},{a},{b},lsl #{rng.randrange(bits)}'
    if op == 1:
        d = width + str(rng.randrange(4))  # immediate add's r31 is SP, not ZR
        return f'{rng.choice(("add", "sub"))} {d},{a},#{rng.randrange(4096)}'
    if op == 2:
        return f'{rng.choice(("and", "orr", "eor", "bic", "orn", "eon"))} {d},{a},{b},lsl #{rng.randrange(bits)}'
    if op == 3:
        d = width + str(rng.randrange(4))  # logical immediate's r31 denotes SP
        return f'{rng.choice(("and", "orr", "eor"))} {d},{a},#0xff00'
    if op == 4:
        return f'{rng.choice(("ubfm", "sbfm", "bfm"))} {d},{a},#{rng.randrange(bits)},#{rng.randrange(bits)}'
    if op == 5:
        return f'{rng.choice(("madd", "msub"))} {d},{a},{b},{c}'
    if op == 6:
        return f'{rng.choice(("mul", "mneg"))} {d},{a},{b}'
    if op == 7:
        return f'{rng.choice(("movz", "movn", "movk"))} {d},#{rng.randrange(65536)},lsl #{rng.randrange(bits // 16) * 16}'
    if op == 8:
        return f'mov {d},{rng.choice((a, zero))}'
    return f'add {rng.choice(("sp", "x0", "x1"))},sp,#{rng.randrange(4096)}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', default=oracle.RUNNER)
    args = parser.parse_args()
    oracle.RUNNER = str(Path(args.runner).resolve())
    rng = random.Random(0x4F17ED)
    fixed = [
        ['add x0,x1,#5', 'add x0,x0,#7', 'add x2,x0,#9', 'mov x3,#4', 'mov x3,#5'],
        ['add w0,w1,#5', 'add x0,x0,#7', 'add w0,w0,#9'],
        ['mov x0,#1', 'movk x0,#0x8000,lsl #48', 'madd x1,x0,x0,x0', 'sub x1,x1,#7'],
        ['mov w0,#1', 'movk w0,#0x8000,lsl #16', 'msub w1,w0,w0,w0', 'add w1,w1,#7'],
        ['add sp,sp,#5', 'add sp,sp,#7', 'mov x0,sp', 'add x1,x0,#9'],
        ['orr xzr,x0,x1', 'add xzr,x2,x3', 'madd xzr,x0,x1,x2', 'nop'],
        ['mov x0,xzr', 'sub x1,x0,#7', 'madd x2,x1,x1,x0', 'bfm x2,x1,#3,#47'],
        ['mov w0,wzr', 'sub w1,w0,#7', 'madd w2,w1,w1,w0', 'bfm w2,w1,#3,#27'],
    ]
    sequences = fixed + [[instruction(rng) for _ in range(length)]
                         for length in (2, 3, 4, 8, 16) for _ in range(16)]
    cases = [(seq, seed) for seq in sequences
             for seed in (0, (1 << 64) - 1, 0x8000000080000000)]
    with tempfile.TemporaryDirectory(prefix='trames-fixed-slots-') as directory:
        folder = Path(directory)
        for offset in range(0, len(cases), 8):
            batch = cases[offset:offset + 8]
            code = ['.global _start', '.text', '_start:', 'mov x24,sp', 'mov x25,#0x100000',
                    'adrp x20,input', 'add x20,x20,:lo12:input',
                    'adrp x21,output', 'add x21,x21,:lo12:output']
            for index, (sequence, _) in enumerate(batch):
                code += ['mov x22,#128', f'case_{index}:',
                         'ldp x0,x1,[x20]', 'ldp x2,x3,[x20,#16]',
                         'mov sp,x25', 'mov x23,#0x90000000', 'msr nzcv,x23',
                         *sequence, 'mrs x23,nzcv', 'mov x9,sp',
                         # A synthetic common SP makes arithmetic that copies
                         # SP into a GPR comparable across different loaders.
                         # No instruction accesses memory through this SP.
                         'sub x9,x9,x25', 'bl other_page',
                         'subs x22,x22,#1', f'b.ne case_{index}',
                         'stp x0,x1,[x21]', 'stp x2,x3,[x21,#16]',
                         'stp x23,x9,[x21,#32]', 'add x20,x20,#32', 'add x21,x21,#48']
            code += ['mov sp,x24', *oracle.write_output(len(batch) * 48),
                     '.balign 4096', 'other_page:', 'ret', '.data', '.balign 16', 'input:']
            for _, seed in batch:
                code += ['.quad ' + ','.join(str((seed + i * 0x12345) & ((1 << 64) - 1)) for i in range(4))]
            code += ['.bss', '.balign 16', f'output:.skip {len(batch) * 48}']
            assert oracle.compare(folder, code, 'fixed-slot regions', 48, batch) == len(batch) * 48
    print(f'PASS {len(cases)} warmed region snapshots: folding, overlap, W/X, SP/ZR and NZCV')


if __name__ == '__main__':
    main()
