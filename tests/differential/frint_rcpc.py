#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""FRINTTS scalar/vector and LRCPC2 memory semantics against pinned QEMU."""
import os
from pathlib import Path
from run import RUNNER
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
AS = os.getenv('AARCH64_AS', 'aarch64-linux-gnu-as')
LD = os.getenv('AARCH64_LD', 'aarch64-linux-gnu-ld')
QEMU = os.getenv('QEMU_AARCH64', 'qemu-aarch64')


def compare(folder, code, length, cases, stride):
    (folder / 'case.S').write_text('\n'.join(code) + '\n')
    subprocess.run([AS, '-march=armv8.5-a', '-o', str(folder / 'case.o'),
                    str(folder / 'case.S')], check=True, capture_output=True)
    subprocess.run([LD, '-o', str(folder / 'case'), str(folder / 'case.o')],
                   check=True, capture_output=True)
    actual = subprocess.run([RUNNER, '--memory',
                             str(folder / 'case')], capture_output=True, timeout=30)
    oracle = subprocess.run([QEMU, '-cpu', 'max', str(folder / 'case')],
                            capture_output=True, timeout=30)
    assert actual.returncode == oracle.returncode == 0, (actual.stderr, oracle.stderr)
    assert len(actual.stdout) == len(oracle.stdout) == length
    for i, case in enumerate(cases):
        a, b = actual.stdout[i * stride:(i + 1) * stride], oracle.stdout[i * stride:(i + 1) * stride]
        assert a == b, (case, a.hex(), b.hex())


def output_code(length):
    return ['adrp x1,output', 'add x1,x1,:lo12:output', f'mov x2,#{length}',
            '.Lwrite:', 'mov x0,#1', 'mov x8,#64', 'svc #0', 'add x1,x1,x0',
            'sub x2,x2,x0', 'cbnz x2,.Lwrite', 'mov x0,#0', 'mov x8,#93', 'svc #0']


def fp_cases():
    rng = random.Random(70815)
    for bits, scalar, arrays, pack in [(32, 's', ['2s', '4s'], 'f'), (64, 'd', ['2d'], 'd')]:
        edges = [0, 1, (1 << (bits - 1)), (1 << (bits - 1)) | 1]
        for value in [0.5, -0.5, 1.5, -1.5, 2147483647.0, 2147483647.5,
                      2147483648.0, -2147483648.0, -2147483648.5, -2147483649.0,
                      2.0**63, -(2.0**63), 2.0**64, float('inf'), -float('inf'), float('nan')]:
            raw = int.from_bytes(struct.pack('<' + pack, value), 'little')
            edges += [raw, raw - 1, raw + 1]
        # Include signaling NaNs, both signs, and random exponent/significands.
        edges += ([0x7f800001, 0xff800001] if bits == 32 else [0x7ff0000000000001, 0xfff0000000000001])
        for op in ['frint32z', 'frint32x', 'frint64z', 'frint64x']:
            for arrangement in [None] + arrays:
                for dest in [0, 1]:
                    for control in [0, 1 << 24, 1 << 25, (1 << 24) | (1 << 25)]:
                        for mode in range(4):
                            for raw in edges + [rng.getrandbits(bits) for _ in range(16)]:
                                words = [raw] + [rng.choice(edges) for _ in range(128 // bits - 1)]
                                vector = sum((word & ((1 << bits) - 1)) << (i * bits) for i, word in enumerate(words))
                                ins = (f'{op} {scalar}{dest},{scalar}1' if arrangement is None else
                                       f'{op} v{dest}.{arrangement},v1.{arrangement}')
                                yield ins, dest, vector, control | (mode << 22), rng.getrandbits(32) & 0x0800009f


def floating(folder):
    samples = list(fp_cases())
    programs = 0
    for offset in range(0, len(samples), 96):
        batch = samples[offset:offset + 96]
        code = ['.global _start', '.text', '_start:', 'adrp x20,input', 'add x20,x20,:lo12:input',
                'adrp x21,output', 'add x21,x21,:lo12:output']
        for ins, dest, *_ in batch:
            code += ['ldr q1,[x20],#16', 'ldp x3,x4,[x20],#16', 'msr fpcr,x3', 'msr fpsr,x4',
                     'movi v0.16b,#255', ins, f'str q{dest},[x21],#16', 'mrs x3,fpsr', 'str x3,[x21],#8']
        code += output_code(len(batch) * 24) + ['.data', '.balign 16', 'input:']
        for _, _, vector, control, flags in batch:
            code += [f'.quad {vector & ((1 << 64) - 1)},{vector >> 64},{control},{flags}']
        code += ['.bss', '.balign 16', f'output:.skip {len(batch) * 24}']
        compare(folder, code, len(batch) * 24, batch, 24)
        programs += 1
    return len(samples), programs


def memory(folder):
    rng = random.Random(702)
    forms = [('stlurb', 'w'), ('stlurh', 'w'), ('stlur', 'w'), ('stlur', 'x'),
             ('ldapurb', 'w'), ('ldapurh', 'w'), ('ldapur', 'w'), ('ldapur', 'x'),
             ('ldapursb', 'w'), ('ldapursb', 'x'), ('ldapursh', 'w'), ('ldapursh', 'x'), ('ldapursw', 'x')]
    cases = [(op, reg, dest, base, displacement, target, rng.getrandbits(64))
             for op, reg in forms for dest in [0, 1, 31] for base in ['x1', 'sp']
             for displacement in [-256, -1, 0, 1, 255] for target in [256, 257, 4092, 4095]
             if target % (1 if op.endswith('b') else 2 if op.endswith('h') else 8 if reg == 'x' and op in ['ldapur', 'stlur'] else 4) == 0]
    programs = 0
    for offset in range(0, len(cases), 64):
        batch = cases[offset:offset + 64]
        code = ['.global _start', '.text', '_start:', 'adrp x20,input', 'add x20,x20,:lo12:input',
                'adrp x22,output', 'add x22,x22,:lo12:output']
        for op, reg, dest, base, displacement, target, _ in batch:
            operand = reg + ('zr' if dest == 31 else str(dest))
            code += ['adrp x21,buffer', 'add x21,x21,:lo12:buffer', f'add x21,x21,#{target}',
                     'ldr x3,[x20],#8', 'str x3,[x21]', 'mov x0,#0x5555',
                     f'mov x4,#{abs(displacement)}',
                     f'{"sub" if displacement >= 0 else "add"} x1,x21,x4', 'mov sp,x1',
                     f'{op} {operand},[{base},#{displacement}]', 'stp x0,x1,[x22],#16',
                     'mov x4,sp', 'ldr x5,[x21]', 'stp x4,x5,[x22],#16']
        code += output_code(len(batch) * 32) + ['.data', '.balign 16', 'input:']
        code += [f'.quad {case[-1]}' for case in batch]
        code += ['.bss', '.balign 4096', 'buffer:.skip 8192', '.balign 16', f'output:.skip {len(batch) * 32}']
        compare(folder, code, len(batch) * 32, batch, 32)
        programs += 1
    return len(cases), programs


def alignment_faults(folder):
    # Trames does not advertise LSE2/USCAT and requires natural alignment.
    # QEMU max has LSE2: ordered accesses may be unaligned within 16 bytes.
    # Crossing that boundary faults in both feature profiles.
    for instruction in ['ldapur x0,[x1]', 'ldapur w0,[x1]', 'ldapurh w0,[x1]',
                        'stlur x0,[x1]', 'stlur w0,[x1]', 'stlurh w0,[x1]']:
        code = ['.global _start', '.text', '_start:', 'adrp x1,buffer', 'add x1,x1,:lo12:buffer',
                'add x1,x1,#15', instruction, 'mov x0,#0', 'mov x8,#93', 'svc #0',
                '.bss', '.balign 16', 'buffer:.skip 32']
        (folder / 'case.S').write_text('\n'.join(code) + '\n')
        subprocess.run([AS, '-march=armv8.5-a', '-o', str(folder / 'case.o'), str(folder / 'case.S')],
                       check=True, capture_output=True)
        subprocess.run([LD, '-o', str(folder / 'case'), str(folder / 'case.o')], check=True, capture_output=True)
        actual = subprocess.run([RUNNER, '--memory', str(folder / 'case')], capture_output=True)
        oracle = subprocess.run([QEMU, '-cpu', 'max', str(folder / 'case')], capture_output=True)
        assert actual.returncode == 128 + 7 and b'Alignment' in actual.stderr, (instruction, actual.stderr)
        assert oracle.returncode in [-7, 128 + 7], (instruction, oracle.returncode, oracle.stderr)


def main():
    with tempfile.TemporaryDirectory(prefix='trames-frint-rcpc-') as directory:
        folder = Path(directory)
        fp, fp_programs = floating(folder)
        print(f'FRINTTS: {fp} FPSR/vector snapshots in {fp_programs} QEMU comparisons', flush=True)
        mem, mem_programs = memory(folder)
        alignment_faults(folder)
    print(f'FRINTTS: {fp} FPSR/vector snapshots in {fp_programs} QEMU comparisons; '
          f'LRCPC2: {mem} memory/register snapshots in {mem_programs} QEMU comparisons; 6 alignment faults')


if __name__ == '__main__':
    main()
