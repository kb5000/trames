#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Scalar ARMv8 FP bit results, FPSR and NZCV against independent QEMU."""
import os, random, struct, subprocess, tempfile
from pathlib import Path
from run import ROOT


def cases(seed):
    rng = random.Random(seed)
    for double in [False, True]:
        reg = 'd' if double else 's'; frac=52 if double else 23; ebits=11 if double else 8
        sign=1<<(frac+ebits); inf=((1<<ebits)-1)<<frac
        edge=[0, sign, 1, sign|1, (1<<frac)-1, 1<<frac, inf-1, inf, sign|inf,
              inf|(1<<(frac-1)), inf|1, sign|inf|123, 0x3ff0000000000000 if double else 0x3f800000,
              0x3ff8000000000000 if double else 0x3fc00000]
        def val(): return rng.choice(edge) if rng.randrange(2) else rng.getrandbits(64 if double else 32)
        binary=['fmul','fdiv','fadd','fsub','fmax','fmin','fmaxnm','fminnm','fnmul']
        unary=['fmov','fabs','fneg','fsqrt','frintn','frintp','frintm','frintz','frinta','frintx','frinti']
        for mode in range(4):
            for control in [0,1<<24,1<<25,(1<<24)|(1<<25)]:
                for op in binary+unary+['fmadd','fmsub','fnmadd','fnmsub','fcmp','fcmpe','fccmp','fccmpe','fcsel','fcvt']:
                    for _ in range(3):
                        a,b,c=val(),val(),val()
                        if op in binary: line=f'{op} {reg}0, {reg}1, {reg}2'
                        elif op in unary: line=f'{op} {reg}0, {reg}1'
                        elif op in ['fmadd','fmsub','fnmadd','fnmsub']: line=f'{op} {reg}0, {reg}1, {reg}2, {reg}3'
                        elif op in ['fcmp','fcmpe']: line=f'{op} {reg}1, {reg}2'
                        elif op in ['fccmp','fccmpe']: line=f'{op} {reg}1, {reg}2, #9, {rng.choice(["eq","ne"])}'
                        elif op=='fcsel': line=f'fcsel {reg}0, {reg}1, {reg}2, eq'
                        else: line=f'fcvt {"s" if double else "d"}0, {reg}1'
                        yield line,a,b,c,control|(mode<<22),0
                for op in ['scvtf','ucvtf','fcvtns','fcvtnu','fcvtas','fcvtau','fcvtps','fcvtpu','fcvtms','fcvtmu','fcvtzs','fcvtzu']:
                    for wide in [False,True]:
                        gpr='x' if wide else 'w'; integer=rng.getrandbits(64)
                        for scaled in ([False,True] if op in ['scvtf','ucvtf','fcvtzs','fcvtzu'] else [False]):
                            suffix=f', #{rng.randint(1,64 if wide else 32)}' if scaled else ''
                            line=f'{op} {reg}0, {gpr}4{suffix}' if op in ['scvtf','ucvtf'] else f'{op} {gpr}0, {reg}1{suffix}'
                            yield line,val(),val(),val(),control|(mode<<22),integer


def program(batch):
    code=['.global _start','.text','_start:', 'adrp x20, results', 'add x20, x20, :lo12:results']
    for line,a,b,c,fpcr,integer in batch:
        code += ['msr fpsr, xzr','msr nzcv, xzr',f'ldr x5, ={fpcr}', 'msr fpcr, x5', 'fmov d0, xzr','mov x0, #0']
        for r,value in [(1,a),(2,b),(3,c)]:
            code += [f'ldr x5, ={value}',f'fmov d{r}, x5']
        code += [f'ldr x4, ={integer}',line]
        if line.split()[0].startswith('fcvt') and not line.startswith('fcvt '):
            code += ['str x0, [x20], #8']
        else: code += ['str d0, [x20], #8']
        code += ['mrs x5, fpsr','str x5, [x20], #8','mrs x5, nzcv','str x5, [x20], #8']
    code += ['mov x0,#1','adrp x1,results','add x1,x1,:lo12:results',f'mov x2,#{len(batch)*24}','mov x8,#64','svc #0','mov x0,#0','mov x8,#93','svc #0','.ltorg','.bss','.balign 16',f'results: .skip {len(batch)*24}']
    return '\n'.join(code)+'\n'


def main():
    runner=str(ROOT/'target/debug/trames-run'); as_=os.getenv('AARCH64_AS','aarch64-linux-gnu-as'); ld=os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'); qemu=os.getenv('QEMU_AARCH64','qemu-aarch64')
    all_cases=list(cases(73281)); count=0
    with tempfile.TemporaryDirectory(prefix='trames-fp-') as folder:
        path=Path(folder); src=path/'fp.S'; obj=path/'fp.o'; exe=path/'fp'
        for offset in range(0,len(all_cases),32):
            batch=all_cases[offset:offset+32];src.write_text(program(batch))
            subprocess.run([as_,'-o',str(obj),str(src)],check=True,capture_output=True)
            subprocess.run([ld,'-static','--build-id=none','-z','noexecstack','-o',str(exe),str(obj)],check=True,capture_output=True)
            actual=subprocess.run([runner, "--memory",str(exe)],capture_output=True,timeout=15)
            ref=subprocess.run([qemu,'-cpu','cortex-a53',str(exe)],capture_output=True,timeout=15)
            if actual.returncode or ref.returncode: raise AssertionError((offset,actual,ref))
            if len(actual.stdout)!=len(batch)*24 or len(ref.stdout)!=len(batch)*24: raise AssertionError('short FP snapshot')
            for i,case in enumerate(batch):
                a=struct.unpack('<QQQ',actual.stdout[i*24:(i+1)*24]); b=struct.unpack('<QQQ',ref.stdout[i*24:(i+1)*24])
                if a!=b: raise AssertionError(f'FP case {offset+i} {case}: Trames {tuple(hex(v) for v in a)} != QEMU {tuple(hex(v) for v in b)}')
            count+=1
    print(f'FP differential passed: {len(all_cases)} scalar cases in {count} QEMU program comparisons')

if __name__=='__main__': main()
