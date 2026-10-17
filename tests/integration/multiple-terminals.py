#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One production Alpine guest, independent host PTYs and unchanged BusyBox shells."""
import ctypes as C
import json
import os
from pathlib import Path
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[2]
api=C.CDLL(os.environ.get('TRAMES_FFI_LIB',str(ROOT/'target/release'/('libtrames_ffi.dylib' if sys.platform=='darwin' else 'libtrames_ffi.so'))))
api.trames_session_open.argtypes=[C.c_char_p,C.c_uint32];api.trames_session_open.restype=C.c_uint64
api.trames_session_terminal_open.argtypes=[C.c_uint64];api.trames_session_terminal_open.restype=C.c_uint64
api.trames_session_terminal_close.argtypes=[C.c_uint64,C.c_uint64]
api.trames_session_terminal_resize.argtypes=[C.c_uint64,C.c_uint64,C.c_uint16,C.c_uint16]
api.trames_session_terminal_input.argtypes=[C.c_uint64,C.c_uint64,C.c_char_p,C.c_uint32]
for name in ['terminal_output','terminal_status']:
    getattr(api,'trames_session_'+name).argtypes=[C.c_uint64,C.c_uint64,C.c_void_p,C.c_uint32]
api.trames_session_status.argtypes=[C.c_uint64,C.c_void_p,C.c_uint32]
api.trames_session_pause.argtypes=[C.c_uint64,C.c_uint32]
api.trames_session_close.argtypes=[C.c_uint64]
api.trames_last_error.argtypes=[C.c_void_p,C.c_uint32]
def error():
    b=C.create_string_buffer(8192);n=api.trames_last_error(b,len(b));return b.raw[:max(0,n)].decode()
def receive(kind,*ids):
    b=C.create_string_buffer(65536);n=getattr(api,'trames_session_'+kind)(handle,*ids,b,len(b));assert n>=0,error();return b.raw[:n]
def status(t):return json.loads(receive('terminal_status',t))
def send(t,text):
    b=text.encode() if isinstance(text,str) else text
    n=api.trames_session_terminal_input(handle,t,b,len(b));assert n==len(b),(n,error())
def wait(f,seconds=20):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        if f():return
        time.sleep(.005)
    raise AssertionError(('timeout',json.loads(receive('status')),logs))
logs={}
def expect(t,needle):
    needle=needle.encode();logs[t]=b''
    wait(lambda: (logs.__setitem__(t,logs[t]+receive('terminal_output',t)) or needle in logs[t]))
def run(t,command,expected):send(t,command+'\n');expect(t,expected)
archive=Path(os.environ.get('TRAMES_ALPINE_ARCHIVE',ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'))
assert archive.is_file(),archive
with tempfile.TemporaryDirectory(prefix='trames-terminals-') as d:
    options=dict(store=str(Path(d)/'store'),rootfs=str(archive),volume_total=16<<30,volume_available=16<<30,network='off',multiple_terminals=True)
    if os.environ.get('TRAMES_TEST_MUSL'):options.update(musl_libc=os.environ['TRAMES_TEST_MUSL'],pure_accelerator=True)
    b=json.dumps(options).encode();handle=api.trames_session_open(b,len(b));assert handle,error()
    try:
        terminals=[1,api.trames_session_terminal_open(handle),api.trames_session_terminal_open(handle)]
        assert terminals==[1,2,3],terminals
        wait(lambda: all(status(t)['phase']=='running' for t in terminals))
        # Match an actual complete output frame, never the command's TTY echo.
        for t in terminals:run(t,f"stty -echo; printf '\\nREADY_{t}\\n'",f'\r\nREADY_{t}\r\n')
        run(1,"cd /tmp; export PRIVATE_VAR=one; printf shared-content >/tmp/shared; echo ONE_READY",'ONE_READY')
        run(2,"printf '\\nTWO:%s:%s:%s\\n' \"$(pwd)\" \"${PRIVATE_VAR-unset}\" \"$(cat /tmp/shared)\"",'TWO:/root:unset:shared-content')
        paths=[]
        for t in terminals:
            run(t,"printf '\\nTTY:'; tty",'TTY:/dev/pts/')
            paths.append(logs[t].split(b'TTY:',1)[1].split(b'\r\n')[0])
        assert len(set(paths))==3,paths
        for t,rows,cols in [(1,31,91),(2,43,123),(3,27,75)]:
            assert api.trames_session_terminal_resize(handle,t,rows,cols)==0,error()
            run(t,'stty size',f'{rows} {cols}\r\n')
        run(1,'sleep 300 & echo SHARED_PID:$!','SHARED_PID:')
        pid=int(logs[1].split(b'SHARED_PID:',1)[1].split(b'\r\n')[0])
        run(2,f'kill {pid}; echo SHARED_PROCESS_KILLED','SHARED_PROCESS_KILLED')
        run(1,'echo SLEEP_START; sleep 300','SLEEP_START\r\n')
        time.sleep(.1);send(1,b'\x03');expect(1,'/tmp # ')
        run(1,'echo CTRL_C_OK','CTRL_C_OK')
        send(3,'yes BLOCKED_OUTPUT\n');time.sleep(.2)
        run(2,'echo OTHER_TTY_RESPONSIVE','OTHER_TTY_RESPONSIVE')
        assert b'BLOCKED_OUTPUT' not in logs[2],logs[2]
        assert api.trames_session_terminal_close(handle,1)==0,error()
        run(2,'echo SURVIVED_FIRST_CLOSE','SURVIVED_FIRST_CLOSE')
        assert json.loads(receive('status'))['phase']=='running'
        assert api.trames_session_terminal_close(handle,3)==0,error()
        send(2,'exit\n');wait(lambda:status(2)['phase']=='exited')
        assert api.trames_session_terminal_close(handle,2)==0,error()
        # Close before init consumes queued opens; a fresh terminal must not
        # receive a second shell from an old request with a recycled PTY path.
        for _ in range(12):
            t=api.trames_session_terminal_open(handle);assert t;assert api.trames_session_terminal_close(handle,t)==0
        last=api.trames_session_terminal_open(handle);assert last>3
        wait(lambda:status(last)['phase']=='running')
        run(last,"stty -echo; echo LAST_TTY_ALIVE",'LAST_TTY_ALIVE')
        run(last,"cat /tmp/shared; echo REOPENED_SAME_GUEST",'shared-contentREOPENED_SAME_GUEST')
        assert api.trames_session_terminal_resize(handle,last,0,80)==-1
        assert api.trames_session_terminal_resize(handle,99999,24,80)==-1
        assert api.trames_session_terminal_close(handle,99999)==-1
        print('PASS: 3 independent PTYs, shared files/processes, private cwd/env, resize, Ctrl+C, output backpressure, close/exit/reopen and rapid pending cancellation',flush=True)
    finally:api.trames_session_close(handle)
