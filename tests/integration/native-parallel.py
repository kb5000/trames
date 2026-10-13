#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real musl pthread native memory, parsing, nested callbacks and shared stacks."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
RUNNER=os.getenv('TRAMES_RUNNER',str(ROOT/'target/release/trames-run'))
LIBC=ROOT/'target/musl-hybrid/libc.so'
FIXTURE=ROOT/'tests/fixtures/bin/native-parallel'
ARCHIVE=ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'
QEMU=os.getenv('QEMU_AARCH64','qemu-aarch64')
def run(args):
    result=subprocess.run(list(map(str,args)),capture_output=True,timeout=240)
    assert result.returncode==0,(args,result.returncode,result.stdout[-1000:],result.stderr[-4000:])
    return result
expected=run([QEMU,'-cpu','max','-L',ROOT/'target/alpine-build/rootfs',FIXTURE]).stdout
assert expected==b'native-parallel-ok 40312\n',expected
assert run([QEMU,'-cpu','max','-L',ROOT/'target/alpine-build/rootfs',FIXTURE,'shared-stack']).stdout==expected
rows=[]
with tempfile.TemporaryDirectory(prefix='trames-native-parallel-') as folder:
    for enabled,cpus,shared,memory in [(False,1,False,True),(False,4,False,True),(True,1,False,True),(True,2,False,True),(True,4,False,True),(True,4,True,True),(True,4,False,False)]:
        args=[RUNNER,*(['--memory'] if memory else ['--storage',str(Path(folder)/'store')]),'--rootfs',ARCHIVE,'--musl-libc',LIBC,'--cpus',str(cpus),'--accelerator','pure' if enabled else 'off','--native-bsearch','--metrics','--max-instructions','2000000000','--exec-file','/bin/native-parallel='+str(FIXTURE),'--guest','/bin/native-parallel',*(['shared-stack'] if shared else [])]
        result=run(args);assert result.stdout==expected,(result.stdout,expected)
        native,runtime=[json.loads(line.split(': ',1)[1]) for line in result.stderr.decode().splitlines()]
        assert native['faults']==0 and native['rejected']==0 and native['checkpoint_retries']==0,native
        if enabled:
            assert all(native['function_calls']) and not any(native['function_fallbacks']),native
            assert native['integer_conversions']>500 and native['numeric_conversions']>=32,native
            if not shared:assert native['callback_sorts']>=8 and native['callback_searches']>=4,native
            if cpus>1:
                assert native['worker_calls']>1000,native
                # Short calls may not overlap in every host schedule; the worker/page
                # guard unit tests enforce simultaneous peer progress deterministically.
                assert runtime['peak_parallel_native_calls']>=1,runtime
            else:assert native['worker_calls']==0 and runtime['peak_parallel_native_calls']==0,(native,runtime)
        else:assert native['calls']==0 and native['worker_calls']==0,native
        rows.append(dict(enabled=enabled,cpus=cpus,shared_stack=shared,memory=memory,native=native,runtime=runtime))
        print(f'PASS native pthread cpus={cpus} enabled={enabled} shared_stack={shared} memory={memory}: all 19 entries, FP/errno, parsing and nested callbacks; worker_calls={native["worker_calls"]} native_overlap={runtime["peak_parallel_native_calls"]}',flush=True)
report={'fixture_sha256':hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),'libc_sha256':hashlib.sha256(LIBC.read_bytes()).hexdigest(),'expected_stdout':expected.decode(),'rows':rows}
(ROOT/'target/musl-hybrid/native-parallel-validation.json').write_text(json.dumps(report,indent=2)+'\n')
