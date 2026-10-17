#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original ARM64 mousepad: Ctrl+C, right-click Copy and navigation/IME.

Use an existing signed-package guest store containing mousepad and font-dejavu.
This exercises the production guest/SCM_RIGHTS/native clipboard broker, not UIKit.
"""
import ctypes as C
import json
import os
from pathlib import Path
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
store = Path(os.environ['TRAMES_CLIPBOARD_STORE']).resolve()
assert (store / 'manifest').exists(), 'Prepare a guest with signed mousepad and font-dejavu first'
api = C.CDLL(os.environ.get('TRAMES_FFI_LIB', str(ROOT / 'target/release' / ('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so'))))
api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
api.trames_session_open.restype = C.c_uint64
for name in ('input', 'transfer', 'wayland_command', 'bridge_reply'):
    getattr(api, 'trames_session_' + name).argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
for name in ('output', 'status', 'wayland_status', 'bridge_poll'):
    getattr(api, 'trames_session_' + name).argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_close.argtypes = [C.c_uint64]
api.trames_session_wayland_frame_acquire.argtypes = [C.c_uint64, C.c_uint64]
api.trames_session_wayland_frame_acquire.restype = C.c_uint64
api.trames_wayland_frame_release.argtypes = [C.c_uint64, C.c_uint32, C.c_uint32]
class FrameInfo(C.Structure):
    _fields_ = [('width', C.c_uint32), ('height', C.c_uint32), ('images', C.c_uint32), ('reserved', C.c_uint32), ('revision', C.c_uint64)]
api.trames_wayland_frame_info.argtypes = [C.c_uint64, C.POINTER(FrameInfo)]
options = json.dumps(dict(store=str(store), volume_total=16 << 30, volume_available=16 << 30, network='off', wayland=True)).encode()
handle = api.trames_session_open(options, len(options))
assert handle
output = ''
def read(name):
    data = C.create_string_buffer(65536)
    count = getattr(api, 'trames_session_' + name)(handle, data, len(data))
    assert count >= 0, (name, count)
    return data.raw[:count]
def payload(name, value):
    data = json.dumps(value).encode()
    assert getattr(api, 'trames_session_' + name)(handle, data, len(data)) >= 0, name
def command(**value):
    payload('wayland_command', value)
def key(code, modifiers=0):
    for pressed in (True, False):
        command(op='key', key=code, modifiers=modifiers if pressed else 0, pressed=pressed, time=1)
try:
    began = time.monotonic()
    while json.loads(read('status'))['phase'] == 'starting':
        assert time.monotonic() - began < 60
        time.sleep(.01)
    assert json.loads(read('status'))['phase'] == 'running'
    expected = 'mousepad guest 中文 🧪\n'
    with tempfile.NamedTemporaryFile() as text:
        text.write(expected.encode()); text.flush()
        payload('transfer', dict(guest='/tmp/clipboard-regression.txt', host=text.name, export=False, replace=True))
    shell = b'GDK_BACKEND=wayland GTK_IM_MODULE=wayland GSETTINGS_BACKEND=memory GTK_USE_PORTAL=0 NO_AT_BRIDGE=1 mousepad --disable-server /tmp/clipboard-regression.txt\n'
    assert api.trames_session_input(handle, shell, len(shell)) == len(shell)
    focused = copied = menu_sent = menu_activated = navigated = edited = False
    copies = 0
    began = time.monotonic()
    while time.monotonic() - began < 90:
        output += read('output').decode(errors='replace')
        scene = json.loads(read('wayland_status'))
        context = scene.get('text')
        for window in scene['windows']:
            images = 0
            frame = api.trames_session_wayland_frame_acquire(handle, window['id'])
            if frame:
                info = FrameInfo(); assert api.trames_wayland_frame_info(frame, C.byref(info)) == 0
                images = info.images
                assert api.trames_wayland_frame_release(frame, 1, int(time.monotonic() * 1000) & 0xffffffff) == 0
            if not focused:
                command(op='focus', window=window['id']); focused = True
            if context and expected.strip() in context['text'] and not copied:
                key(30, 4); key(46, 4); copied = True
            if copies == 1 and not menu_sent:
                command(op='pointer', window=window['id'], x=window['width'] / 4, y=window['height'] / 3, time=2)
                for pressed in (True, False):
                    command(op='button', button=0x111, pressed=pressed, time=2)
                menu_sent = True
            if menu_sent and not menu_activated and images > 1:
                key(46); menu_activated = True
            if copies == 2 and context and not navigated:
                key(105); navigated = True
            elif navigated and context and context['cursor'] == context['anchor'] == 0 and not edited:
                command(op='text', context=context['id'], epoch=context['epoch'], serial=context['serial'], commit='新')
                edited = True
            elif edited and context and context['text'].startswith('新mousepad'):
                print('PASS: Ctrl+C, right-click Copy, restored focus and navigation followed by IME text', flush=True)
                break
        else:
            request = read('bridge_poll')
            if request:
                request = json.loads(request)
                assert request['request']['method'] == 'clipboard.write', request
                assert request['request']['params']['text'] == expected, request
                payload('bridge_reply', dict(version=1, ticket=request['ticket'], result=dict(written=True)))
                copies += 1
                print(('Ctrl+C', 'right-click Copy')[copies - 1] + ': exact UTF-8 host clipboard request', flush=True)
            time.sleep(.005)
            continue
        break
    else:
        raise AssertionError(f'copies={copies}, menu={menu_activated}, navigated={navigated}, edited={edited}\n{output[-6000:]}')
finally:
    if os.environ.get('TRAMES_CLIPBOARD_LOG'):
        Path(os.environ['TRAMES_CLIPBOARD_LOG']).write_text(output)
    api.trames_session_close(handle)
