#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""AArch64 CRC, LSE and paired exclusive accesses against independent QEMU."""
import os,random,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def cases():
    rng=random.Random(890172)
    edge=[0,1,(1<<64)-1,1<<63,0x7fffffffffffffff,0x800000007fffffff]
    for width,tail,reg in [(8,'b','w'),(16,'h','w'),(32,'','w'),(64,'','x')]:
        for ordering in ['','a','l','al']:
            for op in ['swp','ldadd','ldclr','ldeor','ldset','ldsmax','ldsmin','ldumax','ldumin']:
                for trial in range(8):
                    old=edge[trial] if trial<6 else rng.getrandbits(64)
                    source=edge[(trial+2)%6] if trial<6 else rng.getrandbits(64)
                    yield f'{op}{ordering}{tail} {reg}0,{reg}2,[x10]',[old,rng.getrandbits(64),source,rng.getrandbits(64)],[]
            for match in [False,True]:
                for trial in range(8):
                    old=edge[trial] if trial<6 else rng.getrandbits(64)
                    expected=old if match else old^1
                    new=rng.getrandbits(64)
                    yield f'cas{ordering}{tail} {reg}0,{reg}2,[x10]',[old,rng.getrandbits(64),expected,new],['mov x2,x3']
    for reg in ['w','x']:
        for order in ['','a','l','al']:
            for match in [False,True]:
                for _ in range(12):
                    old=[rng.getrandbits(32 if reg=='w' else 64) for _ in range(2)]
                    values=[*old,old[0] if match else old[0]^1,old[1]]
                    setup=['mov x1,x3',f'ldr x2,={rng.getrandbits(64)}',f'ldr x3,={rng.getrandbits(64)}']
                    yield f'casp{order} {reg}0,{reg}1,{reg}2,{reg}3,[x10]',values,setup
    for width,reg in [(32,'w'),(64,'x')]:
        for load in ['ldxp','ldaxp']:
            for store in ['stxp','stlxp']:
                for clear in [False,True]:
                    for _ in range(12):
                        values=[rng.getrandbits(64) for _ in range(4)]
                        # Consecutive exclusives with no intervening snapshot write.
                        code=f'{load} {reg}0,{reg}1,[x10]\n'+('clrex\n' if clear else '')+f'{store} w4,{reg}2,{reg}3,[x10]'
                        yield code,values,[]
    for op in ['ldtrb','ldtrh','ldtrsb','ldtrsh','ldtrsw','ldtr','sttrb','sttrh','sttr','ldaprb','ldaprh','ldapr']:
        regs=['w','x'] if op in ['ldtrsb','ldtrsh','ldtr','sttr','ldapr'] else ['x'] if op=='ldtrsw' else ['w']
        for reg in regs:
            for _ in range(12):yield f'{op} {reg}2,[x10]',[rng.getrandbits(64) for _ in range(4)],[]
    for polynomial in ['', 'c']:
        for suffix,reg in [('b','w'),('h','w'),('w','w'),('x','x')]:
            for dest in [0,1,2]:
                for _ in range(24):
                    yield f'crc32{polynomial}{suffix} w{dest},w1,{reg}2',[rng.getrandbits(64) for _ in range(4)],[]

def main():
    all_cases=list(cases());count=0
    with tempfile.TemporaryDirectory(prefix='trames-atomic-crc-') as folder:
        p=Path(folder)
        for offset in range(0,len(all_cases),64):
            batch=all_cases[offset:offset+64]
            src=['.global _start','.text','_start:','adrp x20,output','add x20,x20,:lo12:output','adrp x10,memory','add x10,x10,:lo12:memory']
            for code,values,setup in batch:
                src += [f'ldr x0,={values[0]}',f'ldr x1,={values[1]}','stp x0,x1,[x10]',f'ldr x0,={values[2]}',f'ldr x3,={values[3]}','mov x2,x0','mov x4,#0','msr nzcv,xzr']+setup+[code,'stp x0,x1,[x20],#16','stp x2,x3,[x20],#16','str x4,[x20],#8','ldp x5,x6,[x10]','stp x5,x6,[x20],#16','mrs x5,nzcv','str x5,[x20],#8']
            src += ['mov x0,#1','adrp x1,output','add x1,x1,:lo12:output',f'mov x2,#{len(batch)*64}','.Lwrite:','mov x0,#1','mov x8,#64','svc #0','add x1,x1,x0','sub x2,x2,x0','cbnz x2,.Lwrite','mov x0,#0','mov x8,#93','svc #0','.ltorg','.data','.balign 16','memory:.skip 16','.bss','.balign 16',f'output:.skip {len(batch)*64}']
            (p/'case.S').write_text('\n'.join(src)+'\n')
            subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-march=armv8.3-a','-o',str(p/'case.o'),str(p/'case.S')],check=True,capture_output=True)
            subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True,capture_output=True)
            a=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(p/'case')],capture_output=True,timeout=20)
            b=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','max',str(p/'case')],capture_output=True,timeout=20)
            assert a.returncode==b.returncode==0,(offset,a.stderr,b.stderr)
            assert len(a.stdout)==len(b.stdout)==len(batch)*64,(len(a.stdout),len(b.stdout))
            for i,case in enumerate(batch):
                actual=a.stdout[i*64:(i+1)*64];expected=b.stdout[i*64:(i+1)*64]
                assert actual==expected,(offset+i,case,actual.hex(),expected.hex())
            count+=1
    print(f'Atomics/CRC: {len(all_cases)} snapshots in {count} QEMU comparisons')
if __name__=='__main__':main()
