#!/usr/bin/env python3
"""Exercise the real C ABI, Alpine PTY, disk copies, pause and cancellation."""
import ctypes
import json
import os
from pathlib import Path
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
LIB = ROOT / 'target/release' / ('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so')
api = ctypes.CDLL(str(LIB))
api.trames_session_open.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
api.trames_session_open.restype = ctypes.c_uint64
for name in ('trames_environment_install',):
    getattr(api, name).argtypes = [ctypes.c_char_p, ctypes.c_uint32]
    getattr(api, name).restype = ctypes.c_int32
for name in ('trames_session_input', 'trames_session_transfer'):
    getattr(api,name).argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint32]
    getattr(api,name).restype = ctypes.c_int32
for name in ('trames_session_output', 'trames_session_status'):
    getattr(api,name).argtypes = [ctypes.c_uint64, ctypes.c_void_p, ctypes.c_uint32]
    getattr(api,name).restype = ctypes.c_int32
api.trames_session_resize.argtypes = [ctypes.c_uint64,ctypes.c_uint16,ctypes.c_uint16]
api.trames_session_pause.argtypes = [ctypes.c_uint64,ctypes.c_uint32]
api.trames_session_close.argtypes = [ctypes.c_uint64]
api.trames_last_error.argtypes = [ctypes.c_void_p,ctypes.c_uint32]

def error():
    b=ctypes.create_string_buffer(8192)
    n=api.trames_last_error(b,len(b))
    return b.raw[:max(0,n)].decode()
def request(fn, *prefix, payload):
    data=json.dumps(payload).encode()
    return fn(*prefix,data,len(data))
def status(handle):
    b=ctypes.create_string_buffer(8192)
    n=api.trames_session_status(handle,b,len(b))
    assert n>0,error()
    return json.loads(b.raw[:n])
def wait(predicate, seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if predicate(): return
        time.sleep(.01)
    raise AssertionError(('embedding event timeout', status(handle), bytes(output[-3000:])))
def send(handle, data):
    n=api.trames_session_input(handle,data,len(data))
    assert n==len(data), (n,error())
def drain(handle):
    b=ctypes.create_string_buffer(65536)
    n=api.trames_session_output(handle,b,len(b))
    assert n>=0,error()
    return b.raw[:n]

archive=Path(os.environ.get('TRAMES_ALPINE_ARCHIVE', ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz'))
assert archive.is_file(), 'run scripts/prepare-alpine.sh first'
with tempfile.TemporaryDirectory(prefix='trames-embedding-') as directory:
    temporary=Path(directory)
    store=temporary/'staging'
    options={'store':str(store),'rootfs':str(archive),'volume_total':64<<30,'volume_available':32<<30,'network':'off'}
    assert request(api.trames_environment_install,payload=options)==0,error()
    active=temporary/'environment'
    store.rename(active) # All Rust file owners/locks must already be released.
    options['store']=str(active); options.pop('rootfs')
    handle=request(api.trames_session_open,payload=options)
    assert handle,error()
    wait(lambda:status(handle)['phase']!='starting')
    assert status(handle)['phase']=='running',status(handle)
    assert status(handle)['budget_bytes']==16<<30
    output=bytearray()
    send(handle,b"printf '\\033[31mABI_READY\\033[0m\\n'\n")
    wait(lambda:(output.extend(drain(handle)) or b'\x1b[31mABI_READY\x1b[0m' in output))
    assert api.trames_session_resize(handle,37,103)==0,error()
    send(handle,b"stty size\n")
    wait(lambda:(output.extend(drain(handle)) or b'37 103' in output))
    source=temporary/'input.bin'; data=bytes(range(256))*4096; source.write_bytes(data)
    assert request(api.trames_session_transfer,handle,payload={'host':str(source),'guest':'/home/user/copied.bin','export':False})==0,error()
    target=temporary/'output.bin'
    assert request(api.trames_session_transfer,handle,payload={'host':str(target),'guest':'/home/user/copied.bin','export':True})==0,error()
    assert target.read_bytes()==data
    assert request(api.trames_session_transfer,handle,payload={'host':str(source),'guest':'/home/user/copied.bin','export':True})==-1
    assert source.read_bytes()==data # Never truncate an existing host file.
    assert api.trames_session_pause(handle,1)==0
    time.sleep(.05); drain(handle)
    send(handle,b"echo RESUMED\n")
    time.sleep(.05)
    assert b'RESUMED' not in drain(handle)
    assert api.trames_session_pause(handle,0)==0
    wait(lambda:(output.extend(drain(handle)) or b'RESUMED' in output))
    send(handle,b"while :; do :; done\n")
    assert api.trames_session_close(handle)==0
    assert api.trames_session_close(handle)==-1
    # Cancellation must release the persistent store even for a busy guest.
    replacement=0
    def reopen():
        global replacement
        replacement=request(api.trames_session_open,payload=options)
        if not replacement: return False
        wait(lambda:status(replacement)['phase']!='starting')
        if status(replacement)['phase']=='running': return True
        api.trames_session_close(replacement); return False
    wait(reopen)
    send(replacement,b"exit 7\n")
    wait(lambda:status(replacement)['phase']=='exited')
    assert status(replacement)['exit_code']==7
    api.trames_session_close(replacement)
    # A bad rootfs must never be accepted/promoted.
    bad=temporary/'bad.tar.gz'; bad.write_bytes(b'bad rootfs')
    options.update(store=str(temporary/'rejected'),rootfs=str(bad))
    assert request(api.trames_environment_install,payload=options)==-1
print('C ABI: Alpine PTY, color bytes, resize, 1 MiB disk roundtrip, exclusive export, pause/resume, busy cancellation, reopen and rootfs rejection passed')
