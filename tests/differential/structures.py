#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""AdvSIMD structure registers, memory and writeback, including register wrap."""
import os,subprocess,tempfile
from pathlib import Path
from run import ROOT

def program():
    code=['.global _start','.text','_start:','adrp x20,snapshot','add x20,x20,:lo12:snapshot'];count=0
    for size in [8,16]:
        for element,suffix in [(1,'b'),(2,'h'),(4,'s'),(8,'d')]:
            for structures,registers in [(1,1),(1,2),(1,3),(1,4),(2,2),(3,3),(4,4)]:
                if size==8 and element==8 and structures!=1:continue
                for post in ['none','immediate','register']:
                    count+=1;arr=f'{size//element}{suffix}';regs=', '.join(f'v{r}.{arr}' for r in [30,31,0,1][:registers]);length=size*registers
                    code += [f'eor v{r}.16b,v{r}.16b,v{r}.16b' for r in [30,31,0,1,7]]
                    code += ['adrp x4,input','add x4,x4,:lo12:input','add x4,x4,#1','mov x5,x4',f'mov x7,#{length+3}']
                    ending='' if post=='none' else f', #{length}' if post=='immediate' else ', x7'
                    code += [f'ld{structures} {{{regs}}},[x4]{ending}','sub x6,x4,x5','stp q30,q31,[x20]','stp q0,q1,[x20,#32]','str x6,[x20,#64]','adrp x4,buffer','add x4,x4,:lo12:buffer','stp q7,q7,[x4]','stp q7,q7,[x4,#32]','mov x5,x4',f'st{structures} {{{regs}}},[x4]{ending}','sub x6,x4,x5','str x6,[x20,#72]','ldp q6,q7,[x5]','stp q6,q7,[x20,#80]','ldp q6,q7,[x5,#32]','stp q6,q7,[x20,#112]','add x20,x20,#144']
    for element,suffix in [(1,'b'),(2,'h'),(4,'s'),(8,'d')]:
        for structures in range(1,5):
            for lane in range(16//element):
                for post in ['none','immediate','register']:
                    count+=1
                    regs=', '.join(f'v{r}.{suffix}' for r in [30,31,0,1][:structures]);length=element*structures
                    ending='' if post=='none' else f', #{length}' if post=='immediate' else ', x7'
                    code += [f'movi v{r}.16b,#165' for r in [30,31,0,1]]
                    code += ['adrp x4,input','add x4,x4,:lo12:input','add x4,x4,#1','mov x5,x4',f'mov x7,#{length+3}',f'ld{structures} {{{regs}}}[{lane}],[x4]{ending}','sub x6,x4,x5','stp q30,q31,[x20]','stp q0,q1,[x20,#32]','str x6,[x20,#64]','adrp x4,buffer','add x4,x4,:lo12:buffer','movi v7.16b,#0','stp q7,q7,[x4]','stp q7,q7,[x4,#32]','mov x5,x4',f'mov x7,#{length+3}',f'st{structures} {{{regs}}}[{lane}],[x4]{ending}','sub x6,x4,x5','str x6,[x20,#72]','ldp q6,q7,[x5]','stp q6,q7,[x20,#80]','ldp q6,q7,[x5,#32]','stp q6,q7,[x20,#112]','add x20,x20,#144']
            for width in [8,16]:
                for post in ['none','immediate','register']:
                    count+=1
                    regs=', '.join(f'v{r}.{width//element}{suffix}' for r in [30,31,0,1][:structures]);length=element*structures
                    ending='' if post=='none' else f', #{length}' if post=='immediate' else ', x7'
                    code += [f'movi v{r}.16b,#165' for r in [30,31,0,1]]
                    code += ['adrp x4,input','add x4,x4,:lo12:input','add x4,x4,#1','mov x5,x4',f'mov x7,#{length+3}',f'ld{structures}r {{{regs}}},[x4]{ending}','sub x6,x4,x5','stp q30,q31,[x20]','stp q0,q1,[x20,#32]','str x6,[x20,#64]','str x6,[x20,#72]','movi v7.16b,#0','stp q7,q7,[x20,#80]','stp q7,q7,[x20,#112]','add x20,x20,#144']
    code += ['mov x0,#1','adrp x1,snapshot','add x1,x1,:lo12:snapshot',f'ldr x2,={count*144}','mov x8,#64','svc #0','mov x0,#0','mov x8,#93','svc #0','.section .rodata','.balign 16','input: .byte '+','.join(str((i*73+21)&255) for i in range(128)),'.bss','.balign 16','buffer:.skip 64',f'snapshot:.skip {count*144}']
    return '\n'.join(code)+'\n',count

def main():
    source,count=program()
    with tempfile.TemporaryDirectory(prefix='trames-structures-') as folder:
        p=Path(folder);(p/'m.S').write_text(source)
        subprocess.run([os.getenv('AARCH64_AS','aarch64-linux-gnu-as'),'-o',str(p/'m.o'),str(p/'m.S')],check=True)
        subprocess.run([os.getenv('AARCH64_LD','aarch64-linux-gnu-ld'),'-static','--build-id=none','-z','noexecstack','-o',str(p/'m'),str(p/'m.o')],check=True)
        a=subprocess.run([str(ROOT/'target/debug/trames-run'),'--memory',str(p/'m')],capture_output=True,timeout=15)
        b=subprocess.run([os.getenv('QEMU_AARCH64','qemu-aarch64'),'-cpu','cortex-a53',str(p/'m')],capture_output=True,timeout=15)
        if a.returncode or b.returncode or len(a.stdout)!=count*144 or a.stdout!=b.stdout or a.stderr!=b.stderr:raise AssertionError((a,b))
    print(f'Structure memory differential passed: {count} load/store variants in 1 QEMU comparison')
if __name__=='__main__':main()
