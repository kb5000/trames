#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless production embedding ABI with original guest clients and retained frames.

Prepare a signed-package store as described in docs/architecture/wayland.md and
set TRAMES_WAYLAND_STORE. This checks protocol/FFI lifetime, not Apple pixels.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import sys
import time
import uuid

ROOT=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prepare',action='store_true',help='install original signed offline inputs into a new store')
args=parser.parse_args()
store=Path(os.environ['TRAMES_WAYLAND_STORE']).resolve()
if args.prepare:assert not store.exists(), 'prepare requires an absent test store'
api=C.CDLL(str(ROOT/'target/release'/('libtrames_ffi.dylib' if sys.platform=='darwin' else 'libtrames_ffi.so')))
api.trames_session_open.argtypes=[C.c_char_p,C.c_uint32];api.trames_session_open.restype=C.c_uint64
for name in ('trames_session_input','trames_session_transfer','trames_session_wayland_command'):
    getattr(api,name).argtypes=[C.c_uint64,C.c_char_p,C.c_uint32]
for name in ('trames_session_status','trames_session_output','trames_session_wayland_status'):
    getattr(api,name).argtypes=[C.c_uint64,C.c_void_p,C.c_uint32]
api.trames_session_close.argtypes=[C.c_uint64]
api.trames_session_wayland_frame_acquire.argtypes=[C.c_uint64,C.c_uint64];api.trames_session_wayland_frame_acquire.restype=C.c_uint64
api.trames_wayland_frame_release.argtypes=[C.c_uint64,C.c_uint32,C.c_uint32]
class Info(C.Structure):
    _fields_=[('width',C.c_uint32),('height',C.c_uint32),('images',C.c_uint32),('reserved',C.c_uint32),('revision',C.c_uint64)]
class Image(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('kind','address','length','offset')]+[(n,C.c_uint32) for n in ('width','height','stride','alpha')]+[(n,C.c_int32) for n in ('x','y','scale')]+[('transform',C.c_uint32)]
api.trames_wayland_frame_info.argtypes=[C.c_uint64,C.POINTER(Info)]
api.trames_wayland_frame_image.argtypes=[C.c_uint64,C.c_uint32,C.POINTER(Image)]
def payload(method,handle,value):
    data=json.dumps(value).encode();result=getattr(api,method)(handle,data,len(data));assert result>=0,(method,result)
def read(method,handle):
    data=C.create_string_buffer(65536);n=getattr(api,method)(handle,data,len(data));assert n>=0,(method,n);return data.raw[:n]
def command(value):payload('trames_session_wayland_command',session,value)
def send(value):
    data=value.encode();assert api.trames_session_input(session,data,len(data))==len(data)
def status():return json.loads(read('trames_session_wayland_status',session))
options={'store':str(store),'volume_total':16<<30,'volume_available':16<<30,'network':'off','wayland':True,
         'vulkan_accelerator':True,'vulkan_icd':str(ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so')}
if args.prepare:options['rootfs']=str(ROOT/'target/alpine-download/alpine-minirootfs-3.22.6-aarch64.tar.gz')
data=json.dumps(options).encode();session=api.trames_session_open(data,len(data));assert session
retained=[];text='';start=time.monotonic()
try:
    while json.loads(read('trames_session_status',session))['phase']=='starting':
        assert time.monotonic()-start<60;time.sleep(.01)
    inputs=ROOT/'target/vulkan-guest/wayland-acceptance'
    if args.prepare:
        repository=inputs/'repository'
        for path in sorted(repository.rglob('*')):
            if path.is_file():payload('trames_session_transfer',session,dict(guest='/tmp/wayland-repo/'+path.relative_to(repository).as_posix(),host=str(path),export=False))
        for guest,host in [('wayland-repositories','repositories'),('weston-assets.apk','weston-assets.apk.input'),('assets.tar','assets.tar')]:
            payload('trames_session_transfer',session,dict(guest='/tmp/'+guest,host=str(inputs/host),export=False))
        send("apk --repositories-file /tmp/wayland-repositories --no-progress add wayland-utils weston-clients vulkan-tools vulkan-loader font-dejavu breeze-cursors && apk verify /tmp/weston-assets.apk && tar -xf /tmp/assets.tar -C /; printf '\\nWAYLAND_SETUP:%d\\n' $?\n")
        began=time.monotonic()
        while '\r\nWAYLAND_SETUP:0\r\n' not in text:
            text+=read('trames_session_output',session).decode(errors='replace')
            assert time.monotonic()-began<300,text[-4096:]
            time.sleep(.01)
        print('Original guest apk resolved and signature-verified the offline Wayland packages',flush=True)
    peer='/tmp/wayland-ffi-'+uuid.uuid4().hex
    payload('trames_session_transfer',session,{'guest':peer,'host':str(ROOT/'target/vulkan-guest/wayland-acceptance/wayland-client'),'export':False})
    for stage,program in [('info','wayland-info'),('windows',peer+' 1 hold'),('shm','weston-simple-shm'),('editor','weston-editor'),('cube','vkcube --wsi wayland --c 30')]:
        before=time.monotonic();frames=0;focused=False;closed=False;resized=False;confirmed=False;edited=False;editor_step=0;first_context=None;last={};held=False
        send(f'chmod 755 {peer}; XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json {program}; printf "\\nWAYLAND_FFI:{stage}:%d\\n" $?\n')
        marker=f'\r\nWAYLAND_FFI:{stage}:0\r\n'
        while marker not in text:
            text+=read('trames_session_output',session).decode(errors='replace')
            scene=status()
            for index,window in enumerate(scene['windows']):
                wid=window['id']
                if not focused:
                    command({'op':'focus','window':wid});focused=True
                    if stage=='editor':
                        command(dict(op='pointer',window=wid,x=80,y=80,time=1))
                        for pressed in [True,False]:command(dict(op='button',button=0x110,pressed=pressed,time=2))
                frame=api.trames_session_wayland_frame_acquire(session,wid)
                if frame:
                    info=Info();image=Image();assert api.trames_wayland_frame_info(frame,C.byref(info))==0
                    if info.revision!=last.get(wid):frames+=1;last[wid]=info.revision
                    assert info.images>0
                    assert api.trames_wayland_frame_image(frame,0,C.byref(image))==0
                    if stage in ('windows','cube') and not held:
                        retained.append((frame,image.kind));held=True
                    else:assert api.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)==0
                if index==0:
                    if stage in ('windows','cube') and not resized:
                        command(dict(op='resize',window=wid,width=320,height=240));resized=True
                    confirmed |= window['width']==320 and window['height']==240
                    ready=stage=='windows' and confirmed and len(scene['windows'])==2 or stage=='shm' and frames>=4
                    if stage=='editor' and scene.get('text'):
                        context=scene['text']
                        values=dict(op='text',serial=context['serial'],context=context['id'],epoch=context['epoch'])
                        def click(y):
                            command(dict(op='pointer',window=wid,x=window['width']-64,y=y,time=4))
                            for pressed in [True,False]:command(dict(op='button',button=0x110,pressed=pressed,time=5))
                        if editor_step==0:
                            first_context=context['id'];command(dict(values,preedit='ni',cursor_begin=2,cursor_end=2))
                            command(dict(values,preedit='',commit='你好🙂'));edited=True;editor_step=1
                        elif editor_step==1 and '你好🙂' in context['text']:
                            click(window['height']*.75);editor_step=2
                        elif editor_step==2 and context['id']!=first_context:
                            command(dict(values,commit='第二框'));editor_step=3
                        elif editor_step==3 and '第二框' in context['text']:
                            click(80);editor_step=4
                        elif editor_step==4 and context['id']==first_context and '你好🙂' in context['text']:
                            command(dict(values,commit='返回'));editor_step=5
                        ready=editor_step==5 and '返回' in context['text']
                    if ready and not closed:command(dict(op='close',window=wid));closed=True
            assert time.monotonic()-before<90,(stage,text[-4096:],scene)
            time.sleep(.004)
        if stage=='cube':assert confirmed
        if stage=='editor':assert edited
        print(f'{stage}: original guest client and production ABI passed; frame revisions={frames}',flush=True)
        while status()['windows']:
            assert time.monotonic()-before<95;time.sleep(.005)
    assert len(retained)==2
    assert api.trames_session_close(session)==0;session=0
    for frame,kind in retained:
        info=Info();image=Image();assert api.trames_wayland_frame_info(frame,C.byref(info))==0
        assert api.trames_wayland_frame_image(frame,0,C.byref(image))==0;assert image.kind==kind
        if kind==1:assert C.string_at(image.address+image.offset,4)==bytes.fromhex('664422ff')
        assert api.trames_wayland_frame_release(frame,0,0)==0
    retained=[]
    print('SHM and GPU frame capabilities remain valid after VM session shutdown, then release cleanly',flush=True)
finally:
    if session:api.trames_session_close(session)
    for frame,_ in retained:api.trames_wayland_frame_release(frame,0,0)
