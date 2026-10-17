# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,ctypes as C,json,pathlib,time,os,tempfile
R=pathlib.Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description='Original signed ARM64 foot over the production Wayland/session ABI')
parser.add_argument('--prepare',action='store_true');args=parser.parse_args()
store=pathlib.Path(os.environ['TRAMES_FOOT_STORE']).resolve()
assert args.prepare != store.exists(), 'prepare needs an absent store; omit --prepare to reuse it'
a=C.CDLL(os.environ.get('TRAMES_FFI_LIB',str(R/'target/release/libtrames_ffi.so')))
a.trames_session_open.argtypes=[C.c_char_p,C.c_uint32];a.trames_session_open.restype=C.c_uint64
for name in ['trames_session_input','trames_session_transfer','trames_session_wayland_command']:getattr(a,name).argtypes=[C.c_uint64,C.c_char_p,C.c_uint32]
for name in ['trames_session_output','trames_session_status','trames_session_wayland_status']:getattr(a,name).argtypes=[C.c_uint64,C.c_void_p,C.c_uint32]
a.trames_session_close.argtypes=[C.c_uint64]
a.trames_session_wayland_frame_acquire.argtypes=[C.c_uint64,C.c_uint64];a.trames_session_wayland_frame_acquire.restype=C.c_uint64
a.trames_wayland_frame_release.argtypes=[C.c_uint64,C.c_uint32,C.c_uint32]
repository=pathlib.Path(os.environ['TRAMES_FOOT_REPOSITORY']).resolve()
opts=dict(store=str(store),volume_total=16<<30,volume_available=16<<30,network='off',wayland=True)
if args.prepare:opts['rootfs']=str(pathlib.Path(os.environ['TRAMES_FOOT_ROOTFS']).resolve())
b=json.dumps(opts).encode();h=a.trames_session_open(b,len(b));assert h
output=''
def read(name):
 b=C.create_string_buffer(65536);n=getattr(a,name)(h,b,len(b));assert n>=0,(name,n);return b.raw[:n]
def payload(name,value):
 b=json.dumps(value).encode();n=getattr(a,name)(h,b,len(b));assert n>=0,(name,n)
def send(text):
 b=text.encode();assert a.trames_session_input(h,b,len(b))==len(b)
def wait(marker,seconds):
 global output
 began=time.monotonic()
 while marker not in output:
  output+=read('trames_session_output').decode(errors='replace')
  assert time.monotonic()-began<seconds,output[-6000:]
  time.sleep(.005)
try:
 began=time.monotonic()
 while json.loads(read('trames_session_status'))['phase']=='starting':
  assert time.monotonic()-began<90;time.sleep(.01)
 status=json.loads(read('trames_session_status'));assert status['phase']=='running',status
 if args.prepare:
  for p in sorted(repository.rglob('*')):
   if p.is_file():payload('trames_session_transfer',dict(guest='/tmp/foot-repo/'+p.relative_to(repository).as_posix(),host=str(p),export=False))
  send("printf '/tmp/foot-repo/main\\n/tmp/foot-repo/community\\n' > /tmp/foot-repositories; apk --repositories-file /tmp/foot-repositories --no-progress add foot font-dejavu; printf '\\nFOOT_SETUP:%d\\n' $?\n")
  wait('\r\nFOOT_SETUP:0\r\n',300);print('Original signed ARM64 foot installed',flush=True)
 send("rm -f /tmp/.foot-acceptance-complete; XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 foot -e /bin/sh -c 'echo FOOT_READY; while [ ! -e /tmp/.foot-acceptance-complete ]; do sleep 0.05; done'; printf '\\nFOOT_RESULT:%d\\n' $?\n")
 began=time.monotonic();frames=0;acknowledged=False
 while '\r\nFOOT_RESULT:0\r\n' not in output:
  output+=read('trames_session_output').decode(errors='replace')
  scene=json.loads(read('trames_session_wayland_status'))
  for w in scene['windows']:
   frame=a.trames_session_wayland_frame_acquire(h,w['id'])
   if frame:
    assert a.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)==0;frames+=1
    if not acknowledged:
     # End the child only after the compositor actually acquired a frame.
     # A fixed sleep can expire during font setup on a busy CI host.
     with tempfile.NamedTemporaryFile() as marker:
      marker.write(b'1');marker.flush()
      payload('trames_session_transfer',dict(guest='/tmp/.foot-acceptance-complete',host=marker.name,export=False))
     acknowledged=True
  if 'FOOT_RESULT:' in output and any('\r\nFOOT_RESULT:'+str(n)+'\r\n' in output for n in [1,134,139]):raise AssertionError(output[-7000:])
  assert time.monotonic()-began<180,output[-7000:]
  time.sleep(.005)
 assert frames>0 and 'No space left on device' not in output,output[-7000:]
 print(output[-7000:],flush=True);print('PASS: original ARM64 foot displayed and closed; frames',frames,flush=True)
finally:
 if os.environ.get('TRAMES_FOOT_LOG'):pathlib.Path(os.environ['TRAMES_FOOT_LOG']).write_text(output)
 a.trames_session_close(h)
