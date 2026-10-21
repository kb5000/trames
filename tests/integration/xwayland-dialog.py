#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original mousepad X11 unsaved-close dialogs through production GPU WSI.

Only edit/close/modal/cancel is exercised. Apple also checks actual exported
Metal textures for nonblank content; this is not an exact UI pixel oracle.
Use a disposable signed-package store. No Firefox or personal data is used.
"""
import argparse
import ctypes as C
import json
import os
import sys
import shlex
from pathlib import Path
import time

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--store',type=Path,required=True)
p.add_argument('--ffi',type=Path,default=ROOT/'target/perf-runtime/release/libtrames_ffi.so')
p.add_argument('--output',type=Path,required=True)
p.add_argument('--timeout',type=float,default=150)
p.add_argument('--layout-client',type=Path,help='also check only the four new attachment-layout GPU pixel cases')
p.add_argument('--wayland-debug',action='store_true')
p.add_argument('--native-wsi',action='store_true',help='test bundled original glamor with DRM-less EGL transport')
p.add_argument('--metal-validator',type=Path,help='test-only zink-metal-pixels library, required on Apple GPU runs')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
if hasattr(os,'sched_setaffinity'):os.sched_setaffinity(0,sorted(os.sched_getaffinity(0))[:4])
api=C.CDLL(str(a.ffi.resolve()))
api.trames_session_open.argtypes=[C.c_char_p,C.c_uint32];api.trames_session_open.restype=C.c_uint64
for name in ('input','transfer','wayland_command'):getattr(api,'trames_session_'+name).argtypes=[C.c_uint64,C.c_char_p,C.c_uint32]
for name in ('status','output','wayland_status'):getattr(api,'trames_session_'+name).argtypes=[C.c_uint64,C.c_void_p,C.c_uint32]
api.trames_session_close.argtypes=[C.c_uint64]
api.trames_session_wayland_frame_acquire.argtypes=[C.c_uint64,C.c_uint64];api.trames_session_wayland_frame_acquire.restype=C.c_uint64
api.trames_wayland_frame_release.argtypes=[C.c_uint64,C.c_uint32,C.c_uint32]
class Info(C.Structure):
    _fields_=[('width',C.c_uint32),('height',C.c_uint32),('images',C.c_uint32),('reserved',C.c_uint32),('revision',C.c_uint64)]
class Image(C.Structure):
    _fields_=[(n,C.c_uint64) for n in ('kind','address','length','offset')]+[(n,C.c_uint32) for n in ('width','height','stride','alpha')]+[(n,C.c_int32) for n in ('x','y','scale')]+[('transform',C.c_uint32)]
api.trames_wayland_frame_info.argtypes=[C.c_uint64,C.POINTER(Info)]
api.trames_wayland_frame_image.argtypes=[C.c_uint64,C.c_uint32,C.POINTER(Image)]
options=dict(store=str(a.store.resolve()),volume_total=16<<30,volume_available=16<<30,network='off',wayland=True,
    xwayland_launcher=str(ROOT/'target/xwayland/trames-xwayland'),
    pure_accelerator=True,musl_libc=str(ROOT/'target/musl-hybrid/libc.so'),memory_budget_bytes=2<<30,process_memory_budget_bytes=2<<30)
metal=None
if a.native_wsi:
    options.update(vulkan_accelerator=True,vulkan_icd=str(ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so'),
                   xwayland_launcher=str(ROOT/'target/vulkan-guest/trames-xwayland'),
                   memory_budget_bytes=4<<30,process_memory_budget_bytes=4<<30)
    if sys.platform=='darwin':
        assert a.metal_validator,'Apple GPU acceptance requires exported Metal texture pixel verification'
        metal=C.CDLL(str(a.metal_validator.resolve()))
        metal.trames_zink_nonblank_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32]
data=json.dumps(options).encode();h=api.trames_session_open(data,len(data));assert h
output=bytearray();queries=0;began=time.monotonic();report={'passed':False}
def read(name):
    b=C.create_string_buffer(65536);n=getattr(api,'trames_session_'+name)(h,b,len(b));assert n>=0,(name,n);return b.raw[:n]
def send(s):
    b=s.encode();assert api.trames_session_input(h,b,len(b))==len(b)
def payload(name,value):
    b=json.dumps(value).encode();assert getattr(api,'trames_session_'+name)(h,b,len(b))>=0
def command(**value):payload('wayland_command',value)
def pump():
    global queries
    output.extend(read('output'))
    wanted=output.count(b'\x1b[6n')
    while queries<wanted:send('\x1b[1;5R');queries+=1
    s=json.loads(read('status'));assert s['phase'] in ('starting','running'),s
    scene=json.loads(read('wayland_status'))
    for w in scene['windows']:
        frame=api.trames_session_wayland_frame_acquire(h,w['id'])
        if frame:api.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)
    (a.output/'terminal.log').write_bytes(output)
    return scene
def shell(command):
    # ash reads a terminal cursor-position reply before accepting the next
    # line. Drain that exchange before submitting another command, otherwise
    # its prefix can be consumed as part of the reply on a fast host.
    previous=len(output);quiet=time.monotonic()
    while time.monotonic()-quiet<.25:
        pump()
        if len(output)!=previous:previous=len(output);quiet=time.monotonic()
        time.sleep(.005)
    send(command)
def wait(predicate,timeout=None):
    start=time.monotonic()
    while True:
        scene=pump()
        if predicate(scene):return scene
        if time.monotonic()-start>5 and not scene['windows']:
            export('/tmp/xwayland.log','xwayland.log')
            log=a.output/'xwayland.log'
            if log.exists():
                text=log.read_text(errors='replace')
                assert not any(error in text for error in ('Fatal server error','ZINK: vkQueueSubmit failed','vkCreateGraphicsPipelines failed')),text[-3000:]
        assert time.monotonic()-start<(timeout or a.timeout),(scene,output[-4000:])
        time.sleep(.005)
def export(guest,name):
    (a.output/name).unlink(missing_ok=True)
    b=json.dumps(dict(guest=guest,host=str((a.output/name).resolve()),export=True,maximum_bytes=8<<20)).encode()
    return api.trames_session_transfer(h,b,len(b))
def key(code):
    for down in (True,False):command(op='key',key=code,pressed=down,modifiers=0,time=1)
def gpu(window):
    frame=api.trames_session_wayland_frame_acquire(h,window)
    if not frame:return False
    try:
        info=Info();image=Image()
        assert api.trames_wayland_frame_info(frame,C.byref(info))==0
        assert api.trames_wayland_frame_image(frame,0,C.byref(image))==0
        assert image.kind==(2 if a.native_wsi else 1),'unexpected rendering path'
        ready=image.width>1 and image.height>1
        if ready and metal:ready=metal.trames_zink_nonblank_texture(image.address,image.width,image.height)==0
        return ready
    finally:api.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)
try:
    wait(lambda _:json.loads(read('status'))['phase']=='running',60)
    if a.layout_client:
        for name in ('vulkan-layouts', 'vulkan-render.vert.spv', 'vulkan-render.frag.spv'):
            payload('transfer',dict(guest='/tmp/'+name,host=str((a.layout_client.parent/name).resolve()),export=False,replace=True))
        modes=('general','general2','legacy-feedback','legacy-feedback2')
        for mode in modes:
            start=len(output)
            shell('chmod 755 /tmp/vulkan-layouts; /tmp/vulkan-layouts /tmp/vulkan-render.vert.spv /tmp/vulkan-render.frag.spv /tmp/layout.ppm '+mode+'; printf "\\nLAYOUT_EXIT:%d\\n" $?\n')
            def finished(_):
                new=output[start:]
                assert b'Assertion failed:' not in new,new[-4000:]
                assert not any(('\r\nLAYOUT_EXIT:'+str(n)+'\r\n').encode() in new for n in (1,127,134,139)),new[-4000:]
                return b'\r\nLAYOUT_EXIT:0\r\n' in new
            wait(finished,90)
            assert ('vulkan-general-layout-ok: '+mode).encode() in output[start:]
        report['layout_pixel_modes']=modes
        report['layout_readback_pixels']=len(modes)*3*256*192
    debug='LIBGL_ALWAYS_SOFTWARE=true ' if os.environ.get('TRAMES_ZINK_CPU_REFERENCE')=='1' else ''
    if os.environ.get('TRAMES_DIALOG_NO_FEEDBACK')=='1':debug+="MESA_EXTENSION_OVERRIDE='-GL_NV_texture_barrier -GL_ARB_texture_barrier' "
    if os.environ.get('TRAMES_DIALOG_ZINK_DEBUG'):debug+='ZINK_DEBUG='+shlex.quote(os.environ['TRAMES_DIALOG_ZINK_DEBUG'])+' '
    debug+='TRAMES_VULKAN_TRACE=1 '
    if not a.native_wsi:debug+='TRAMES_XWAYLAND_NATIVE_WSI=0 '
    shell(debug+"trames-xwayland :0 >/tmp/xwayland.log 2>&1 &\n")
    shell("printf 'dialog fixture\\n' >/tmp/trames-dialog-fixture.txt; while ! xprop -root >/dev/null 2>&1; do sleep .1; done; GDK_BACKEND=x11 GTK_IM_MODULE=xim XMODIFIERS=@im=trames GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0 NO_AT_BRIDGE=1 mousepad --disable-server /tmp/trames-dialog-fixture.txt >/tmp/mousepad-dialog.log 2>&1 &\n")
    scene=wait(lambda s:any('trames-dialog-fixture' in w['title'] for w in s['windows']));editor=next(w['id'] for w in scene['windows'] if 'trames-dialog-fixture' in w['title'])
    print('editor:',scene['windows'],flush=True)
    command(op='focus',window=editor);wait(lambda _:gpu(editor));key(30)
    wait(lambda s:any('*' in w['title'] for w in s['windows']))
    for attempt in range(2):
        command(op='close',window=editor)
        scene=wait(lambda s:len(s['windows'])>=2)
        print('save dialog:',scene['windows'],flush=True)
        dialog=next(w['id'] for w in scene['windows'] if w['id']!=editor)
        wait(lambda _:gpu(dialog));command(op='focus',window=dialog);key(1)
        wait(lambda s:len(s['windows'])==1 and s['windows'][0]['id']==editor)
    report.update(passed=True,save_dialog=True,gpu=bool(a.native_wsi),metal_nonblank=bool(metal),cancel_cycles=2)
finally:
    for guest,name in [('/tmp/xwayland.log','xwayland.log'),('/tmp/mousepad-dialog.log','mousepad.log')]:export(guest,name)
    report['seconds']=time.monotonic()-began
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    api.trames_session_close(h)
