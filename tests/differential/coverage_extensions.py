#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent execution tests for the newly added scalar, SIMD and memory ISA.

PAC and zero-tag compatibility are intentionally checked by policy-specific unit
and guest tests rather than falsely compared with enabled hardware protection.
"""
import argparse
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
from run import RUNNER

AS = os.getenv('AARCH64_AS', 'aarch64-linux-gnu-as')
LD = os.getenv('AARCH64_LD', 'aarch64-linux-gnu-ld')
QEMU = os.getenv('QEMU_AARCH64', 'qemu-aarch64')
ARCH = 'armv9.4-a+crypto+sha3+sm4+lse128+memtag+ls64'


def write_output(length):
    return ['adrp x1,output', 'add x1,x1,:lo12:output', f'ldr x2, ={length}',
            '.Lwrite:', 'mov x0,#1', 'mov x8,#64', 'svc #0', 'add x1,x1,x0',
            'sub x2,x2,x0', 'cbnz x2,.Lwrite', 'mov x0,#0', 'mov x8,#93', 'svc #0', '.ltorg']


def compare(folder, code, name, stride=None, cases=None, oracle_code=None):
    (folder/'case.S').write_text('\n'.join(code)+'\n')
    subprocess.run([AS, '-march='+ARCH, '-o', str(folder/'case.o'), str(folder/'case.S')], check=True, capture_output=True)
    subprocess.run([LD, '-static', '--build-id=none', '-z', 'noexecstack', '-o', str(folder/'case'), str(folder/'case.o')], check=True, capture_output=True)
    actual = subprocess.run([RUNNER, '--memory', '--max-instructions', '100000000', str(folder/'case')], capture_output=True, timeout=45)
    oracle_binary = folder / 'case'
    if oracle_code is not None:
        (folder/'oracle.S').write_text('\n'.join(oracle_code)+'\n')
        subprocess.run([AS, '-march=armv8.5-a', '-o', str(folder/'oracle.o'), str(folder/'oracle.S')], check=True, capture_output=True)
        subprocess.run([LD, '-static', '--build-id=none', '-z', 'noexecstack', '-o', str(folder/'oracle'), str(folder/'oracle.o')], check=True, capture_output=True)
        oracle_binary = folder/'oracle'
    oracle = subprocess.run([QEMU, '-cpu', 'max', str(oracle_binary)], capture_output=True, timeout=45)
    assert actual.returncode == oracle.returncode == 0, (name, actual.returncode, actual.stderr, oracle.returncode, oracle.stderr)
    assert len(actual.stdout) == len(oracle.stdout), (name, len(actual.stdout), len(oracle.stdout))
    if actual.stdout != oracle.stdout:
        offset = next(i for i,(a,b) in enumerate(zip(actual.stdout,oracle.stdout)) if a != b)
        case = cases[offset//stride] if cases is not None else name
        raise AssertionError((name, case, offset, actual.stdout[max(0,offset-16):offset+24].hex(), oracle.stdout[max(0,offset-16):offset+24].hex()))
    return len(actual.stdout)


def cssc_reference(line):
    """QEMU 10.0 has no CSSC: independently execute equivalent baseline ARM ops."""
    parts=line.replace(',', ' ').split()
    op=parts[0]
    if op not in ['abs','cnt','ctz','smax','smin','umax','umin']:return [line]
    dest,source=parts[1:3];reg=dest[0];tmp=reg+'14'
    if op=='abs':return ['mrs x14,nzcv',f'cmp {source},#0',f'cneg {dest},{source},mi','msr nzcv,x14']
    if op=='cnt':
        return [f'mov {tmp},{source}','fmov d31,x14','cnt v31.8b,v31.8b','addv b31,v31.8b',f'umov w{dest[1:]},v31.b[0]']
    if op=='ctz':return [f'rbit {tmp},{source}',f'clz {dest},{tmp}']
    right=parts[3];prefix=['mrs x14,nzcv']
    if right.startswith('#'):
        prefix.append(f'mov {reg}13,{right}');right=reg+'13'
    condition={'smax':'ge','smin':'le','umax':'hs','umin':'ls'}[op]
    return prefix+[f'cmp {source},{right}',f'csel {dest},{source},{right},{condition}','msr nzcv,x14']


def scalar(folder):
    rng = random.Random(31982)
    edge = [0,1,127,128,255,1<<31,(1<<32)-1,1<<63,(1<<64)-1]
    cases=[]
    for reg in ['w','x']:
        for dest in ['0','1','2','zr']:
            for op in ['abs','cnt','ctz']:
                for value in edge+[rng.getrandbits(64) for _ in range(12)]:
                    cases.append((f'{op} {reg}{dest},{reg}1',[rng.getrandbits(64),value,rng.getrandbits(64)],rng.randrange(16)))
            for op in ['smax','smin','umax','umin']:
                for _ in range(24):
                    cases.append((f'{op} {reg}{dest},{reg}1,{reg}2',[rng.getrandbits(64),rng.choice(edge),rng.choice(edge)],rng.randrange(16)))
                for imm in ([-128,-1,0,1,127] if op[0]=='s' else [0,1,127,128,255]):
                    for value in edge:
                        cases.append((f'{op} {reg}{dest},{reg}1,#{imm}',[rng.getrandbits(64),value,rng.getrandbits(64)],rng.randrange(16)))
    for op in ['axflag','xaflag']:
        for flags in range(16):cases.append((op,[rng.getrandbits(64) for _ in range(3)],flags))
    for offset in range(0,len(cases),64):
        batch=cases[offset:offset+64]
        code=['.global _start','.text','_start:','adrp x20,input','add x20,x20,:lo12:input','adrp x21,output','add x21,x21,:lo12:output']
        for line,_,_ in batch:
            code+=['ldp x0,x1,[x20],#16','ldp x2,x3,[x20],#16','msr nzcv,x3',line,'stp x0,x1,[x21],#16','mrs x3,nzcv','stp x2,x3,[x21],#16']
        code+=write_output(len(batch)*32)+['.data','.balign 16','input:']
        for _,values,flags in batch:code += [f'.quad {values[0]},{values[1]},{values[2]},{flags<<28}']
        code+=['.bss','.balign 16',f'output:.skip {len(batch)*32}']
        oracle_code=[item for line in code for item in cssc_reference(line)]
        compare(folder,code,'CSSC/FlagM2',32,batch,oracle_code=oracle_code)
    print(f'CSSC/FlagM2: {len(cases)} complete GPR/NZCV snapshots (CSSC uses baseline ARM oracle sequences)',flush=True)


def crypto(folder):
    rng=random.Random(952812)
    templates=['sha512h qD,q1,v2.2d','sha512h2 qD,q1,v2.2d',
        'sha512su0 vD.2d,v1.2d','sha512su1 vD.2d,v1.2d,v2.2d',
        'eor3 vD.16b,v1.16b,v2.16b,v3.16b','bcax vD.16b,v1.16b,v2.16b,v3.16b','rax1 vD.2d,v1.2d,v2.2d',
        'sm3ss1 vD.4s,v1.4s,v2.4s,v3.4s','sm3partw1 vD.4s,v1.4s,v2.4s','sm3partw2 vD.4s,v1.4s,v2.4s',
        'sm4e vD.4s,v1.4s','sm4ekey vD.4s,v1.4s,v2.4s']
    templates += [f'xar vD.2d,v1.2d,v2.2d,#{i}' for i in range(64)]
    templates += [f'{op} vD.4s,v1.4s,v2.s[{i}]' for op in ['sm3tt1a','sm3tt1b','sm3tt2a','sm3tt2b'] for i in range(4)]
    samples=[]
    patterns=[0,(1<<128)-1,int.from_bytes(bytes(range(16)),'little'),1<<127]
    for line in templates:
        for dest in range(4):
            for trial in range(16):
                values=[patterns[(trial+i)%4] for i in range(4)] if trial<4 else [rng.getrandbits(128) for _ in range(4)]
                samples.append((line.replace('D',str(dest)),dest,values,rng.getrandbits(32)&0x0800009f,rng.randrange(16)<<28))
    for offset in range(0,len(samples),64):
        batch=samples[offset:offset+64]
        code=['.global _start','.text','_start:','adrp x20,input','add x20,x20,:lo12:input','adrp x21,output','add x21,x21,:lo12:output']
        for line,dest,*_ in batch:
            code+=['ldp q0,q1,[x20],#32','ldp q2,q3,[x20],#32','ldp x4,x5,[x20],#16','msr fpsr,x4','msr nzcv,x5',line,
                'stp q0,q1,[x21],#32','stp q2,q3,[x21],#32','mrs x4,fpsr','mrs x5,nzcv','stp x4,x5,[x21],#16']
        code+=write_output(len(batch)*80)+['.data','.balign 16','input:']
        for _,_,values,flags,nzcv in batch:
            for value in values:code += [f'.quad {value&((1<<64)-1)},{value>>64}']
            code += [f'.quad {flags},{nzcv}']
        code+=['.bss','.balign 16',f'output:.skip {len(batch)*80}']
        compare(folder,code,'SHA512/SHA3/SM3/SM4',80,batch)
    print(f'SHA512/SHA3/SM3/SM4: {len(samples)} complete vector/FPSR/NZCV snapshots',flush=True)


def simd(folder):
    rng=random.Random(67191)
    templates=['bfcvt hD,s1','bfcvtn vD.4h,v1.4s','bfcvtn2 vD.8h,v1.4s',
        'bfmmla vD.4s,v1.8h,v2.8h','bfmlalb vD.4s,v1.8h,v2.8h','bfmlalt vD.4s,v1.8h,v2.8h']
    for wide in [False,True]:
        out='4s' if wide else '2s';src='8h' if wide else '4h';half='4h' if wide else '2h'
        templates.append(f'bfdot vD.{out},v1.{src},v2.{src}')
        for i in range(4):templates.append(f'bfdot vD.{out},v1.{src},v2.2h[{i}]')
        for op in ['fmlal','fmlsl','fmlal2','fmlsl2']:
            templates.append(f'{op} vD.{out},v1.{half},v2.{half}')
            for i in range(8):templates.append(f'{op} vD.{out},v1.{half},v2.h[{i}]')
    for op in ['bfmlalb','bfmlalt']:
        for i in range(8):templates.append(f'{op} vD.4s,v1.8h,v2.h[{i}]')
    for arr in ['4h','8h','2s','4s','2d']:
        for angle in [90,270]:templates.append(f'fcadd vD.{arr},v1.{arr},v2.{arr},#{angle}')
        for angle in [0,90,180,270]:templates.append(f'fcmla vD.{arr},v1.{arr},v2.{arr},#{angle}')
        if arr!='2d' and arr!='2s':
            for index in range(4 if arr=='8h' else 2):
                for angle in [0,90,180,270]:templates.append(f'fcmla vD.{arr},v1.{arr},v2.{"h" if arr.endswith("h") else "s"}[{index}],#{angle}')
    samples=[]
    edge=[0,(1<<128)-1,int.from_bytes(bytes([0,124,0,128,1,0,255,123]*2),'little')]
    for line in templates:
        for dest in [0,1,2]:
            for control in [0,1<<19,1<<24,1<<25,3<<22]:
                for trial in range(4):
                    values=[edge[(trial+i)%3] for i in range(3)] if trial<3 else [rng.getrandbits(128) for _ in range(3)]
                    samples.append((line.replace('D',str(dest)),values,control,rng.randrange(16)<<28))
    for offset in range(0,len(samples),64):
        batch=samples[offset:offset+64]
        code=['.global _start','.text','_start:','adrp x20,input','add x20,x20,:lo12:input','adrp x21,output','add x21,x21,:lo12:output']
        for line,*_ in batch:
            code+=['ldp q0,q1,[x20],#32','ldr q2,[x20],#16','ldp x4,x5,[x20],#16','msr fpcr,x4','msr fpsr,xzr','msr nzcv,x5',line,
                'stp q0,q1,[x21],#32','str q2,[x21],#16','mrs x4,fpsr','mrs x5,nzcv','stp x4,x5,[x21],#16']
        code+=write_output(len(batch)*64)+['.data','.balign 16','input:']
        for _,values,fpcr,nzcv in batch:
            for value in values:code += [f'.quad {value&((1<<64)-1)},{value>>64}']
            code += [f'.quad {fpcr},{nzcv}']
        code+=['.bss','.balign 16',f'output:.skip {len(batch)*64}']
        compare(folder,code,'BF16/FHM/complex',64,batch)
    print(f'BF16/FHM/complex: {len(samples)} complete vector/FPSR/NZCV snapshots',flush=True)


def memory(folder):
    rng=random.Random(75913)
    templates=[f'{op}{suffix} {reg}0,[x4]' for op in ['ldlar','stllr'] for suffix,reg in [('b','w'),('h','w'),('','w'),('','x')]]
    # QEMU 10.0 lacks LSE128, so those operations are tested by fault/alias unit tests.
    for line in templates:
        for _ in range(16):
            value=rng.getrandbits(64);cells=[rng.getrandbits(64) for _ in range(4)]
            code=['.global _start','.text','_start:','adrp x4,buffer','add x4,x4,:lo12:buffer',f'ldr x0, ={value}','mov x3,#0xa0000000','msr nzcv,x3',line,
                'adrp x21,output','add x21,x21,:lo12:output','str x0,[x21],#8','mrs x3,nzcv','str x3,[x21],#8','ldp q0,q1,[x4]','stp q0,q1,[x21]']
            code+=write_output(48)+['.data','.balign 16','buffer:',f'.quad {cells[0]},{cells[1]},{cells[2]},{cells[3]}','.bss','.balign 16','output:.skip 48']
            compare(folder,code,line)
    print(f'LOR: {len(templates)*16} memory/GPR/NZCV snapshots',flush=True)


def mops(folder):
    rng=random.Random(88916)
    size=320*1024
    # An independently generated input distinguishes neighbouring bytes/pages.
    (folder/'buffer.bin').write_bytes(rng.randbytes(size))
    cases=[]
    for length in [0,1,7,16,31,127,4095,4096,4097,16385,131073]:
        for copy in ['cpyf','cpy']:
            cases.append((copy,length,8192,163840))
            if copy=='cpy' and length:
                cases.append((copy,length,163841,163840))
                cases.append((copy,length,163840,163847))
        cases.append(('set',length,8193,0x6d))
    cases += [('cpy',8193,163840+4097,163840),('cpy',8193,163840,163840+4097)]
    for family,length,dest,source in cases:
        code=['.global _start','.text','_start:','adrp x20,buffer','add x20,x20,:lo12:buffer',f'ldr x0, ={dest}','add x0,x20,x0']
        if family=='set':code += [f'ldr x1, ={length}',f'mov x2,#{source}']
        else:code += [f'ldr x1, ={source}','add x1,x20,x1',f'ldr x2, ={length}']
        code += ['mov x3,#0xf0000000','msr nzcv,x3','adrp x21,output','add x21,x21,:lo12:output']
        for stage in ['p','m','e']:
            code += [f'set{stage} [x0]!,x1!,x2' if family=='set' else f'{family}{stage} [x0]!,[x1]!,x2!',
                    'stp x0,x1,[x21],#16','mrs x3,nzcv','stp x2,x3,[x21],#16']
        code += write_output(96)
        # Write buffer before exiting: replace the first exit sequence with a
        # second write loop, retaining the exact stage snapshots first.
        at=code.index('mov x0,#0')
        code[at:at]=['adrp x1,buffer','add x1,x1,:lo12:buffer',f'ldr x2, ={size}',
            '.Lbuffer:','mov x0,#1','mov x8,#64','svc #0','add x1,x1,x0','sub x2,x2,x0','cbnz x2,.Lbuffer']
        code+=['.data','.balign 4096','buffer:',f'.incbin "{folder / "buffer.bin"}"','.bss','.balign 16','output:.skip 96']
        assert compare(folder,code,(family,length,dest,source))==96+size
    print(f'MOPS: {len(cases)} three-stage snapshots and complete buffers, including overlap and yielding',flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--groups',nargs='+',choices=['scalar','crypto','simd','memory','mops'],default=['scalar','crypto','simd','memory','mops'])
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='trames-isa-extensions-') as name:
        folder=Path(name)
        for group in args.groups:globals()[group](folder)

if __name__=='__main__':main()
