#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Vector INS aliases and variable shifts, including negative/oversize shifts."""
import os, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def main():
    source=['.text','.global _start','_start:','adr x9, input','adr x10, output']; cases=0
    for instruction in ['movi v31.16b,#2', 'movi v0.8b,#255', 'movi v0.8h,#17,lsl #8', 'movi v0.4s,#19,lsl #24', 'movi v0.4s,#23,msl #16', 'movi v0.2d,#0xff00ff00ff00ff00', 'mvni v0.8h,#31', 'mvni v0.4s,#29,msl #8', 'orr v0.4s,#41,lsl #16', 'bic v0.8h,#43,lsl #8', 'fmov v0.4s,#1.5', 'fmov v0.2d,#-2.0']:
        destination = 31 if instruction.startswith('movi v31.') else 0
        source += ['ldr q0,[x9]', instruction, f'str q{destination},[x10],#16']; cases += 1
    for bits,letter in [(8,'b'),(16,'h'),(32,'s'),(64,'d')]:
        for destination in range(128//bits):
            for origin in range(128//bits):
                for register in [0,1]:
                    source+=['ldp q0, q1, [x9]',f'ins v{register}.{letter}[{destination}], v0.{letter}[{origin}]',f'str q{register}, [x10], #16']; cases+=1
        for lane in range(128//bits):
            for register in [0,1]:
                source+=['ldp q0, q1, [x9]',f'dup {letter}{register}, v0.{letter}[{lane}]',f'str q{register}, [x10], #16']; cases+=1
        for width in [64,128]:
            if bits < 64:
                for register in [0,1,2]:
                    upper = width == 128
                    source+=['ldp q0, q1, [x9]','ldr q2, [x9]',f'xtn{"2" if upper else ""} v{register}.{128//bits if upper else 64//bits}{letter}, v0.{64//bits}{dict([(8,"h"),(16,"s"),(32,"d")])[bits]}',f'str q{register}, [x10], #16']; cases+=1
            if bits==64 and width==64: continue
            for lane in range(128//bits):
                for register in [0,1]:
                    source+=['ldp q0, q1, [x9]',f'dup v{register}.{width//bits}{letter}, v0.{letter}[{lane}]',f'str q{register}, [x10], #16']; cases+=1
            for operation in ['add','sub','cmeq','cmgt','cmge','cmhi','cmhs','cmtst','addp','abs','neg']:
                for register in [0,1,2]:
                    arrangement=f'{width//bits}{letter}'
                    operands=f'v0.{arrangement}' if operation in ['abs','neg'] else f'v0.{arrangement}, v1.{arrangement}'
                    source+=['ldp q0, q1, [x9]',f'{operation} v{register}.{arrangement}, {operands}',f'str q{register}, [x10], #16']; cases+=1
            for operation in ['uzp1','uzp2','trn1','trn2','zip1','zip2']:
                for register in [0,1,2]:
                    source+=['ldp q0, q1, [x9]',f'{operation} v{register}.{width//bits}{letter}, v0.{width//bits}{letter}, v1.{width//bits}{letter}',f'str q{register}, [x10], #16']; cases+=1
            for operation in ['sshr','ushr','ssra','usra','srshr','urshr','srsra','ursra','sri','shl','sli']:
                shifts=range(bits) if operation in ['shl','sli'] else range(1,bits+1)
                for shift in shifts:
                    for register in [0,1]:
                        source+=['ldp q0, q1, [x9]',f'{operation} v{register}.{width//bits}{letter}, v0.{width//bits}{letter}, #{shift}',f'str q{register}, [x10], #16']; cases+=1
            for operation in ['sshl','ushl']:
                for shift in [-128,-65,-64,-63,-33,-32,-31,-17,-16,-15,-9,-8,-7,-1,0,1,7,8,9,15,16,17,31,32,33,63,64,65,127]:
                    for register in [0,1,2]:
                        source+=['ldp q0, q1, [x9]',f'mov x5, #{(shift&255)|0x5500}',f'dup v1.{128//bits}{letter}, '+('x5' if bits==64 else 'w5'),f'{operation} v{register}.{width//bits}{letter}, v0.{width//bits}{letter}, v1.{width//bits}{letter}',f'str q{register}, [x10], #16']; cases+=1
    for operation in ['tbl','tbx']:
        for width in [8,16]:
            for base in [0,27,29,30,31]:
                for count in [1,2,3,4]:
                    for destination in [0,28,31]:
                        source += [f'movi v{k}.16b, #{k*7}' for k in range(32)]
                        source += ['adr x5, indices', 'ldr q28, [x5]',f'{operation} v{destination}.{width}b, '+ '{'+', '.join(f'v{(base+k)%32}.16b' for k in range(count))+'}, v28.'+str(width)+'b',f'str q{destination}, [x10], #16']; cases+=1
    # Keep each write inside the Linux ABI buffer budget.
    source+=['adr x1, output',f'ldr x2, ={cases*16}','.Lwrite:', 'mov x0, #1','mov x8, #64','svc #0','add x1, x1, x0','sub x2, x2, x0','cbnz x2, .Lwrite','mov x0, #0','mov x8, #93','svc #0','.ltorg','.data','.balign 16','input:','.byte 0,255,128,127,1,254,129,126,254,1,255,0,127,128,126,129','.byte 255,0,127,128,254,1,126,129,1,254,0,255,128,127,129,126','indices:', '.byte 0,15,16,31,32,47,48,63,64,65,127,128,254,255,1,62','.bss','.balign 16',f'output: .skip {cases*16}']
    with tempfile.TemporaryDirectory(prefix='trames-copy-shift-') as directory:
        p=Path(directory); (p/'case.S').write_text('\n'.join(source)+'\n')
        subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-o',str(p/'case.o'),str(p/'case.S')],check=True)
        subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True)
        a=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(p/'case')],capture_output=True,timeout=30)
        b=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','cortex-a53',str(p/'case')],capture_output=True,timeout=30)
        assert a.returncode==0 and b.returncode==0 and len(b.stdout)==cases*16,(a.returncode,a.stderr,b.returncode,b.stderr,len(b.stdout),cases)
        assert (a.stdout,a.stderr)==(b.stdout,b.stderr),next((i,a.stdout[i*16:(i+1)*16].hex(),b.stdout[i*16:(i+1)*16].hex()) for i in range(cases) if a.stdout[i*16:(i+1)*16]!=b.stdout[i*16:(i+1)*16])
    print(f'Vector copy and shift: {cases} snapshots matched QEMU')
if __name__=='__main__': main()
