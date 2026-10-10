#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real static musl/BusyBox processes and libm, compared to independent QEMU.
Reference external applets use host exec wrappers that re-enter QEMU, so binfmt
registration is unnecessary. Trames exec stays entirely in the virtual kernel.
"""
import os,shlex,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def run(cmd,cwd,data=b'',env=None):
    r=subprocess.run(cmd,cwd=cwd,input=data,capture_output=True,env=env,timeout=45)
    return r.returncode,r.stdout,r.stderr

def main():
    runner=str(ROOT/'target/debug/trames-run');busybox=str(ROOT/'target/busybox-build/busybox')
    qemu=shutil.which(os.getenv('QEMU_AARCH64','qemu-aarch64'))
    if not qemu:raise RuntimeError('QEMU unavailable')
    comparisons=0
    cases=[
        ('builtins','for v in 1 2 3; do echo $((v*7)); done',b'',0),
        ('floating printf',"printf '%.2f %e %g\\n' 1.25 -0.001 1e20",b'',0),
        ('pipeline',"printf 'one\\ntwo\\n' | cat | wc -l",b'',0),
        ('redirection',"echo first > data/out; echo second >> data/out; cat data/out",b'',0),
        ('descriptor redirection',"sh -c 'echo error >&2' 2>&1",b'',0),
        ('substitution',"v=$(echo words | cat); echo got:$v",b'',0),
        ('subshell cwd',"mkdir sub; (cd sub; pwd); pwd",b'',0),
        ('background wait',"false & p=$!; wait $p; echo status:$?",b'',0),
        ('exec replacement',"exec echo replaced",b'',0),
        ('nested shell',"sh -c 'echo nested; exit 7'; echo status:$?",b'',0),
        ('explicit exit',"exit 23",b'',23),
        ('signal trap',"trap 'echo caught' USR1; kill -USR1 $$; echo after",b'',0),
        ('read stdin',"read -r v; printf 'got:%s\\n' \"$v\"",b'hello input\n',0),
        ('globbing',"echo data/*.txt",b'',0),
        ('file lifecycle',"mkdir fresh; echo payload >fresh/a; cat fresh/a; rm fresh/a; ls fresh",b'',0),
        ('sequence arithmetic',"seq 0.1 0.1 0.3",b'',0),
        ('pipeline exit',"seq 1 200 | cat | wc -l; echo status:$?",b'',0),
        ('shebang chain',"data/script argument",b'',0),
        ('binary file',"cat data/input.bin | wc -c",b'',0),
        ('shell status',"false || echo fallback; true && echo success; ! false; echo status:$?",b'',0),
        ('heredoc',"cat <<EOF\nfirst\nsecond\nEOF\n",b'',0),
        ('timed background',"sleep 0.01 & p=$!; echo before; wait $p; echo after",b'',0),
    ]
    with tempfile.TemporaryDirectory(prefix='trames-m2-') as directory:
        folder=Path(directory);bin_=folder/'bin';bin_.mkdir()
        for name in ['sh','ash','echo','printf','true','false','cat','wc','head','tail','pwd','ls','basename','dirname','sleep','kill','seq','env','test','mkdir','rm','touch']:
            wrapper=bin_/name;wrapper.write_text('#!/bin/sh\nexec '+shlex.join([qemu,'-cpu','cortex-a53',busybox,name])+' "$@"\n');wrapper.chmod(0o755)
        env={'PATH':str(bin_),'LANG':'C'}
        for name,script,input_,code in cases:
            refdir=folder/'reference';refdir.mkdir();(refdir/'data').mkdir()
            imports=[]
            fixtures={'a.txt':b'a\n','b.txt':b'b\n','input.bin':b'\0\xff\n'*200,'script':b'#!/usr/bin/env sh\nprintf "script:%s\\n" "$1"\n'}
            for file,content in fixtures.items():
                path=refdir/'data'/file;path.write_bytes(content);path.chmod(0o755 if file=='script' else 0o644)
                imports += ['--exec-file' if file=='script' else '--file',f'/data/{file}={path}']
            actual=run([runner,'--install-busybox',*imports,busybox,'ash','-c',script],refdir,input_)
            reference=run([qemu,'-cpu','cortex-a53',busybox,'ash','-c',script],refdir,input_,env)
            # Normalize exactly the reference cwd, not arbitrary diagnostics or bytes.
            if name=='subshell cwd': reference=(reference[0],reference[1].replace(str(refdir).encode()+b'/sub\n',b'/sub\n').replace(str(refdir).encode()+b'\n',b'/\n'),reference[2])
            if reference[0]!=code or actual!=reference:raise AssertionError(f'{name}: Trames {actual!r} != QEMU {reference!r}, expected exit {code}')
            comparisons+=1;shutil.rmtree(refdir)
        for name in ['m2-libm','m2-process']:
            exe=str(ROOT/'tests/fixtures/bin'/name);refdir=folder/'reference';refdir.mkdir()
            if name=='m2-process':
                wrapper=bin_/'probe';wrapper.write_text('#!/bin/sh\nexec '+shlex.join([qemu,'-cpu','cortex-a53',exe])+' "$@"\n');wrapper.chmod(0o755)
                actual=run([runner,'--exec-file',f'/data/probe={exe}',exe,'/data/probe'],refdir)
                reference=run([qemu,'-cpu','cortex-a53',exe,str(wrapper)],refdir)
            else:actual=run([runner,exe],refdir);reference=run([qemu,'-cpu','cortex-a53',exe],refdir)
            if actual!=reference or actual[0]!=0 or not actual[1]:raise AssertionError(f'{name}: Trames {actual!r} != QEMU {reference!r}')
            comparisons+=1;shutil.rmtree(refdir)
    print(f'M2 integration passed: {comparisons} QEMU comparisons (22 shell cases, process ABI and musl libm)')
if __name__=='__main__':main()
