#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Binary16 results, exceptions, conversions and AHP against independent QEMU."""
import os, random, struct, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

def cases():
    rng = random.Random(621059)
    edge = [0,0x8000,1,0x8001,0x3ff,0x400,0x3c00,0x3e00,0x7bff,0x7c00,0xfc00,0x7c01,0x7e00,0xffff]
    controls = [0,1<<19,1<<24,1<<25,1<<26,(1<<19)|(1<<24)|(1<<25)|(1<<26)]
    def case(line, a, b=0, c=0, integer=0, control=0):
        return line, [rng.getrandbits(128), a, b, c], integer, control
    # Every half encoding, including alternate-half exponent 31, is widened.
    for control in [0,1<<26]:
        for raw in range(1<<16): yield case('fcvt s0,h1',raw,control=control)
    for mode in range(4):
        for control in controls:
            fpcr = control | (mode<<22)
            for dest,source,width in [('h','s',32),('h','d',64),('s','h',16),('d','h',16)]:
                raw_values=edge if width==16 else ([0,1,1<<(width-1),((1<<(8 if width==32 else 11))-1)<<(23 if width==32 else 52),(1<<width)-1])
                for raw in raw_values+[rng.getrandbits(width) for _ in range(24)]:
                    yield case(f'fcvt {dest}0,{source}1',raw,control=fpcr)
            for op,operands in [('fcvtl','v0.4s,v1.4h'),('fcvtl2','v0.4s,v1.8h'),('fcvtn','v0.4h,v1.4s'),('fcvtn2','v0.8h,v1.4s')]:
                for _ in range(24):yield case(f'{op} {operands}',rng.getrandbits(128),control=fpcr)
            binary=['fmul','fdiv','fadd','fsub','fmax','fmin','fmaxnm','fminnm','fnmul']
            unary=['fmov','fabs','fneg','fsqrt','frintn','frintp','frintm','frintz','frinta','frintx','frinti']
            for op in binary+unary+['fmadd','fmsub','fnmadd','fnmsub','fcmp','fcmpe','fccmp','fccmpe','fcsel']:
                for _ in range(16):
                    a,b,c=[rng.choice(edge) if rng.randrange(2) else rng.getrandbits(16) for _ in range(3)]
                    if op in binary: line=f'{op} h0,h1,h2'
                    elif op in unary:line=f'{op} h0,h1'
                    elif op in ['fmadd','fmsub','fnmadd','fnmsub']:line=f'{op} h0,h1,h2,h3'
                    elif op in ['fcmp','fcmpe']:line=f'{op} h1,h2'
                    elif op in ['fccmp','fccmpe']:line=f'{op} h1,h2,#9,{rng.choice(["eq","ne"])}'
                    else:line='fcsel h0,h1,h2,eq'
                    yield case(line,a,b,c,control=fpcr)
            for reg,width in [('w',32),('x',64)]:
                for op in ['scvtf','ucvtf','fcvtns','fcvtnu','fcvtas','fcvtau','fcvtps','fcvtpu','fcvtms','fcvtmu','fcvtzs','fcvtzu']:
                    for _ in range(8):
                        source=rng.choice(edge) if rng.randrange(2) else rng.getrandbits(16)
                        integer=rng.getrandbits(width)
                        line=f'{op} h0,{reg}4' if op in ['scvtf','ucvtf'] else f'{op} {reg}0,h1'
                        yield case(line,source,integer=integer,control=fpcr)
                        if op in ['scvtf','ucvtf','fcvtzs','fcvtzu']:
                            yield case(line+f', #{rng.choice([1,width])}',source,integer=integer,control=fpcr)
                for _ in range(16):
                    yield case(f'fmov {reg}0,h1',rng.getrandbits(128),control=fpcr)
                    yield case(f'fmov h0,{reg}4',0,integer=rng.getrandbits(width),control=fpcr)
            for immediate in ['1.0','-2.0','0.125','31.0']:
                yield case(f'fmov h0,#{immediate}',0,control=fpcr)
            def vector():
                return sum((rng.choice(edge) if rng.randrange(2) else rng.getrandbits(16)) << (i*16) for i in range(8))
            for arr in ['4h','8h']:
                for immediate in ['1.0','-2.0','0.125','31.0']:
                    yield case(f'fmov v0.{arr},#{immediate}',0,control=fpcr)
                binary=['fadd','fsub','fmul','fdiv','fmla','fmls','fmax','fmin','fmaxnm','fminnm','fcmeq','fcmge','fcmgt','facge','facgt','faddp','fmulx','fabd','fmaxp','fminp','fmaxnmp','fminnmp','frecps','frsqrts']
                unary=['fabs','fneg','fsqrt','frintn','frintp','frintm','frintz','frinta','frintx','frinti','scvtf','ucvtf','fcvtzs','fcvtzu','fcvtns','fcvtnu','fcvtps','fcvtpu','fcvtms','fcvtmu','fcvtas','fcvtau','frecpe','frsqrte']
                for op in binary+unary:
                    line=f'{op} v0.{arr},v1.{arr}'+(f',v2.{arr}' if op in binary else '')
                    for trial in range(8):yield case(line.replace('v1','v0') if trial==0 else line,vector(),vector(),control=fpcr)
                for op in ['fcmgt','fcmge','fcmeq','fcmle','fcmlt']:
                    for _ in range(8):yield case(f'{op} v0.{arr},v1.{arr},#0.0',vector(),control=fpcr)
                for op in ['fmaxv','fminv','fmaxnmv','fminnmv']:
                    for _ in range(8):yield case(f'{op} h0,v1.{arr}',vector(),control=fpcr)
                for op in ['fmul','fmulx','fmla','fmls']:
                    for index in range(8):yield case(f'{op} v0.{arr},v1.{arr},v2.h[{index}]',vector(),vector(),control=fpcr)
                for op in ['scvtf','ucvtf','fcvtzs','fcvtzu']:
                    for scale in [1,16]:yield case(f'{op} v0.{arr},v1.{arr},#{scale}',vector(),control=fpcr)
            for op in ['fmulx','fabd','frecps','frsqrts','fcmeq','fcmge','fcmgt','facge','facgt']:
                for _ in range(8):yield case(f'{op} h0,h1,h2',rng.choice(edge),rng.choice(edge),control=fpcr)
            for op in ['frecpe','frsqrte','frecpx','scvtf','ucvtf','fcvtzs','fcvtzu','fcvtns','fcvtnu','fcvtas','fcvtau','fcvtps','fcvtpu','fcvtms','fcvtmu']:
                for _ in range(8):yield case(f'{op} h0,h1',rng.choice(edge),control=fpcr)
            for op in ['faddp','fmaxp','fminp','fmaxnmp','fminnmp']:
                for _ in range(8):yield case(f'{op} h0,v1.2h',vector(),control=fpcr)
            for op in ['fcmgt','fcmge','fcmeq','fcmle','fcmlt']:
                for _ in range(8):yield case(f'{op} h0,h1,#0.0',rng.choice(edge),control=fpcr)
            for op in ['fmul','fmulx','fmla','fmls']:
                for index in range(8):yield case(f'{op} h0,h1,v2.h[{index}]',rng.choice(edge),vector(),control=fpcr)
            for op in ['scvtf','ucvtf','fcvtzs','fcvtzu']:
                for scale in [1,16]:yield case(f'{op} h0,h1,#{scale}',rng.choice(edge),control=fpcr)

def main():
    samples=list(cases()); programs=0
    with tempfile.TemporaryDirectory(prefix='trames-half-fp-') as folder:
        p=Path(folder)
        for offset in range(0,len(samples),128):
            batch=samples[offset:offset+128]
            code=['.global _start','.text','_start:','adrp x20,output','add x20,x20,:lo12:output','adrp x21,input','add x21,x21,:lo12:input']
            for line,_,_,_ in batch:
                code+=['ldp q0,q1,[x21],#32','ldp q2,q3,[x21],#32','ldp x4,x5,[x21],#16','msr fpcr,x5','msr fpsr,xzr','msr nzcv,xzr','mov x0,#0',line,'str q0,[x20],#16','str x0,[x20],#8','mrs x5,fpsr','str x5,[x20],#8','mrs x5,nzcv','str x5,[x20],#8']
            code+=['adrp x1,output','add x1,x1,:lo12:output',f'mov x2,#{len(batch)*40}','.Lwrite:','mov x0,#1','mov x8,#64','svc #0','add x1,x1,x0','sub x2,x2,x0','cbnz x2,.Lwrite','mov x0,#0','mov x8,#93','svc #0','.data','.balign 16','input:']
            for _,values,integer,control in batch:
                for value in values:code+=[f'.quad {value&((1<<64)-1)},{value>>64}']
                code+=[f'.quad {integer},{control}']
            code+=['.bss','.balign 16',f'output:.skip {len(batch)*40}']
            (p/'case.S').write_text('\n'.join(code)+'\n')
            subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-march=armv8.2-a+fp16','-o',str(p/'case.o'),str(p/'case.S')],check=True,capture_output=True)
            subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True,capture_output=True)
            actual=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(p/'case')],capture_output=True,timeout=20)
            oracle=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max',str(p/'case')],capture_output=True,timeout=20)
            assert actual.returncode==oracle.returncode==0,(offset,actual.stderr,oracle.stderr)
            assert len(actual.stdout)==len(oracle.stdout)==len(batch)*40,(offset,len(actual.stdout),len(oracle.stdout))
            for i,case in enumerate(batch):
                a=actual.stdout[i*40:(i+1)*40];b=oracle.stdout[i*40:(i+1)*40]
                assert a==b,(offset+i,case,a.hex(),b.hex())
            programs+=1
    print(f'Half FP: {len(samples)} snapshots in {programs} QEMU comparisons (exhaustive IEEE/AHP half-to-single encodings)')
if __name__=='__main__':main()
