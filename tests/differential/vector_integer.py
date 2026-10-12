#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent QEMU checks for integer AdvSIMD, including aliasing and FPSR.QC."""
import os, random, struct, subprocess, tempfile
from pathlib import Path
from run import RUNNER
ROOT=Path(__file__).resolve().parents[2]

def instructions():
    for width in [64,128]:
        for bits,letter in [(8,'b'),(16,'h'),(32,'s'),(64,'d')]:
            arr=f'{width//bits}{letter}'
            if bits==64 and width==64: continue
            if bits<64:
                for op in ['mul','mla','mls','smax','smin','umax','umin','sabd','uabd','saba','uaba','shadd','uhadd','shsub','uhsub','srhadd','urhadd']:
                    yield f'{op} vD.{arr},v1.{arr},v2.{arr}'
                for op in ['clz','cls']:yield f'{op} vD.{arr},v1.{arr}'
            for op in ['sqadd','uqadd','sqsub','uqsub','srshl','urshl','sqshl','uqshl','sqrshl','uqrshl']:
                yield f'{op} vD.{arr},v1.{arr},v2.{arr}'
            for op in ['suqadd','usqadd','sqabs','sqneg']:yield f'{op} vD.{arr},v1.{arr}'
            for op in ['cmgt','cmge','cmeq','cmle','cmlt']:yield f'{op} vD.{arr},v1.{arr},#0'
            for block in [16,32,64]:
                if bits<block:yield f'rev{block} vD.{arr},v1.{arr}'
            if bits in [16,32]:
                for op in ['sqdmulh','sqrdmulh']:yield f'{op} vD.{arr},v1.{arr},v2.{arr}'
                for op in ['mul','mla','mls','sqdmulh','sqrdmulh']:
                    for index in range(128//bits):
                        yield f'{op} vD.{arr},v1.{arr},v2.{letter}[{index}]'
                if bits==32:
                    yield f'mov v18.16b,v2.16b\nmul vD.{arr},v1.{arr},v18.s[3]'
            if bits<64:
                for op in ['sqshlu','sqshl','uqshl']:
                    for distance in [0,1,bits-1]:yield f'{op} vD.{arr},v1.{arr},#{distance}'
            if bits<64 and (bits<32 or width==128):
                for op in ['addv','smaxv','sminv','umaxv','uminv']:yield f'{op} {letter}D,v1.{arr}'
                wider={8:'h',16:'s',32:'d'}[bits]
                for op in ['saddlv','uaddlv']:yield f'{op} {wider}D,v1.{arr}'
            if bits<64:
                wider={8:'h',16:'s',32:'d'}[bits]
                for op in ['saddlp','uaddlp','sadalp','uadalp']:yield f'{op} vD.{width//(bits*2)}{wider},v1.{arr}'
        byte_arr=f'{width//8}b'
        for op in ['cnt','not','rbit']:yield f'{op} vD.{byte_arr},v1.{byte_arr}'
        for op in ['bic','orn','bsl','bit','bif','pmul']:yield f'{op} vD.{byte_arr},v1.{byte_arr},v2.{byte_arr}'
        for offset in [0,1,width//8-1]:yield f'ext vD.{byte_arr},v1.{byte_arr},v2.{byte_arr},#{offset}'
    for bits,letter,wider in [(8,'b','h'),(16,'h','s'),(32,'s','d')]:
        for upper in [False,True]:
            low=f'{128//bits if upper else 64//bits}{letter}';high=f'{64//bits}{wider}'
            dest=f'{128//bits if upper else 64//bits}{letter}'
            for op in ['sqxtn','uqxtn','sqxtun']:yield f'{op}{"2" if upper else ""} vD.{dest},v1.{high}'
            for op in ['addhn','raddhn','subhn','rsubhn']:yield f'{op}{"2" if upper else ""} vD.{dest},v1.{high},v2.{high}'
            for op in ['shrn','rshrn','sqshrn','uqshrn','sqrshrn','uqrshrn','sqshrun','sqrshrun']:
                for distance in [1,bits]:yield f'{op}{"2" if upper else ""} vD.{dest},v1.{high},#{distance}'
            for op in ['saddl','uaddl','ssubl','usubl','sabdl','uabdl','sabal','uabal','smull','umull','smlal','umlal','smlsl','umlsl']:
                yield f'{op}{"2" if upper else ""} vD.{high},v1.{low},v2.{low}'
            for op in ['saddw','uaddw','ssubw','usubw']:yield f'{op}{"2" if upper else ""} vD.{high},v1.{high},v2.{low}'
            yield f'shll{"2" if upper else ""} vD.{high},v1.{low},#{bits}'
            if bits>8:
                for op in ['sqdmull','sqdmlal','sqdmlsl']:yield f'{op}{"2" if upper else ""} vD.{high},v1.{low},v2.{low}'
                for op in ['smull','umull','smlal','umlal','smlsl','umlsl','sqdmull','sqdmlal','sqdmlsl']:
                    for index in range(128//bits):
                        yield f'{op}{"2" if upper else ""} vD.{high},v1.{low},v2.{letter}[{index}]'
    for upper in [False,True]:
        yield f'pmull{"2" if upper else ""} vD.8h,v1.{16 if upper else 8}b,v2.{16 if upper else 8}b'
    for bits,letter in [(8,'b'),(16,'h'),(32,'s'),(64,'d')]:
        for op in ['sqadd','uqadd','sqsub','uqsub']:yield f'{op} {letter}D,{letter}1,{letter}2'
        for op in ['suqadd','usqadd','sqabs','sqneg']:yield f'{op} {letter}D,{letter}1'
        for op in ['sqshlu','sqshl','uqshl']:
            for distance in [0,1,bits-1]:yield f'{op} {letter}D,{letter}1,#{distance}'
        for op in ['sqshl','uqshl','sqrshl','uqrshl']:yield f'{op} {letter}D,{letter}1,{letter}2'
        if bits<64:
            wider={8:'h',16:'s',32:'d'}[bits]
            for op in ['sqxtn','uqxtn','sqxtun']:yield f'{op} {letter}D,{wider}1'
            for op in ['sqshrn','uqshrn','sqrshrn','uqrshrn','sqshrun','sqrshrun']:
                for distance in [1,bits]:yield f'{op} {letter}D,{wider}1,#{distance}'
    for op in ['ssra','usra','srshr','urshr','srsra','ursra','sri']:
        for distance in [1,32,63,64]:yield f'{op} dD,d1,#{distance}'
    for distance in [0,1,32,63]:yield f'sli dD,d1,#{distance}'
    for op in ['sshl','ushl','srshl','urshl']:yield f'{op} dD,d1,d2'
    for bits,letter,wider in [(16,'h','s'),(32,'s','d')]:
        for op in ['sqdmulh','sqrdmulh']:
            yield f'{op} {letter}D,{letter}1,{letter}2'
            for index in range(128//bits):yield f'{op} {letter}D,{letter}1,v2.{letter}[{index}]'
        for op in ['sqdmull','sqdmlal','sqdmlsl']:
            yield f'{op} {wider}D,{letter}1,{letter}2'
            for index in range(128//bits):yield f'{op} {wider}D,{letter}1,v2.{letter}[{index}]'

def main():
    rng=random.Random(281990)
    patterns=[0,(1<<128)-1,int.from_bytes(bytes([128,127,0,255]*4),'little'),int.from_bytes(bytes([1,254,129,126]*4),'little')]
    cases=[]
    for template in instructions():
        for dest in [0,1,2]:
            for trial in range(6):
                values=[rng.getrandbits(128) for _ in range(3)] if trial>=4 else [patterns[(trial+i)%4] for i in range(3)]
                cases.append((template.replace('D',str(dest)),dest,values))
    with tempfile.TemporaryDirectory(prefix='trames-vector-integer-') as folder:
        p=Path(folder)
        for offset in range(0,len(cases),96):
            batch=cases[offset:offset+96];source=['.global _start','.text','_start:','adrp x20,input','add x20,x20,:lo12:input','adrp x21,output','add x21,x21,:lo12:output']
            for ins,dest,values in batch:
                source+=['ldp q0,q1,[x20],#32','ldr q2,[x20],#16','msr fpsr,xzr',ins,f'str q{dest},[x21],#16','mrs x5,fpsr','str x5,[x21],#8']
            source+=['adrp x1,output','add x1,x1,:lo12:output',f'mov x2,#{len(batch)*24}','.Lwrite:','mov x0,#1','mov x8,#64','svc #0','add x1,x1,x0','sub x2,x2,x0','cbnz x2,.Lwrite','mov x0,#0','mov x8,#93','svc #0','.data','.balign 16','input:']
            for _,_,values in batch:
                for value in values:source += [f'.quad {value&((1<<64)-1)},{value>>64}']
            source+=['.bss','.balign 16',f'output: .skip {len(batch)*24}']
            (p/'case.S').write_text('\n'.join(source)+'\n')
            subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-o',str(p/'case.o'),str(p/'case.S')],check=True,capture_output=True)
            subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True,capture_output=True)
            actual=subprocess.run([RUNNER,'--memory',str(p/'case')],capture_output=True,timeout=20)
            oracle=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','cortex-a53',str(p/'case')],capture_output=True,timeout=20)
            assert not actual.returncode and not oracle.returncode,(offset,actual.stderr,oracle.stderr)
            assert len(actual.stdout)==len(oracle.stdout)==len(batch)*24,(offset,len(actual.stdout),len(oracle.stdout))
            for i,case in enumerate(batch):
                a=actual.stdout[i*24:(i+1)*24];b=oracle.stdout[i*24:(i+1)*24]
                assert a==b,(offset+i,case,a.hex(),b.hex())
    print(f'Integer AdvSIMD: {len(cases)} register/FPSR snapshots matched independent QEMU')
if __name__=='__main__':main()
