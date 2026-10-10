#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Packed binary32/binary64 FP results and cumulative exceptions against QEMU."""
import os,random,struct,subprocess,tempfile
from pathlib import Path
from run import ROOT

def cases():
    rng=random.Random(19284)
    binary=['fadd','fsub','fmul','fdiv','fmla','fmls','fmax','fmin','fmaxnm','fminnm','fcmeq','fcmge','fcmgt','facge','facgt','faddp']
    unary=['fabs','fneg','fsqrt','frintn','frintp','frintm','frintz','frinta','frintx','frinti','scvtf','ucvtf','fcvtzs','fcvtzu']
    for width,lanes,arr in [(32,2,'2s'),(32,4,'4s'),(64,2,'2d')]:
        frac=23 if width==32 else 52;inf=(255 if width==32 else 2047)<<frac;sign=1<<(width-1)
        edge=[0,sign,1,sign|1,(1<<frac)-1,1<<frac,inf-1,inf,sign|inf,inf|1,inf|(1<<(frac-1)),sign|inf|123]
        def vector(): return sum((rng.choice(edge) if rng.randrange(2) else rng.getrandbits(width))<<(i*width) for i in range(lanes))
        for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
            for mode in range(4):
                for op in binary+unary:
                    line=f'{op} v0.{arr}, v1.{arr}'+(f', v2.{arr}' if op in binary else '')
                    for _ in range(2): yield line,vector(),vector(),vector(),control|(mode<<22)
    for arr in ['2s','4s','2d']:
        for val in ['1.0','-2.0','0.125','31.0']:yield f'fmov v0.{arr}, #{val}',0,0,0,0
    for reg,arr in [('s','2s'),('d','2d')]:
        for _ in range(32): yield f'faddp {reg}0, v1.{arr}',rng.getrandbits(128),0,0,0

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
