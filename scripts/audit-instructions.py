#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproducible representative decode audit; no exhaustive ISA percentage."""
import argparse
from collections import Counter
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
ARCH = 'armv9.4-a+cssc+sve2+sme+crypto+sha3+sm4+lse128+memtag+ls64'


def additional_samples():
    groups = {
        'scalar/CSSC-FlagM2': ['axflag', 'xaflag'],
        'security-compatibility': '''pacia x0,x1
pacib x0,x1
pacda x0,x1
pacdb x0,x1
autia x0,x1
autib x0,x1
autda x0,x1
autdb x0,x1
paciza x0
pacizb x0
pacdza x0
pacdzb x0
autiza x0
autizb x0
autdza x0
autdzb x0
xpaci x0
xpacd x0
pacga x0,x1,x2
braa x0,x1
brab x0,x1
braaz x0
brabz x0
blraa x0,x1
blrab x0,x1
blraaz x0
blrabz x0
retaa
retab
ldraa x0,[x1,#8]!
ldrab x0,[x1,#-8]
irg x0,x1
addg x0,x1,#16,#1
subg x0,x1,#32,#2
gmi x0,x1,x2
subp x0,x1,x2
subps x0,x1,x2
ldg x0,[x1,#16]
stg x0,[x1,#16]
stg x0,[x1],#16
stzg x0,[x1,#16]!
st2g x0,[x1,#32]
stz2g x0,[x1,#32]
stgp x0,x2,[x1,#16]
bti
bti c
bti j
bti jc'''.splitlines(),
        'ordered-memory/LOR-LSE128-LS64': ['ld64b x0,[x1]', 'st64b x0,[x1]',
            'st64bv x8,x0,[x1]', 'st64bv0 x8,x0,[x1]'],
        'hints-and-system': ['wfi', 'wfet x0', 'wfit x0', 'sb', 'dgh', 'clrbhb',
            'mrs x0,rndr', 'mrs x0,rndrrs', 'mrs x0,cntpct_el0',
            'mrs x0,cntpctss_el0', 'mrs x0,cntvctss_el0',
            'mrs x0,tpidr2_el0', 'msr tpidr2_el0,x0'],
        'crypto/SHA512-SHA3-SM3-SM4': ['sha512h q0,q1,v2.2d', 'sha512h2 q0,q1,v2.2d',
            'sha512su0 v0.2d,v1.2d', 'sha512su1 v0.2d,v1.2d,v2.2d',
            'eor3 v0.16b,v1.16b,v2.16b,v3.16b', 'bcax v0.16b,v1.16b,v2.16b,v3.16b',
            'rax1 v0.2d,v1.2d,v2.2d', 'sm3ss1 v0.4s,v1.4s,v2.4s,v3.4s',
            'sm3partw1 v0.4s,v1.4s,v2.4s', 'sm3partw2 v0.4s,v1.4s,v2.4s',
            'sm4e v0.4s,v1.4s', 'sm4ekey v0.4s,v1.4s,v2.4s'],
        'simd/BF16-FHM-complex': ['bfcvt h0,s1', 'bfcvtn v0.4h,v1.4s',
            'bfcvtn2 v0.8h,v1.4s', 'bfmmla v0.4s,v1.8h,v2.8h',
            'bfmlalb v0.4s,v1.8h,v2.8h', 'bfmlalt v0.4s,v1.8h,v2.8h'],
        'memory/MOPS': [],
        'remaining/SVE-SVE2-SME': ['ptrue p0.b', 'add z0.s,z1.s,z2.s',
            'ld1w z0.s,p0/z,[x1]', 'whilelt p0.s,x0,x1',
            'eor3 z0.d,z0.d,z1.d,z2.d', 'smstart', 'smstop', 'zero {za}'],
        'privileged-tag-operations': ['ldgm x0,[x1]', 'stgm x0,[x1]', 'stzgm x0,[x1]'],
    }
    scalar = groups['scalar/CSSC-FlagM2']
    for width in ['w', 'x']:
        scalar += [f'{op} {width}0,{width}1' for op in ['abs', 'cnt', 'ctz']]
        for op in ['smax', 'smin', 'umax', 'umin']:
            scalar += [f'{op} {width}0,{width}1,{width}2',
                       f'{op} {width}0,{width}1,#{-1 if op[0] == "s" else 255}']
    mem = groups['ordered-memory/LOR-LSE128-LS64']
    for suffix, reg in [('', 'x'), ('', 'w'), ('b', 'w'), ('h', 'w')]:
        mem += [f'ldlar{suffix} {reg}0,[x1]', f'stllr{suffix} {reg}0,[x1]']
    for op in ['swpp', 'ldclrp', 'ldsetp']:
        mem += [f'{op}{order} x0,x2,[x4]' for order in ['', 'a', 'l', 'al']]
    crypto = groups['crypto/SHA512-SHA3-SM3-SM4']
    crypto += [f'xar v0.2d,v1.2d,v2.2d,#{i}' for i in range(64)]
    for op in ['sm3tt1a', 'sm3tt1b', 'sm3tt2a', 'sm3tt2b']:
        crypto += [f'{op} v0.4s,v1.4s,v2.s[{i}]' for i in range(4)]
    simd = groups['simd/BF16-FHM-complex']
    for arr, half in [('2s', '4h'), ('4s', '8h')]:
        simd += [f'bfdot v0.{arr},v1.{half},v2.{half}']
        simd += [f'bfdot v0.{arr},v1.{half},v2.2h[{i}]' for i in range(4)]
        for op in ['fmlal', 'fmlsl', 'fmlal2', 'fmlsl2']:
            source_arr = '2h' if arr == '2s' else '4h'
            simd += [f'{op} v0.{arr},v1.{source_arr},v2.{source_arr}']
            simd += [f'{op} v0.{arr},v1.{source_arr},v2.h[{i}]' for i in range(8)]
    for op in ['bfmlalb', 'bfmlalt']:
        simd += [f'{op} v0.4s,v1.8h,v2.h[{i}]' for i in range(8)]
    for arr in ['4h', '8h', '2s', '4s', '2d']:
        simd += [f'fcadd v0.{arr},v1.{arr},v2.{arr},#{angle}' for angle in [90, 270]]
        simd += [f'fcmla v0.{arr},v1.{arr},v2.{arr},#{angle}' for angle in [0, 90, 180, 270]]
    for arr, lane, count in [('4h', 'h', 2), ('8h', 'h', 4), ('4s', 's', 2)]:
        simd += [f'fcmla v0.{arr},v1.{arr},v2.{lane}[{i}],#{angle}'
                 for i in range(count) for angle in [0, 90, 180, 270]]
    mops = groups['memory/MOPS']
    for op in ['cpyf', 'cpy']:
        for hint in ['', 'wt', 'rt', 't', 'wn', 'rn', 'n', 'wtn', 'rtn',
                         'tn', 'wtwn', 'wtrn', 'rtwn', 'rtrn', 'trn', 'twn']:
            for stage in ['p', 'm', 'e']:
                mops.append(f'{op}{stage}{hint} [x0]!,[x1]!,x2!')
    for op in ['set', 'setg']:
        for hint in ['', 't', 'n', 'tn']:
            for stage in ['p', 'm', 'e']:
                mops.append(f'{op}{stage}{hint} [x0]!,x1!,x2')
    return [(group, asm) for group, rows in groups.items() for asm in rows]


def policy(assembly, status):
    op = assembly.split()[0]
    if status == 'unsupported':
        return 'remaining', 'Unavailable; faults with SIGILL. Representative probe only.'
    if op.startswith(('pac', 'aut', 'xpac', 'braa', 'brab', 'blraa', 'blrab', 'ldraa', 'ldrab')) or op in [
            'retaa', 'retab', 'bti', 'irg', 'addg', 'subg', 'gmi', 'subp', 'subps',
            'ldg', 'stg', 'stzg', 'st2g', 'stz2g', 'stgp'] or op.startswith('setg'):
        return 'compatibility', 'PAC/BTI/tag checks disabled; required branch/load/address/store effects execute.'
    if op.startswith(('ld64b', 'st64b')):
        return 'virtual-memory-profile', '64-byte normal-memory transfer; no physical LS64 device/ACCDATA model.'
    if op in ['wfi', 'wfet', 'wfit']:
        return 'compatibility', 'Spurious wakeup; normal bounded scheduler remains active.'
    return 'execution', 'Decode acceptance only; semantic evidence is recorded separately.'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'docs/validation/instruction-coverage-current')
    parser.add_argument('--probe', type=Path, default=ROOT/'target/release/examples/instruction_probe')
    args = parser.parse_args()
    rows = []
    with (ROOT/'docs/validation/nonjit-0.7-cpu.csv').open(newline='') as f:
        for row in csv.DictReader(f):
            rows.append(dict(category=row['category'], assembly=row['assembly'],
                             opcode=row['opcode'], previous=row['cached']))
    seen = {r['assembly'] for r in rows}
    assembler = os.getenv('AARCH64_AS', 'aarch64-linux-gnu-as')
    objcopy = os.getenv('AARCH64_OBJCOPY', 'aarch64-linux-gnu-objcopy')
    with tempfile.TemporaryDirectory(prefix='trames-isa-audit-') as d:
        folder = Path(d)
        extras = [(category, asm) for category, asm in additional_samples() if asm not in seen]
        (folder/'samples.S').write_text('.text\n'+ '\n'.join(asm for _, asm in extras)+'\n')
        subprocess.run([assembler, '-march='+ARCH, '-o', str(folder/'samples.o'), str(folder/'samples.S')], check=True)
        subprocess.run([objcopy, '-O', 'binary', '-j', '.text', str(folder/'samples.o'), str(folder/'samples.bin')], check=True)
        data = (folder/'samples.bin').read_bytes()
        assert len(data) == len(extras)*4, 'each sample must be exactly one instruction'
        for i, (category, asm) in enumerate(extras):
            rows.append(dict(category=category, assembly=asm,
                             opcode=f'{int.from_bytes(data[i*4:i*4+4], "little"):08x}', previous='not-probed'))
    result = subprocess.run([str(args.probe)], input=''.join(f'{i} {r["opcode"]}\n' for i,r in enumerate(rows)),
                            text=True, capture_output=True, check=True)
    lines = result.stdout.splitlines()
    assert len(lines) == len(rows)
    for i, (row, line) in enumerate(zip(rows, lines)):
        identifier, opcode, status = line.split()
        assert identifier == str(i) and opcode == row['opcode']
        row['cached'] = status
        row['profile'], row['notes'] = policy(row['assembly'], status)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.with_suffix('.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
    hwcap, hwcap2, features = subprocess.check_output([str(args.probe), '--features'], text=True).strip().split(' ', 2)
    sources = sorted((ROOT/'crates/trames-cpu/src').glob('*.rs')) + sorted((ROOT/'crates/trames-compute/src').glob('*.rs'))
    sources += [ROOT/'runtime/crates/trames-memory/src/lib.rs', ROOT/'runtime/crates/trames-memory/src/backing.rs', ROOT/'runtime/crates/trames-memory/src/reservations.rs', ROOT/'crates/trames-core/src/process_events.rs',
                ROOT/'crates/trames-core/src/process.rs', ROOT/'crates/trames-core/src/lib.rs', ROOT/'runtime/crates/trames-memory/src/cpu_memory.rs',
                ROOT/'runtime/crates/trames-memory/src/parallel.rs', ROOT/'crates/trames-core/src/workers.rs']
    report = dict(scope=f'{len(rows)} representative encodings; decode acceptance, not exhaustive ISA conformance',
                  statuses=dict(Counter(r['cached'] for r in rows)), profiles=dict(Counter(r['profile'] for r in rows)),
                  hwcap=int(hwcap), hwcap2=int(hwcap2), feature_names=features,
                  source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'source_sha256'}, indent=2))


if __name__ == '__main__':
    main()
