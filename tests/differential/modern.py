#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""DotProd, I8MM, RDM, FlagM and JS conversion against independent QEMU."""
import os,random,struct,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def cases():
    rng=random.Random(185503)
    patterns=[0,(1<<128)-1,int.from_bytes(bytes([128,127,0,255]*4),'little'),int.from_bytes(bytes([1,254,129,126]*4),'little')]
    templates=[]
    for width in [64,128]:
        for bits,letter in [(16,'h'),(32,'s')]:
            arr=f'{width//bits}{letter}'
            for op in ['sqrdmlah','sqrdmlsh']:
                templates.append(f'{op} vD.{arr},v1.{arr},v2.{arr}')
                for index in range(128//bits):templates.append(f'{op} vD.{arr},v1.{arr},v2.{letter}[{index}]')
        for op in ['sdot','udot','usdot']:
            templates.append(f'{op} vD.{width//32}s,v1.{width//8}b,v2.{width//8}b')
        for op in ['sdot','udot','usdot','sudot']:
            for index in range(4):templates.append(f'{op} vD.{width//32}s,v1.{width//8}b,v2.4b[{index}]')
    for op in ['smmla','ummla','usmmla']:templates.append(f'{op} vD.4s,v1.16b,v2.16b')
    for bits,letter in [(16,'h'),(32,'s')]:
        for op in ['sqrdmlah','sqrdmlsh']:
            templates.append(f'{op} {letter}D,{letter}1,{letter}2')
            for index in range(128//bits):templates.append(f'{op} {letter}D,{letter}1,v2.{letter}[{index}]')
    for template in templates:
        for dest in [0,1,2]:
            for trial in range(32):
                values=[patterns[(trial+i)%4] for i in range(3)] if trial<4 else [rng.getrandbits(128) for _ in range(3)]
                if 4<=trial<8:
                    bits=16 if trial<6 else 32
                    minimum=sum((1<<(bits-1))<<(i*bits) for i in range(128//bits))
                    values=[minimum if trial%2 else 0,minimum,minimum]
                yield template.replace('D',str(dest)),dest,values,rng.getrandbits(64),rng.getrandbits(32)&0x9f,rng.getrandbits(4)<<28,0
    gpr_edge=[0,1,0x7f,0x80,0xff,0x100,0x17f,0x180,0x7fff,0x8000,0xffff,0x10000,(1<<64)-1]
    for mask in range(16):
        for rotation in range(64):
            for value in [rng.choice(gpr_edge),rng.getrandbits(64)]:
                yield f'rmif x1,#{rotation},#{mask}',0,[rng.getrandbits(128) for _ in range(3)],value,0,rng.getrandbits(4)<<28,0
    for flags in range(16):
        for op in ['cfinv','setf8 w1','setf16 w1']:
            for value in gpr_edge+[rng.getrandbits(64) for _ in range(16)]:
                yield op,0,[rng.getrandbits(128) for _ in range(3)],value,0,flags<<28,0
    fp_edge=[0,1,0x8000000000000000,0x7ff0000000000000,0xfff0000000000000,0x7ff0000000000001,0x7ff8000000000000]
    fp_edge += [struct.unpack('<Q',struct.pack('<d',value))[0] for value in [1.0,-1.0,0.5,-0.5,2147483647.0,2147483647.5,2147483648.0,2147483648.5,-2147483648.0,-2147483648.5,-2147483649.0,4294967296.0,4294967296.5,-4294967296.5]]
    for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
        for mode in range(4):
            for raw in fp_edge+[rng.getrandbits(64) for _ in range(256)]:
                yield 'fjcvtzs w0,d1',0,[rng.getrandbits(128),raw,rng.getrandbits(128)],0,rng.getrandbits(32)&0x0800009f,rng.getrandbits(4)<<28,control|(mode<<22)

def main():
    samples=list(cases());programs=0
    with tempfile.TemporaryDirectory(prefix='trames-modern-') as folder:
        p=Path(folder)
        for offset in range(0,len(samples),96):
            batch=samples[offset:offset+96];code=['.global _start','.text','_start:','adrp x20,input','add x20,x20,:lo12:input','adrp x21,output','add x21,x21,:lo12:output']
            for ins,dest,*_ in batch:
                code+=['ldp q0,q1,[x20],#32','ldr q2,[x20],#16','ldp x1,x4,[x20],#16','ldp x5,x6,[x20],#16','msr fpsr,x4','msr nzcv,x5','msr fpcr,x6','mov x0,#0',ins,f'str q{dest},[x21],#16','str x0,[x21],#8','mrs x5,fpsr','str x5,[x21],#8','mrs x5,nzcv','str x5,[x21],#8']
            code+=['adrp x1,output','add x1,x1,:lo12:output',f'mov x2,#{len(batch)*40}','.Lwrite:','mov x0,#1','mov x8,#64','svc #0','add x1,x1,x0','sub x2,x2,x0','cbnz x2,.Lwrite','mov x0,#0','mov x8,#93','svc #0','.data','.balign 16','input:']
            for _,_,values,gpr,flags,nzcv,fpcr in batch:
                for value in values:code+=[f'.quad {value&((1<<64)-1)},{value>>64}']
                code+=[f'.quad {gpr},{flags},{nzcv},{fpcr}']
            code+=['.bss','.balign 16',f'output:.skip {len(batch)*40}']
            (p/'case.S').write_text('\n'.join(code)+'\n')
            subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-march=armv8.6-a+flagm','-o',str(p/'case.o'),str(p/'case.S')],check=True,capture_output=True)
            subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True,capture_output=True)
            actual=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(p/'case')],capture_output=True,timeout=20)
            oracle=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max',str(p/'case')],capture_output=True,timeout=20)
            assert actual.returncode==oracle.returncode==0,(offset,actual.stderr,oracle.stderr)
            assert len(actual.stdout)==len(oracle.stdout)==len(batch)*40,(offset,len(actual.stdout),len(oracle.stdout))
            for i,case in enumerate(batch):
                a=actual.stdout[i*40:(i+1)*40];b=oracle.stdout[i*40:(i+1)*40]
                assert a==b,(offset+i,case,a.hex(),b.hex())
            programs+=1
    print(f'Modern scalar/SIMD: {len(samples)} snapshots in {programs} independent QEMU comparisons')
if __name__=='__main__':main()
