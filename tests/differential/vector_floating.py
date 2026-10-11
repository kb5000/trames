#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Packed binary32/binary64 FP results and cumulative exceptions against QEMU."""
import os,random,struct,subprocess,tempfile
from pathlib import Path
from run import ROOT

def cases():
    rng=random.Random(19284)
    binary=['fadd','fsub','fmul','fdiv','fmla','fmls','fmax','fmin','fmaxnm','fminnm','fcmeq','fcmge','fcmgt','facge','facgt','faddp','fmulx','fabd','fmaxp','fminp','fmaxnmp','fminnmp','frecps','frsqrts']
    unary=['fabs','fneg','fsqrt','frintn','frintp','frintm','frintz','frinta','frintx','frinti','scvtf','ucvtf','fcvtzs','fcvtzu','fcvtns','fcvtnu','fcvtps','fcvtpu','fcvtms','fcvtmu','fcvtas','fcvtau','frecpe','frsqrte']
    for width,lanes,arr in [(32,2,'2s'),(32,4,'4s'),(64,2,'2d')]:
        frac=23 if width==32 else 52;inf=(255 if width==32 else 2047)<<frac;sign=1<<(width-1)
        edge=[0,sign,1,sign|1,(1<<frac)-1,1<<frac,inf-1,inf,sign|inf,inf|1,inf|(1<<(frac-1)),sign|inf|123]
        def vector(): return sum((rng.choice(edge) if rng.randrange(2) else rng.getrandbits(width))<<(i*width) for i in range(lanes))
        for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
            for mode in range(4):
                for op in binary+unary:
                    line=f'{op} v0.{arr}, v1.{arr}'+(f', v2.{arr}' if op in binary else '')
                    for _ in range(2): yield line,vector(),vector(),vector(),control|(mode<<22)
    for arr,width in [('2s',32),('4s',32),('2d',64)]:
        for mode in range(4):
            for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
                state=control|(mode<<22)
                for op in ['fcmgt','fcmge','fcmeq','fcmle','fcmlt']:
                    for _ in range(4):yield f'{op} v0.{arr},v1.{arr},#0.0',rng.getrandbits(128),0,0,state
                for op in ['scvtf','ucvtf','fcvtzs','fcvtzu']:
                    for scale in [1,width]:yield f'{op} v0.{arr},v1.{arr},#{scale}',rng.getrandbits(128),0,0,state
                for op in ['fmul','fmulx','fmla','fmls']:
                    for index in range(128//width):
                        yield f'{op} v0.{arr},v1.{arr},v2.{"s" if width==32 else "d"}[{index}]',rng.getrandbits(128),rng.getrandbits(128),rng.getrandbits(128),state
                if arr=='4s':
                    for op in ['fmaxv','fminv','fmaxnmv','fminnmv']:
                        for _ in range(4):yield f'{op} s0,v1.4s',rng.getrandbits(128),0,0,state
    for reg,width in [('s',32),('d',64)]:
        frac=23 if width==32 else 52;inf=((1<<(8 if width==32 else 11))-1)<<frac;sign=1<<(width-1)
        values=[0,sign,1,sign|1,(1<<frac)-1,1<<frac,1<<(frac-2),inf-1,inf,sign|inf,inf|1,inf|(1<<(frac-1)),sign|inf|123]
        for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
            for mode in range(4):
                for op in ['frecpe','frsqrte','frecpx']:
                    for value in values+[rng.getrandbits(width) for _ in range(16)]:yield f'{op} {reg}0,{reg}1',value,0,0,control|(mode<<22)
                for op in ['frecps','frsqrts']:
                    for _ in range(32):yield f'{op} {reg}0,{reg}1,{reg}2',rng.choice(values),rng.choice(values),0,control|(mode<<22)
    for arr in ['2s','4s']:
        for op in ['urecpe','ursqrte']:
            for _ in range(64):yield f'{op} v0.{arr},v1.{arr}',rng.getrandbits(128),0,0,0
    for op in ['fcvtl','fcvtl2','fcvtn','fcvtn2','fcvtxn','fcvtxn2']:
        for mode in range(4):
            for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
                for _ in range(16):
                    operands='v0.2d,v1.4s' if op=='fcvtl2' else 'v0.2d,v1.2s' if op=='fcvtl' else ('v0.4s,v1.2d' if op.endswith('2') else 'v0.2s,v1.2d')
                    yield f'{op} {operands}',rng.getrandbits(128),0,rng.getrandbits(128),control|(mode<<22)
    for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
        for _ in range(64):yield 'fcvtxn s0,d1',rng.getrandbits(128),0,rng.getrandbits(128),control
    for reg,arr in [('s','2s'),('d','2d')]:
        for op in ['fmaxp','fminp','fmaxnmp','fminnmp']:
            for _ in range(32):yield f'{op} {reg}0,v1.{arr}',rng.getrandbits(128),0,0,0
    for arr in ['2s','4s','2d']:
        for val in ['1.0','-2.0','0.125','31.0']:yield f'fmov v0.{arr}, #{val}',0,0,0,0
    for reg,arr in [('s','2s'),('d','2d')]:
        for _ in range(32): yield f'faddp {reg}0, v1.{arr}',rng.getrandbits(128),0,0,0
    for reg,width in [('s',32),('d',64)]:
        for op in ['scvtf','ucvtf','fcvtzs','fcvtzu']:
            for mode in range(4):
                for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
                    for raw in [0,1,1<<(width-1),(1<<width)-1,*[rng.getrandbits(width) for _ in range(4)]]:
                        yield f'{op} {reg}0, {reg}1',raw,0,0,control|(mode<<22)

def program(batch):
    code=['.global _start','.text','_start:','adrp x20,results','add x20,x20,:lo12:results']
    for line,a,b,c,fpcr in batch:
        code+=['msr fpsr,xzr',f'ldr x5, ={fpcr}','msr fpcr,x5']
        for r,val in [(0,c),(1,a),(2,b)]: code += [f'ldr x5, ={val&((1<<64)-1)}',f'fmov d{r},x5',f'ldr x5, ={val>>64}',f'fmov v{r}.d[1],x5']
        code += [line,'str q0,[x20],#16','mrs x5,fpsr','str x5,[x20],#8']
    code+=['mov x0,#1','adrp x1,results','add x1,x1,:lo12:results',f'mov x2,#{len(batch)*24}','mov x8,#64','svc #0','mov x0,#0','mov x8,#93','svc #0','.ltorg','.bss','.balign 16',f'results:.skip {len(batch)*24}']
    return '\n'.join(code)+'\n'

def main():
    all_cases=list(cases());count=0
    as_=os.getenv('AARCH64_AS','aarch64-linux-gnu-as');ld=os.getenv('AARCH64_LD','aarch64-linux-gnu-ld');qemu=os.getenv('QEMU_AARCH64','qemu-aarch64')
    with tempfile.TemporaryDirectory(prefix='trames-vector-fp-') as folder:
        path=Path(folder);src=path/'v.S';obj=path/'v.o';exe=path/'v'
        for offset in range(0,len(all_cases),24):
            batch=all_cases[offset:offset+24];src.write_text(program(batch))
            subprocess.run([as_,'-o',str(obj),str(src)],check=True,capture_output=True);subprocess.run([ld,'-static','--build-id=none','-z','noexecstack','-o',str(exe),str(obj)],check=True,capture_output=True)
            a=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(exe)],capture_output=True,timeout=15);b=subprocess.run([qemu,'-cpu','cortex-a53',str(exe)],capture_output=True,timeout=15)
            if a.returncode or b.returncode:raise AssertionError((offset,a,b))
            if len(a.stdout)!=len(batch)*24 or len(b.stdout)!=len(batch)*24:raise AssertionError('short vector snapshot')
            for i,case in enumerate(batch):
                actual=struct.unpack('<QQQ',a.stdout[i*24:(i+1)*24]);ref=struct.unpack('<QQQ',b.stdout[i*24:(i+1)*24])
                if actual!=ref:raise AssertionError(f'Vector FP {offset+i} {case}: {tuple(hex(v) for v in actual)} != {tuple(hex(v) for v in ref)}')
            count+=1
    print(f'Vector FP differential passed: {len(all_cases)} cases in {count} QEMU program comparisons')
if __name__=='__main__':main()
