#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""All widening sizes, signedness, halves, boundary shifts and destination aliasing."""
import os, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def main():
    source=['.text','.global _start','_start:','adr x9, input','adr x10, output']; cases=0
    for op in ['sshll','ushll']:
        for bits,letter,outletter in [(8,'b','h'),(16,'h','s'),(32,'s','d')]:
            for high in [False,True]:
                for shift in [0,1,bits-1]:
                    for dst in [0,2]:
                        source+=['ldr q0, [x9]',f'{op}{"2" if high else ""} v{dst}.{64//bits}{outletter}, v0.{(128 if high else 64)//bits}{letter}, #{shift}',f'str q{dst}, [x10], #16']; cases+=1
    source+=['adr x1, output',f'mov x2, #{cases*16}','mov x0, #1','mov x8, #64','svc #0','mov x0, #0','mov x8, #93','svc #0','.data','.balign 16','input:','.byte 0,255,128,127,1,254,129,126,254,1,255,0,127,128,126,129','.bss','.balign 16',f'output: .skip {cases*16}']
    with tempfile.TemporaryDirectory(prefix='trames-widening-') as folder:
        p=Path(folder);(p/'case.S').write_text('\n'.join(source)+'\n')
        subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-o',str(p/'case.o'),str(p/'case.S')],check=True)
        subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-o',str(p/'case'),str(p/'case.o')],check=True)
        a=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(p/'case')],capture_output=True,timeout=15)
        b=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','cortex-a53',str(p/'case')],capture_output=True,timeout=15)
        assert b.returncode==0 and len(b.stdout)==cases*16
        assert (a.returncode,a.stdout,a.stderr)==(b.returncode,b.stdout,b.stderr),(a.returncode,a.stderr,b.stderr,a.stdout.hex(),b.stdout.hex())
    print(f'Widening integer SIMD passed: {cases} snapshots in 1 QEMU comparison')
if __name__=='__main__':main()
