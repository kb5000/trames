#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Warm integer pairs across code pages, comparing results and NZCV with QEMU."""
import argparse
from pathlib import Path
import tempfile

import coverage_extensions as oracle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', default=oracle.RUNNER)
    args = parser.parse_args()
    oracle.RUNNER = str(Path(args.runner).resolve())
    operations = [
        'movk x0,#0xba98,lsl #32', 'add x0,x0,#11', 'adds x0,x0,#1',
        'sub x2,x2,x1,asr #7', 'ands x3,x2,x0,ror #11', 'orr x3,x3,#0xff00',
        'bfm x3,x3,#7,#25', 'csneg x5,x5,x2,ne', 'add x5,x5,w1,sxtw #2',
        'ccmp x5,#7,#9,ge', 'madd x6,x5,x0,x5', 'lsl x6,x6,x1', 'sdiv x6,x6,x0',
    ]
    cases = [(a, b, seed) for a in operations for b in operations
             for seed in [0, (1 << 64) - 1, 0x8000000080000000]]
    with tempfile.TemporaryDirectory(prefix='trames-integer-blocks-') as directory:
        folder = Path(directory)
        for offset in range(0, len(cases), 8):
            batch = cases[offset:offset + 8]
            code = ['.global _start', '.text', '_start:',
                    'adrp x20,input', 'add x20,x20,:lo12:input',
                    'adrp x21,output', 'add x21,x21,:lo12:output']
            for index, (a, b, _) in enumerate(batch):
                code += ['mov x22,#128', f'case_{index}:',
                         'ldp x0,x1,[x20]', 'ldp x2,x3,[x20,#16]',
                         'ldp x4,x5,[x20,#32]', 'ldp x6,x7,[x20,#48]',
                         'ldp x8,x9,[x20,#64]', 'mov x23,#0x90000000',
                         'msr nzcv,x23', a, b, 'mrs x23,nzcv',
                         # The helper is on a different guest code page. Each
                         # iteration revisits the arithmetic page, independently
                         # of the runtime's instruction-slice configuration.
                         'bl other_page', 'subs x22,x22,#1', f'b.ne case_{index}',
                         'stp x0,x1,[x21]', 'stp x2,x3,[x21,#16]',
                         'stp x4,x5,[x21,#32]', 'stp x6,x7,[x21,#48]',
                         'stp x8,x23,[x21,#64]', 'add x20,x20,#80', 'add x21,x21,#80']
            code += oracle.write_output(len(batch) * 80)
            code += ['.balign 4096', 'other_page:', 'ret', '.data', '.balign 16', 'input:']
            for _, _, seed in batch:
                code += ['.quad ' + ','.join(str((seed + i) & ((1 << 64) - 1)) for i in range(10))]
            code += ['.bss', '.balign 16', f'output:.skip {len(batch) * 80}']
            assert oracle.compare(folder, code, 'warm integer pairs', 80, batch) == len(batch) * 80
    print(f'PASS {len(cases)} warmed integer-pair snapshots: 169 combinations, 3 seeds, GPR/NZCV')


if __name__ == '__main__':
    main()
