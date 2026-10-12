#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""AES/SHA round, schedule and polynomial operations against independent QEMU."""
import os,random,subprocess,tempfile
from pathlib import Path
from run import RUNNER
ROOT=Path(__file__).resolve().parents[2]

def main():
    rng=random.Random(992491)
    instructions=[f'{op} vD.16b,v1.16b' for op in ['aese','aesd','aesmc','aesimc']]+[
        'sha1h sD,s1','sha1c qD,s1,v2.4s','sha1p qD,s1,v2.4s','sha1m qD,s1,v2.4s',
        'sha1su0 vD.4s,v1.4s,v2.4s','sha1su1 vD.4s,v1.4s',
        'sha256h qD,q1,v2.4s','sha256h2 qD,q1,v2.4s','sha256su0 vD.4s,v1.4s','sha256su1 vD.4s,v1.4s,v2.4s',
        'pmull vD.1q,v1.1d,v2.1d','pmull2 vD.1q,v1.2d,v2.2d']
    patterns=[0,(1<<128)-1,1<<127,int.from_bytes(bytes(range(16)),'little')]
    cases=[]
    for template in instructions:
        for dest in [0,1,2]:
            for trial in range(128):
                values=[patterns[(trial+i)%4] for i in range(3)] if trial<4 else [rng.getrandbits(128) for _ in range(3)]
                cases.append((template.replace('D',str(dest)),dest,values,rng.getrandbits(32)&0x0800009f,rng.getrandbits(4)<<28))
    with tempfile.TemporaryDirectory(prefix='trames-crypto-') as folder:
        p=Path(folder)
        for offset in range(0,len(cases),96):
            batch=cases[offset:offset+96];code=['.global _start','.text','_start:','adrp x20,input','add x20,x20,:lo12:input','adrp x21,output','add x21,x21,:lo12:output']
            for ins,dest,_,_,_ in batch:
                code+=['ldp q0,q1,[x20],#32','ldr q2,[x20],#16','ldp x4,x5,[x20],#16','msr fpsr,x4','msr nzcv,x5',ins,f'str q{dest},[x21],#16','mrs x5,fpsr','str x5,[x21],#8','mrs x5,nzcv','str x5,[x21],#8']
            code+=['adrp x1,output','add x1,x1,:lo12:output',f'mov x2,#{len(batch)*32}','.Lwrite:','mov x0,#1','mov x8,#64','svc #0','add x1,x1,x0','sub x2,x2,x0','cbnz x2,.Lwrite','mov x0,#0','mov x8,#93','svc #0','.data','.balign 16','input:']
            for _,_,values,flags,nzcv in batch:
                for val in values:code+=[f'.quad {val&((1<<64)-1)},{val>>64}']
                code+=[f'.quad {flags},{nzcv}']
            code+=['.bss','.balign 16',f'output:.skip {len(batch)*32}']
            (p/'case.S').write_text('\n'.join(code)+'\n')
            subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-march=armv8-a+crypto','-o',str(p/'case.o'),str(p/'case.S')],check=True,capture_output=True)
            subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True,capture_output=True)
            actual=subprocess.run([RUNNER,'--memory',str(p/'case')],capture_output=True,timeout=20)
            oracle=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max',str(p/'case')],capture_output=True,timeout=20)
            assert actual.returncode==oracle.returncode==0,(offset,actual.stderr,oracle.stderr)
            assert len(actual.stdout)==len(oracle.stdout)==len(batch)*32,(offset,len(actual.stdout),len(oracle.stdout))
            for i,case in enumerate(batch):
                a=actual.stdout[i*32:(i+1)*32];b=oracle.stdout[i*32:(i+1)*32]
                assert a==b,(offset+i,case,a.hex(),b.hex())
    print(f'Crypto: {len(cases)} AES/SHA/PMULL snapshots matched independent QEMU')
if __name__=='__main__':main()
