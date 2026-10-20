#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original ARM64 Xwayland/xterm/xclock through the production session ABI.

Use an isolated signed-package store; see docs/architecture/xwayland.md.
Default: Linux SHM/window/input gate. --native-wsi: original glamor/Zink and
native Vulkan WSI, with X11 pixel checks. Apple also requires a Metal oracle.
GLX/DRI3 direct rendering is outside both gates.
"""
import argparse
import ctypes as C
import json
import os
import sys
from pathlib import Path
import time

ROOT=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--store',type=Path,required=True)
p.add_argument('--ffi',type=Path,default=ROOT/'target/perf-runtime/release/libtrames_ffi.so')
p.add_argument('--output',type=Path,required=True)
p.add_argument('--timeout',type=float,default=150)
p.add_argument('--wayland-debug',action='store_true')
p.add_argument('--capture',action='store_true',help='save retained SHM image (requires Pillow)')
p.add_argument('--native-wsi',action='store_true',help='test bundled original glamor with DRM-less EGL transport')
p.add_argument('--metal-validator',type=Path,help='test-only zink-metal-pixels library, required on Apple GPU runs')
p.add_argument('--interactions',action='store_true',help='original Xlib XIM and bidirectional text clipboard gate')
p.add_argument('--wayland-peer',action='store_true',help='include original signed mousepad cross-protocol clipboard checks')
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
if hasattr(os,'sched_setaffinity'):os.sched_setaffinity(0,sorted(os.sched_getaffinity(0))[:4])
api=C.CDLL(str(a.ffi.resolve()))
api.trames_session_open.argtypes=[C.c_char_p,C.c_uint32];api.trames_session_open.restype=C.c_uint64
for name in ('input','transfer','wayland_command','bridge_reply'):getattr(api,'trames_session_'+name).argtypes=[C.c_uint64,C.c_char_p,C.c_uint32]
for name in ('status','output','wayland_status','bridge_poll'):getattr(api,'trames_session_'+name).argtypes=[C.c_uint64,C.c_void_p,C.c_uint32]
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
        metal.trames_zink_check_texture.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32]
data=json.dumps(options).encode();h=api.trames_session_open(data,len(data));assert h
output=bytearray();queries=0;began=time.monotonic();held=0;pattern_held=0;report={}
copies=[];reads=[];pending_reads=[];allow_read=False;host_text='iPad 粘贴 🧪'
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
    if a.interactions:
        while packet:=read('bridge_poll'):
            packet=json.loads(packet);request=packet['request']
            if request['method']=='clipboard.write':
                copies.append(request['params']['text'])
                payload('bridge_reply',dict(version=1,ticket=packet['ticket'],result=dict(written=True)))
            elif request['method']=='clipboard.read':
                reads.append(packet);pending_reads.append(packet)
            else:raise AssertionError(request)
        if allow_read:
            for packet in pending_reads:payload('bridge_reply',dict(version=1,ticket=packet['ticket'],result=dict(text=host_text)))
            pending_reads.clear()
    (a.output/'terminal.log').write_bytes(output)
    return scene
def wait(predicate,timeout=None):
    start=time.monotonic()
    while True:
        scene=pump()
        if predicate(scene):return scene
        if time.monotonic()-start>5 and not scene['windows']:
            export('/tmp/xwayland.log','xwayland.log')
            log=a.output/'xwayland.log'
            if log.exists():assert 'Fatal server error' not in log.read_text(errors='replace'),log.read_text(errors='replace')[-3000:]
        assert time.monotonic()-start<(timeout or a.timeout),(scene,output[-4000:])
        time.sleep(.005)
def export(guest,name):
    (a.output/name).unlink(missing_ok=True)
    b=json.dumps(dict(guest=guest,host=str((a.output/name).resolve()),export=True,maximum_bytes=8<<20)).encode()
    return api.trames_session_transfer(h,b,len(b))
try:
    wait(lambda _:json.loads(read('status'))['phase']=='running',60)
    for _ in range(25):pump();time.sleep(.01)
    if a.native_wsi:
        payload('transfer',dict(guest='/tmp/x11-pixels',host=str(ROOT/'target/xwayland-native/x11-pixels'),export=False,replace=True))
        payload('transfer',dict(guest='/tmp/libwsi-pixels.so',host=str(ROOT/'target/xwayland-native/libwsi-pixels.so'),export=False,replace=True))
        send("chmod 755 /tmp/x11-pixels\n")
    else:
        payload('transfer',dict(guest='/tmp/signal-leaf-stack',host=str(ROOT/'target/xwayland/signal-leaf-stack'),export=False,replace=True))
        send("chmod 755 /tmp/signal-leaf-stack; /tmp/signal-leaf-stack; printf '\\nSIGNAL_STACK:%d\\n' $?\n")
        wait(lambda _:b'\r\nSIGNAL_STACK:0\r\n' in output,20)
        report['signal_leaf_stack']=True
    for _ in range(25):pump();time.sleep(.01)
    debug='WAYLAND_DEBUG=1 ' if a.wayland_debug else ''
    if a.native_wsi:
        debug+='LD_PRELOAD=/tmp/libwsi-pixels.so '
        if os.environ.get('TRAMES_ZINK_CPU_REFERENCE')=='1':debug+='LIBGL_ALWAYS_SOFTWARE=true '
    else:
        debug+='TRAMES_XWAYLAND_NATIVE_WSI=0 '
    send("rm -f /tmp/xwm-key; "+debug+"trames-xwayland :0 >/tmp/xwayland.log 2>&1 &\n")
    for _ in range(25):pump();time.sleep(.01)
    send("while ! test -S /tmp/.X11-unix/X0; do sleep .1; done; DISPLAY=:0 xterm -title Trames-X11-terminal -geometry 40x10 -e /bin/sh -c 'echo X11_INPUT_READY; read line; printf \"X11_KEY:%s\\n\" \"$line\" >/tmp/xwm-key; sleep 120' >/tmp/xterm.log 2>&1 & DISPLAY=:0 xclock -name Trames-X11-clock -geometry 160x160 >/tmp/xclock.log 2>&1 &\n")
    scene=wait(lambda s:len(s['windows'])>=2)
    report['windows']=scene['windows'];print('X11 windows:',[(w['title'],w['width'],w['height']) for w in scene['windows']],flush=True)
    terminal=next(w for w in scene['windows'] if 'terminal' in w['title']);wid=terminal['id']
    deadline=time.monotonic()+10
    while True:
        pump();held=api.trames_session_wayland_frame_acquire(h,wid);assert held
        info=Info();image=Image();assert api.trames_wayland_frame_info(held,C.byref(info))==0
        assert api.trames_wayland_frame_image(held,0,C.byref(image))==0
        assert image.kind==(2 if a.native_wsi else 1),'Xwayland presentation path changed'
        if a.native_wsi:break
        pixels=C.string_at(image.address+image.offset,image.stride*image.height)
        if len({pixels[i:i+3] for i in range(0,len(pixels),4)})>1:break
        api.trames_wayland_frame_release(held,1,int(time.monotonic()*1000)&0xffffffff);held=0
        assert time.monotonic()<deadline,'X11 surface was blank'
        time.sleep(.01)
    report['gpu_image' if a.native_wsi else 'nonblank_shm']=True
    if a.capture and not a.native_wsi:
        from PIL import Image as PillowImage
        PillowImage.frombytes('RGBA',(image.width,image.height),pixels,'raw','BGRA',image.stride,1).save(a.output/'terminal.png')
    command(op='resize',window=wid,width=480,height=320)
    wait(lambda s:any(w['id']==wid and w['width']==480 and w['height']==320 for w in s['windows']))
    report['resize']=True
    command(op='focus',window=wid)
    # Native evdev -> wl_keyboard -> Xwayland -> X11 -> ordinary shell read.
    for key in (30,28):
        for pressed in (True,False):command(op='key',key=key,pressed=pressed,modifiers=0,time=1)
    deadline=time.monotonic()+30
    while True:
        pending=a.output/'input.pending';pending.unlink(missing_ok=True)
        if export('/tmp/xwm-key','input.pending')>=0 and pending.read_bytes()==b'X11_KEY:a\n':
            pending.replace(a.output/'input.txt');break
        pump();assert time.monotonic()<deadline;time.sleep(.02)
    assert (a.output/'input.txt').read_text()=='X11_KEY:a\n'
    report['keyboard']=True
    if a.interactions:
        payload('transfer',dict(guest='/tmp/x11-input',host=str(ROOT/'target/xwayland-native/x11-input'),export=False,replace=True))
        send('rm -f /tmp/x11-committed /tmp/x11-pasted /tmp/x11-events; chmod 755 /tmp/x11-input; DISPLAY=:0 /tmp/x11-input >/tmp/x11-input.log 2>&1 &\n')
        scene=wait(lambda s:any(w['title']=='Trames-X11-input' for w in s['windows']))
        iw=next(w['id'] for w in scene['windows'] if w['title']=='Trames-X11-input')
        command(op='focus',window=iw)
        scene=wait(lambda s:s.get('text') and s['text']['window']==iw and s['text']['version']==4)
        context=scene['text']
        def edit(**values):command(op='text',context=context['id'],epoch=context['epoch'],serial=context['serial'],**values)
        def key(code):
            for down in (True,False):command(op='key',key=code,pressed=down,modifiers=0,time=1)
        def file_equals(guest,expected):
            return export(guest,'file.pending')>=0 and (a.output/'file.pending').read_bytes()==expected
        edit(preedit='zhong',cursor_begin=5,cursor_end=5)
        for _ in range(10):pump();time.sleep(.01)
        assert export('/tmp/x11-committed','commit.pending')<0,'preedit was committed'
        edit(preedit='',commit='中文🧪');edit(commit=' 输入');key(105);edit(commit='后')
        wait(lambda _:file_equals('/tmp/x11-events','中文🧪 输入<left>后'.encode()),30)
        report['xim_unicode_and_key_order']=True
        key(66) # F8 resets this IC without changing the native window.
        scene=wait(lambda s:s.get('text') and s['text']['window']==iw and s['text']['epoch']!=context['epoch'])
        edit(commit='STALE_RESET')
        context=scene['text']
        assert not reads,'clipboard read before paste'
        for down in (True,False):command(op='key',key=46,pressed=down,modifiers=4 if down else 0,time=1)
        wait(lambda _:'X11 复制 🧪' in copies,20)
        command(op='pointer',window=iw,x=30,y=30,time=2)
        for down in (True,False):command(op='button',button=0x111,pressed=down,time=2)
        wait(lambda _:'X11 菜单复制 🧪' in copies,20)
        key(65) # F7 forces ICCCM INCR even for this bounded text payload.
        wait(lambda _:'段'*3000 in copies,20)
        assert not reads,'copy requested host paste permission'
        command(op='clipboard_changed')
        for _ in range(10):pump();time.sleep(.01)
        command(op='key',key=64,pressed=True,modifiers=0,time=3) # Hold F6 over permission.
        wait(lambda _:bool(reads),20)
        for _ in range(80):pump();time.sleep(.01)
        assert len(reads)==1,'held paste repeated during permission prompt'
        allow_read=True
        wait(lambda _:file_equals('/tmp/x11-pasted',host_text.encode()),20)
        command(op='key',key=64,pressed=False,modifiers=0,time=4)
        if a.wayland_peer:
            peer_text='Wayland 复制 🧪'
            peer_file=a.output/'peer.txt';peer_file.write_text(peer_text)
            payload('transfer',dict(guest='/tmp/wayland-peer.txt',host=str(peer_file.resolve()),export=False,replace=True))
            send('GDK_BACKEND=wayland GTK_IM_MODULE=wayland GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0 NO_AT_BRIDGE=1 mousepad --disable-server /tmp/wayland-peer.txt >/tmp/wayland-peer.log 2>&1 &\n')
            scene=wait(lambda s:any('Mousepad' in w['title'] or 'mousepad' in w['app_id'].lower() for w in s['windows']))
            peer=next(w['id'] for w in scene['windows'] if 'Mousepad' in w['title'] or 'mousepad' in w['app_id'].lower())
            command(op='focus',window=peer)
            wait(lambda s:s.get('text') and s['text']['window']==peer and peer_text in s['text']['text'])
            for code in (30,46):
                for down in (True,False):command(op='key',key=code,pressed=down,modifiers=4 if down else 0,time=5)
            wait(lambda _:peer_text in copies)
            command(op='focus',window=iw)
            wait(lambda s:s.get('text') and s['text']['window']==iw)
            key(64)
            wait(lambda _:file_equals('/tmp/x11-pasted',peer_text.encode()))
            key(63)
            wait(lambda _:copies[-1]=='X11 复制 🧪')
            command(op='focus',window=peer)
            wait(lambda s:s.get('text') and s['text']['window']==peer)
            for code in (30,47):
                for down in (True,False):command(op='key',key=code,pressed=down,modifiers=4 if down else 0,time=6)
            # GTK's asynchronous paste can update the document before its IM
            # surrounding-text snapshot. Verify the saved document itself.
            deadline=time.monotonic()+20
            while True:
                for down in (True,False):command(op='key',key=31,pressed=down,modifiers=4 if down else 0,time=7)
                for _ in range(10):pump();time.sleep(.01)
                if export('/tmp/wayland-peer.txt','peer-saved.txt')>=0 and (a.output/'peer-saved.txt').read_bytes().rstrip(b'\n')=='X11 复制 🧪'.encode():break
                assert time.monotonic()<deadline,(a.output/'peer-saved.txt').read_bytes()
            assert len(reads)==1,'guest clipboard transfer unnecessarily read host clipboard'
            report.update(wayland_to_x11=True,x11_to_wayland=True)
            # Discard unsaved editor changes before closing, keeping this gate
            # focused on selection transport rather than dialog layout.
            send('killall mousepad\n')
            wait(lambda s:all(w['id']!=peer for w in s['windows']))
            command(op='focus',window=iw)
            scene=wait(lambda s:s.get('text') and s['text']['window']==iw)
            context=scene['text']
        # An old native composition must not reach a newly focused application.
        command(op='focus',window=wid);edit(commit='STALE')
        for _ in range(10):pump();time.sleep(.01)
        assert file_equals('/tmp/x11-committed','中文🧪 输入后'.encode())
        command(op='close',window=iw)
        wait(lambda s:all(w['id']!=iw for w in s['windows']))
        report.update(x11_copy=True,x11_menu_copy=True,x11_incr_bytes=9000,host_paste=True,permission_repeat=False,stale_ime_rejected=True,xim_reset=True)
    if a.native_wsi:
        send('DISPLAY=:0 /tmp/x11-pixels >/tmp/x11-pixels.log 2>&1 &\n')
        scene=wait(lambda s:any(w['title']=='Trames-X11-pixels' for w in s['windows']))
        pattern=next(w['id'] for w in scene['windows'] if w['title']=='Trames-X11-pixels')
        def check_pattern(width,height):
            deadline=time.monotonic()+30
            while True:
                pump()
                pending=a.output/'pixels.pending';pending.unlink(missing_ok=True)
                ready=export('/tmp/x11-pixels.log','pixels.pending')>=0 and (
                    f'x11-pixels-ok: {width}x{height} {width*height} pixels\n'.encode() in pending.read_bytes())
                pending=a.output/'wsi.pending';pending.unlink(missing_ok=True)
                ready=ready and export('/tmp/xwayland.log','wsi.pending')>=0 and (
                    f'x11-wsi-pixels-ok: {width}x{height} {width*height} pixels\n'.encode() in pending.read_bytes())
                frame=api.trames_session_wayland_frame_acquire(h,pattern)
                if frame:
                    info=Info();image=Image()
                    assert api.trames_wayland_frame_info(frame,C.byref(info))==0
                    assert api.trames_wayland_frame_image(frame,0,C.byref(image))==0 and image.kind==2
                    matches=ready and (image.width,image.height)==(width,height)
                    if matches and metal:
                        matches=metal.trames_zink_check_texture(image.address,width,height,0)==0
                    if matches:return frame
                    api.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)
                assert time.monotonic()<deadline,'X11 pattern pixels/size not ready'
                time.sleep(.01)
        pattern_held=check_pattern(128,96)
        command(op='resize',window=pattern,width=160,height=100)
        frame=check_pattern(160,100)
        api.trames_wayland_frame_release(frame,1,int(time.monotonic()*1000)&0xffffffff)
        command(op='close',window=pattern)
        wait(lambda s:all(w['id']!=pattern for w in s['windows']))
        report.update(x11_pixel_checks=[128*96,160*100],presentation_pixel_checks=[128*96,160*100],metal_pixels=bool(metal))
    # xclock has WM_DELETE_WINDOW; xterm has it too. Close only the intended X11
    # client and leave the other window and Wayland compositor alive.
    command(op='close',window=wid)
    wait(lambda s:len(s['windows'])==1 and all(w['id']!=wid for w in s['windows']))
    command(op='close',window=pump()['windows'][0]['id'])
    wait(lambda s:not s['windows'])
    report['close']=True
    send("killall Xwayland Xwayland-native 2>/dev/null; wait; dmesg >/tmp/xwm-dmesg; echo XWM_DONE\n")
    wait(lambda _:b'\r\nXWM_DONE\r\n' in output)
    if a.native_wsi:
        assert export('/tmp/xwayland.log','xwayland.log')>=0
        log=(a.output/'xwayland.log').read_text(errors='replace')
        assert 'Trames Xwayland native WSI:' in log and 'no SHM presentation' in log
    report['passed']=True
finally:
    if json.loads(read('status'))['phase']=='running':
        for _ in range(25):pump();time.sleep(.01)
        send("dmesg >/tmp/xwm-dmesg; ps -o pid,ppid,pgid,sid,stat,args >/tmp/xwm-processes; echo XWM_DIAG_DONE\n")
        deadline=time.monotonic()+10
        while b'\r\nXWM_DIAG_DONE\r\n' not in output and time.monotonic()<deadline:
            pump();time.sleep(.01)
    for guest,name in [('/tmp/xwayland.log','xwayland.log'),('/tmp/xterm.log','xterm.log'),('/tmp/xclock.log','xclock.log'),('/tmp/xwm-dmesg','dmesg.log'),('/tmp/xwm-processes','processes.log')]:export(guest,name)
    if a.native_wsi:
        export('/tmp/x11-pixels.log','pixels.log')
    if a.interactions:export('/tmp/x11-input.log','input.log')
    if a.wayland_peer:export('/tmp/wayland-peer.log','wayland-peer.log')
    report['seconds']=time.monotonic()-began
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    api.trames_session_close(h)
    if held:
        info=Info();assert api.trames_wayland_frame_info(held,C.byref(info))==0
        image=Image();assert api.trames_wayland_frame_image(held,0,C.byref(image))==0
        if not a.native_wsi:assert C.string_at(image.address+image.offset,16)==pixels[:16]
        assert api.trames_wayland_frame_release(held,0,0)==0
    if pattern_held:
        image=Image();assert api.trames_wayland_frame_image(pattern_held,0,C.byref(image))==0
        assert (image.kind,image.width,image.height)==(2,128,96)
        if metal:assert metal.trames_zink_check_texture(image.address,128,96,0)==0
        assert api.trames_wayland_frame_release(pattern_held,0,0)==0
print('Xwayland: '+('glamor/Zink GPU images and X11 pixels' if a.native_wsi else 'nonblank SHM')+
      ', resize, keyboard, independent close and retained frames passed',flush=True)
