#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original signed vkmark scenes through production Wayland/Vulkan FFI (not a speed benchmark)."""
import ctypes as C
import json,os,sys,time,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
store=Path(os.environ.get('TRAMES_WAYLAND_STORE',str(ROOT/'target/wayland/store'))).resolve()
api=C.CDLL(str(ROOT/'target/release'/('libtrames_ffi.dylib' if sys.platform=='darwin' else 'libtrames_ffi.so')))
api.trames_session_open.argtypes=[C.c_char_p,C.c_uint32];api.trames_session_open.restype=C.c_uint64
for name in ('trames_session_input','trames_session_transfer','trames_session_wayland_command'):
    getattr(api,name).argtypes=[C.c_uint64,C.c_char_p,C.c_uint32]
for name in ('trames_session_status','trames_session_output','trames_session_wayland_status'):
    getattr(api,name).argtypes=[C.c_uint64,C.c_void_p,C.c_uint32]
api.trames_session_close.argtypes=[C.c_uint64]
api.trames_last_error.argtypes=[C.c_void_p,C.c_uint32]
api.trames_session_wayland_frame_acquire.argtypes=[C.c_uint64,C.c_uint64];api.trames_session_wayland_frame_acquire.restype=C.c_uint64
api.trames_wayland_frame_release.argtypes=[C.c_uint64,C.c_uint32,C.c_uint32]
class Info(C.Structure):
    _fields_=[('width',C.c_uint32),('height',C.c_uint32),('images',C.c_uint32),('reserved',C.c_uint32),('revision',C.c_uint64)]
class Image(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('kind','address','length','offset')]+[(n,C.c_uint32) for n in ('width','height','stride','alpha')]+[(n,C.c_int32) for n in ('x','y','scale')]+[('transform',C.c_uint32)]
api.trames_wayland_frame_info.argtypes=[C.c_uint64,C.POINTER(Info)]
api.trames_wayland_frame_image.argtypes=[C.c_uint64,C.c_uint32,C.POINTER(Image)]
def payload(method,handle,value):
    data=json.dumps(value).encode();result=getattr(api,method)(handle,data,len(data))
    if result<0:
        error=C.create_string_buffer(4096);api.trames_last_error(error,len(error))
        raise AssertionError((method,value,error.value.decode(errors='replace')))
def read(method,handle):
    data=C.create_string_buffer(65536);n=getattr(api,method)(handle,data,len(data));assert n>=0,(method,n);return data.raw[:n]
def command(value):payload('trames_session_wayland_command',session,value)
def send(value):
    data=value.encode();assert api.trames_session_input(session,data,len(data))==len(data)
def status():return json.loads(read('trames_session_wayland_status',session))
options={'store':str(store),'volume_total':16<<30,'volume_available':16<<30,'network':'off','wayland':True,
         'vulkan_accelerator':True,'vulkan_icd':str(ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so')}
data=json.dumps(options).encode();session=api.trames_session_open(data,len(data));assert session
text='';start=time.monotonic()
try:
    while json.loads(read('trames_session_status',session))['phase']=='starting':
        assert time.monotonic()-start<60;time.sleep(.01)
    state=json.loads(read('trames_session_status',session));assert state['phase']=='running',state
    repository=ROOT/'target/wayland/vkmark-repository'
    guest='/tmp/vkmark-'+uuid.uuid4().hex
    for path in sorted(repository.rglob('*')):
        if path.is_file():payload('trames_session_transfer',session,dict(guest=guest+'/repo/'+path.relative_to(repository).as_posix(),host=str(path),export=False))
    config=repository.parent/'vkmark-repositories';config.write_text(guest+'/repo/main\n'+guest+'/repo/community\n')
    payload('trames_session_transfer',session,dict(guest=guest+'/repositories',host=str(config),export=False))
    send("apk --repositories-file "+guest+"/repositories --no-progress add vkmark; printf '\\nVKMARK_SETUP:%d\\n' $?\n")
    while '\r\nVKMARK_SETUP:0\r\n' not in text:
        text+=read('trames_session_output',session).decode(errors='replace')
        assert time.monotonic()-start<240,text[-4096:];time.sleep(.005)
    scenes=['vertex:device-local=true','vertex:device-local=false','texture:anisotropy=0','texture:anisotropy=16',
        'shading:shading=gouraud','shading:shading=blinn-phong-inf','shading:shading=phong','shading:shading=cel',
        'effect2d:kernel=edge','effect2d:kernel=blur','desktop','cube','clear']
    if os.environ.get('TRAMES_VKMARK_SCENES'):scenes=os.environ['TRAMES_VKMARK_SCENES'].split(',')
    arguments=' '.join('-b '+value+':duration=0.05' for value in scenes)
    send('XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json vkmark --winsys wayland --size 320x240 '+arguments+"; printf '\\nVKMARK_RESULT:%d\\n' $?\n")
    began=time.monotonic();frames=0
    while '\r\nVKMARK_RESULT:0\r\n' not in text:
        text+=read('trames_session_output',session).decode(errors='replace')
        if any('\r\nVKMARK_RESULT:'+str(n)+'\r\n' in text for n in [1,134,139]):
            send('dmesg\n');time.sleep(.1);text+=read('trames_session_output',session).decode(errors='replace');raise AssertionError(text[-12000:])
        for window in status()['windows']:
            frame=api.trames_session_wayland_frame_acquire(session,window['id'])
            if frame:
                info=Info();assert api.trames_wayland_frame_info(frame,C.byref(info))==0
                assert api.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)==0;frames+=1
        assert time.monotonic()-began<240,text[-12000:]
        time.sleep(.003)
    assert frames>0 and 'vkmark Score' in text,text[-12000:]
    print(text,flush=True);print('Original vkmark passed',len(scenes),'scene variants; native frame acquisitions:',frames,flush=True)
finally:
    api.trames_session_close(session)
