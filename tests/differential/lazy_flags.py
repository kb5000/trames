#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise deferred NZCV producers/consumers against independent AArch64 QEMU.

Observe both GPR results and all four architectural flags after every sequence;
in particular partial flag writers must preserve the deferred carry correctly.
"""
import argparse
from pathlib import Path
import random
import tempfile

import coverage_extensions as oracle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', default=oracle.RUNNER)
    args = parser.parse_args()
    oracle.RUNNER = str(Path(args.runner).resolve())
    conditions = 'eq ne hs lo mi pl vs vc hi ls ge lt gt le al nv'.split()
    consumers = []
    for index, condition in enumerate(conditions):
        consumers += [
            [f'csel x3,x1,x2,{condition}'],
            ['mov x3,#0', f'b.{condition} 1f', 'mov x3,#1', '1:'],
            [f'ccmp x1,x2,#{index},{condition}', 'adc x3,x1,x2'],
            [f'ccmn w1,#17,#{index},{condition}', 'adc x3,x1,x2'],
            ['fmov d0,#1.0', 'fmov d1,#2.0', f'fccmp d0,d1,#{index},{condition}'],
        ]
    for reg in ['w', 'x']:
        consumers += [[f'{op} {reg}3,{reg}1,{reg}2'] for op in ['adc', 'adcs', 'sbc', 'sbcs']]
    consumers += [[f'rmif x1,#29,#{mask}'] for mask in range(16)]
    consumers += [[op] for op in ['setf8 w1', 'setf16 w1', 'cfinv', 'axflag', 'xaflag',
                                 'ands x3,x1,x2', 'ands w3,w1,w2', 'msr nzcv,x2', 'mrs x3,nzcv']]
    consumers += [['mrs x3,nzcv', 'adc x3,x1,x2'], ['adds x3,x1,x2', 'subs w3,w1,w2'],
                  ['fmov d0,#1.0', 'fcmp d0,#0.0']]
    edges = [0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 1 << 32,
             0x7fffffffffffffff, 1 << 63, (1 << 64) - 1]
    pairs = [(a, b) for a in edges for b in [0, 1, a, (1 << 64) - 1]]
    rng = random.Random(0x9f71c)
    pairs += [(rng.getrandbits(64), rng.getrandbits(64)) for _ in range(12)]
    cases = [(f'{op} {reg}0,{reg}1,{reg}2', consumer, a, b)
             for reg in ['w', 'x'] for op in ['adds', 'subs']
             for consumer in consumers for a, b in pairs]
    with tempfile.TemporaryDirectory(prefix='trames-lazy-flags-') as directory:
        folder = Path(directory)
        for offset in range(0, len(cases), 64):
            batch = cases[offset:offset + 64]
            code = ['.global _start', '.text', '_start:',
                    'adrp x20,input', 'add x20,x20,:lo12:input',
                    'adrp x21,output', 'add x21,x21,:lo12:output']
            for producer, consumer, _, _ in batch:
                code += ['ldp x1,x2,[x20],#16', 'mov x3,#0', producer,
                         # Preserve flags through non-S ALU and a cache boundary
                         # as batches naturally cross code pages.
                         'add x4,x1,#1', 'nop', *consumer,
                         'stp x0,x3,[x21],#16', 'mrs x4,nzcv', 'str x4,[x21],#8']
            code += oracle.write_output(len(batch) * 24) + ['.data', '.balign 16', 'input:']
            for _, _, a, b in batch:
                code += [f'.quad {a},{b}']
            code += ['.bss', '.balign 16', f'output:.skip {len(batch) * 24}']
            length = oracle.compare(folder, code, 'lazy NZCV', 24, batch)
            assert length == len(batch) * 24
    print(f'PASS {len(cases)} NZCV sequences: 32/64-bit arithmetic, all conditions, '
          'carry chains, CCMP/FCCMP, FlagM/FlagM2 and explicit overwrites')


if __name__ == '__main__':
    main()
