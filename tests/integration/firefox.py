#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe original ARM64 Firefox via the production App session/compositor ABI.

Prepare an isolated store with signed firefox/font-dejavu APKs first. Defaults
retain Firefox's sandbox. Optional disabling is an explicitly recorded diagnosis.
No browser binary or libxul patch; use an offline page with a JS/title marker.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--stage', choices=['headless', 'wayland', 'graphics'], required=True)
p.add_argument('--backend', choices=['wayland', 'x11'], default='wayland', help='X11 windows use the native XWM and bundled Xwayland launcher')
p.add_argument('--ffi', type=Path, default=ROOT/'target/perf-runtime/release/libtrames_ffi.so')
p.add_argument('--musl', type=Path, default=ROOT/'target/musl-hybrid/libc.so')
p.add_argument('--timeout', type=float, default=180)
p.add_argument('--diagnostic-timeout', type=float, default=15)
p.add_argument('--guest-progress', action='store_true', help='intrusive diagnosis: periodic guest ps/proc commands; disabled for startup gates')
p.add_argument('--memory-gib', type=int, default=4, help='aggregate guest memory budget; overrides are diagnostic')
p.add_argument('--process-memory-gib', type=int, default=4)
p.add_argument('--host-cpus', type=int, default=4, choices=range(1,65), help='diagnosis only: restrict host CPU affinity before opening the production session; default 4')
p.add_argument('--gpu', action='store_true')
p.add_argument('--disable-content-sandbox', action='store_true')
p.add_argument('--disable-wayland-proxy', action='store_true', help='diagnosis only: bypass the known FIONREAD blocker')
p.add_argument('--avoid-opengl', action='store_true', help='diagnosis only: skip Firefox graphics probing')
p.add_argument('--wayland-debug', action='store_true')
p.add_argument('--moz-log', help='diagnosis only: original Firefox MOZ_LOG modules, e.g. Process:5,nsDocShell:5,DocumentChannel:5')
p.add_argument('--browser-log-interval', type=float, default=0, help='diagnosis only: export the latest browser log without executing guest ps/cat; 0 disables')
p.add_argument('--pref', action='append', default=[], metavar='NAME=JSON', help='diagnosis only: write a Firefox preference in this fresh profile; repeatable')
p.add_argument('--env', action='append', default=[], metavar='NAME=VALUE', help='diagnosis only: additional browser environment; repeatable')
p.add_argument('--capture-shm', action='store_true', help='save software-composited PNG and require the offline canvas green rectangle; requires Pillow')
p.add_argument('--native-capture', type=Path, help='diagnosis only: BGRA/JSON prefix from a private native GPU readback oracle; never dereference opaque GPU handles')
p.add_argument('--require-gpu-frame', action='store_true', help='require a native GPU image on the JS-marked Wayland window; transport gate only, not canvas pixel validation')
p.add_argument('--interact', action='store_true', help='after the page loads, verify pointer focus, native Unicode text, navigation keys, resize and close; requires Wayland and --capture-shm')
p.add_argument('--close-only', action='store_true', help='close after the pixel/title gate; reuse passed input/resize checks; requires --interact')
p.add_argument('--resize-before-close', action='store_true', help='target resize/close teardown without repeating input checks; requires --close-only')
p.add_argument('--close-timeout', type=float, default=45, help='bounded exit wait after close; diagnostics may explicitly allow slower shutdown')
p.add_argument('--sampler', type=Path, help='optional Linux host PC sampling; not an uninstrumented timing run')
a = p.parse_args()
environment = {}
for item in a.env:
    name, separator, value = item.partition('=')
    if not separator or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name) or '\0' in value:
        p.error('--env requires NAME=VALUE without NUL characters')
    if name in {'MOZ_DISABLE_CONTENT_SANDBOX', 'MOZ_DISABLE_WAYLAND_PROXY',
                'MOZ_AVOID_OPENGL_ALTOGETHER', 'MOZ_ENABLE_WAYLAND', 'GDK_BACKEND',
                'XDG_RUNTIME_DIR', 'WAYLAND_DISPLAY', 'MOZ_LOG'}:
        p.error('--env cannot override probe-controlled backend, sandbox or logging options')
    environment[name] = value
preferences = {}
for preference in a.pref:
    name, separator, raw = preference.partition('=')
    if not separator or not re.fullmatch(r'[A-Za-z0-9_.-]+', name):
        p.error('--pref requires NAME=JSON')
    try:
        value = json.loads(raw)
    except ValueError:
        p.error('--pref value must be a JSON boolean, string, or signed 32-bit integer')
    if not (isinstance(value, (bool, str)) or type(value) is int and -(1<<31) <= value < 1<<31):
        p.error('--pref value must be a JSON boolean, string, or signed 32-bit integer')
    preferences[name] = value
if a.browser_log_interval and a.browser_log_interval < 5:
    p.error('--browser-log-interval must be 0 or at least 5 seconds')
if a.native_capture and (a.capture_shm or not a.gpu or a.stage != 'wayland' or a.backend != 'wayland'):
    p.error('--native-capture requires --gpu --stage wayland --backend wayland without --capture-shm')
capture_pixels = a.capture_shm or bool(a.native_capture)
if a.close_only and not a.interact: p.error('--close-only requires --interact')
if a.resize_before_close and not a.close_only: p.error('--resize-before-close requires --close-only')
if not 0 < a.close_timeout <= 300: p.error('--close-timeout must be positive and at most 300 seconds')
if a.interact and (a.stage != 'wayland' or a.backend != 'wayland' or not capture_pixels):
    p.error('--interact requires native Wayland and a pixel capture oracle')
if a.require_gpu_frame and (not a.gpu or a.stage != 'wayland' or a.backend != 'wayland' or a.capture_shm):
    p.error('--require-gpu-frame requires --gpu --stage wayland --backend wayland without --capture-shm')
if capture_pixels:
    from PIL import Image as PillowImage, ImageChops
a.output.mkdir(parents=True, exist_ok=False)
if hasattr(os, 'sched_setaffinity'):
    os.sched_setaffinity(0, sorted(os.sched_getaffinity(0))[:a.host_cpus])
elif a.host_cpus != 4:
    p.error('--host-cpus requires host CPU-affinity support')

api = C.CDLL(str(a.ffi.resolve()))
api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
api.trames_session_open.restype = C.c_uint64
for name in ('input', 'transfer', 'wayland_command'):
    getattr(api, 'trames_session_'+name).argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
for name in ('status', 'output', 'wayland_status'):
    getattr(api, 'trames_session_'+name).argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_close.argtypes = [C.c_uint64]
api.trames_session_wayland_frame_acquire.argtypes = [C.c_uint64, C.c_uint64]
api.trames_session_wayland_frame_acquire.restype = C.c_uint64
api.trames_wayland_frame_release.argtypes = [C.c_uint64, C.c_uint32, C.c_uint32]
class Info(C.Structure):
    _fields_ = [('width', C.c_uint32), ('height', C.c_uint32), ('images', C.c_uint32),
               ('reserved', C.c_uint32), ('revision', C.c_uint64)]
api.trames_wayland_frame_info.argtypes = [C.c_uint64, C.POINTER(Info)]
class Image(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in ('kind','address','length','offset')] + [
        (n, C.c_uint32) for n in ('width','height','stride','alpha')] + [
        (n, C.c_int32) for n in ('x','y','scale')] + [('transform', C.c_uint32)]
api.trames_wayland_frame_image.argtypes = [C.c_uint64, C.c_uint32, C.POINTER(Image)]

def color_mask(canvas, color):
    channels = canvas.convert('RGB').split()
    matches = [channel.point([255 if i == expected else 0 for i in range(256)])
               for channel, expected in zip(channels, color)]
    return ImageChops.multiply(ImageChops.multiply(matches[0],matches[1]),matches[2])

def green_pixels(canvas):
    return color_mask(canvas, (22,163,74)).histogram()[255]

def capture(frame, info):
    """Read retained SHM or explicit diagnostic bytes; GPU handles stay opaque."""
    if not (0 < info.width <= 4096 and 0 < info.height <= 4096): return None
    canvas = PillowImage.new('RGBA', (info.width, info.height), (0,0,0,255))
    for i in range(info.images):
        image = Image()
        assert api.trames_wayland_frame_image(frame, i, C.byref(image)) == 0
        if image.kind == 2:
            if not a.native_capture or image.transform or image.scale < 1: return None
            try:
                meta = json.loads(a.native_capture.with_suffix('.json').read_text())
                if (meta['width'],meta['height']) != (image.width,image.height): return None
                pixels = a.native_capture.with_suffix('.bgra').read_bytes()
                if len(pixels) != image.width*image.height*4: return None
                layer = PillowImage.frombytes('RGBA', (image.width,image.height), pixels, 'raw', 'BGRA')
            except (OSError, ValueError, KeyError):
                return None
            if image.scale != 1:
                layer = layer.resize((max(1,image.width//image.scale), max(1,image.height//image.scale)))
            canvas.alpha_composite(layer, (image.x,image.y))
            continue
        size = image.stride * image.height
        if (image.kind != 1 or not image.address or image.transform or image.scale < 1
            or not 0 < image.width <= 4096 or not 0 < image.height <= 4096
            or image.stride < image.width*4 or size > 64<<20
            or image.offset + size > image.length): return None
        pixels = C.string_at(image.address + image.offset, size)
        layer = PillowImage.frombytes('RGBA', (image.width,image.height), pixels,
                                     'raw', 'BGRA', image.stride, 1)
        if not image.alpha: layer.putalpha(255)
        if image.scale != 1:
            layer = layer.resize((max(1,image.width//image.scale), max(1,image.height//image.scale)))
        canvas.alpha_composite(layer, (image.x,image.y))
    canvas.save(a.output/'wayland.png')
    if a.interact:
        # Locate the actual page controls in the presented pixels. Browser
        # decoration heights and subsurface offsets are not fixed coordinates.
        targets = {}
        for name, color in [('a', (213,57,169)), ('b', (38,139,210))]:
            bounds = color_mask(canvas, color).getbbox()
            if bounds:
                x, y, right, bottom = bounds
                if right-x >= 200 and bottom-y >= 30:
                    targets[name] = [(x+right)/2, (y+bottom)/2]
        report['interaction_targets'] = targets
    return green_pixels(canvas)

options = dict(store=str(a.store.resolve()), volume_total=16<<30, volume_available=16<<30,
    network='off', wayland=True, pure_accelerator=True, musl_libc=str(a.musl.resolve()),
    memory_budget_bytes=a.memory_gib<<30, process_memory_budget_bytes=a.process_memory_gib<<30,
    vulkan_accelerator=a.gpu)
if a.gpu:
    options['vulkan_icd'] = str(ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so')
if a.backend == 'x11':
    options['xwayland_launcher'] = str(ROOT/'target/vulkan-guest/trames-xwayland')
report = dict(stage=a.stage, backend=a.backend, sandbox='content-disabled-diagnostic' if a.disable_content_sandbox else 'default',
    wayland_proxy='disabled-diagnostic' if a.disable_wayland_proxy else 'default',
    avoid_opengl_diagnostic=a.avoid_opengl,
    host_cpus_requested=a.host_cpus,
    host_cpu_affinity=sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
    physical_ipad=False, options=options, frames=0, windows=[], js_title_seen=False,
    guest_progress_diagnostic=a.guest_progress,
    moz_log_diagnostic=a.moz_log,
    browser_log_interval_diagnostic=a.browser_log_interval,
    preferences_diagnostic=preferences,
    environment_diagnostic=environment,
    cpu_vulkan_reference_diagnostic=a.gpu and os.environ.get('TRAMES_ZINK_CPU_REFERENCE') == '1',
    capture_shm=a.capture_shm,
    native_capture_diagnostic=bool(a.native_capture),
    require_gpu_frame=a.require_gpu_frame,
    js_window_gpu_frame_seen=False,
    interaction_requested=a.interact,
    close_only=a.close_only,
    resize_before_close=a.resize_before_close,
    close_timeout=a.close_timeout,
    probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    ffi_sha256=hashlib.sha256(a.ffi.read_bytes()).hexdigest(),
    musl_sha256=hashlib.sha256(a.musl.read_bytes()).hexdigest(),
    input_manifest_sha256=hashlib.sha256((a.store/'manifest').read_bytes()).hexdigest())
data = json.dumps(options).encode()
h = api.trames_session_open(data, len(data))
assert h, 'session open failed'
sampler = None
output = bytearray()
queries = 0
def read(name):
    b = C.create_string_buffer(65536)
    n = getattr(api, 'trames_session_'+name)(h, b, len(b))
    assert n >= 0, (name, n)
    return b.raw[:n]
def send(value):
    b = value.encode()
    assert api.trames_session_input(h, b, len(b)) == len(b)
def payload(name, value):
    b = json.dumps(value).encode()
    assert getattr(api, 'trames_session_'+name)(h, b, len(b)) >= 0

class Interaction:
    """Commands enter through the same native compositor ABI as the App UI.

    Every transition waits for a DOM acknowledgement from the real browser;
    a successful command enqueue or changing frame revision is not a pass.
    """
    def __init__(self):
        self.step = 'page'
        self.entered = time.monotonic()
        self.window = None
        self.original_size = None
        report['interaction_steps'] = []

    def advance(self, step):
        report['interaction_steps'].append(dict(step=self.step, seconds=time.monotonic()-self.entered))
        self.step = step
        self.entered = time.monotonic()
        report['interaction_step'] = step

    def command(self, **values):
        payload('wayland_command', values)

    def click(self, name):
        x, y = report['interaction_targets'][name]
        self.command(op='pointer', window=self.window, x=x, y=y, time=int(time.monotonic()*1000)&0xffffffff)
        for pressed in (True, False):
            self.command(op='button', button=0x110, pressed=pressed, time=int(time.monotonic()*1000)&0xffffffff)

    def key(self, key):
        for pressed in (True, False):
            self.command(op='key', key=key, pressed=pressed, modifiers=0, time=int(time.monotonic()*1000)&0xffffffff)

    def text(self, context, value):
        self.command(op='text', serial=context['serial'], context=context['id'], epoch=context['epoch'], preedit='', commit=value)

    def poll(self, scene):
        if self.step in ('done', 'failed'): return
        if self.step != 'page' and time.monotonic()-self.entered > (a.close_timeout if self.step == 'close' else 45):
            report['interaction_error'] = 'no browser acknowledgement for '+self.step
            self.advance('failed')
            return
        window = next((w for w in scene.get('windows', []) if w['id'] == self.window), None)
        if self.step == 'page':
            if not (report['js_title_seen'] and report.get('canvas_green_seen')
                    and set(report.get('interaction_targets', {})) == {'a', 'b'}): return
            window = next(w for w in scene['windows'] if 'TRAMES_FIREFOX_JS_OK' in w.get('title', ''))
            self.window = window['id']
            self.original_size = (window['width'], window['height'])
            if a.close_only:
                if a.resize_before_close:
                    self.command(op='resize', window=self.window, width=800, height=600)
                    self.advance('resize_close')
                else:
                    self.command(op='close', window=self.window)
                    self.advance('close')
                return
            self.click('a')
            self.advance('focus_a')
            return
        if self.step == 'close':
            if not window:
                report['interaction_window_closed'] = True
                report.setdefault('interaction_window_close_seconds', time.monotonic()-self.entered)
            if not window and re.search(rb'\r?\nFIREFOX_PROBE_EXIT:0\r?\n', output):
                report['interaction_passed'] = True
                self.advance('done')
            return
        if self.step == 'resize_close':
            frame = report.get('last_frame', {})
            if window and (window['width'],window['height']) == (800,600) and (frame.get('width'),frame.get('height')) == (800,600):
                self.command(op='close', window=self.window)
                self.advance('close')
            return
        if not window: return
        title = window.get('title', '')
        if not title.startswith('TRAMES_FIREFOX_INPUT:'): return
        try:
            state, _ = json.JSONDecoder().raw_decode(title[len('TRAMES_FIREFOX_INPUT:'):])
        except ValueError:
            return
        report['interaction_dom'] = state
        context = scene.get('text')
        editable = context and context['window'] == self.window
        if self.step == 'focus_a' and state['focus'] == 'a' and editable:
            self.text(context, 'ab你🙂'); self.advance('type_a')
        elif self.step == 'type_a' and state['a'] == 'ab你🙂':
            self.key(105); self.advance('left_a')
        elif self.step == 'left_a' and state['cursor'] == 3 and state['key'] == 'ArrowLeft':
            self.key(14); self.advance('backspace_a')
        elif self.step == 'backspace_a' and state['a'] == 'ab🙂' and editable:
            self.text(context, 'Z'); self.advance('resume_a')
        elif self.step == 'resume_a' and state['a'] == 'abZ🙂':
            self.click('b'); self.advance('focus_b')
        elif self.step == 'focus_b' and state['focus'] == 'b' and editable:
            self.text(context, '第二框'); self.advance('type_b')
        elif self.step == 'type_b' and state['a'] == 'abZ🙂' and state['b'] == '第二框':
            self.click('a'); self.advance('focus_return')
        elif self.step == 'focus_return' and state['focus'] == 'a':
            self.key(107); self.advance('end_a')
        elif self.step == 'end_a' and state['cursor'] == 5 and state['key'] == 'End' and editable:
            self.text(context, '返回'); self.advance('return_a')
        elif self.step == 'return_a' and state['a'] == 'abZ🙂返回' and state['b'] == '第二框':
            report['interaction_input_passed'] = True
            self.command(op='resize', window=self.window, width=800, height=600)
            self.advance('resize')
        elif self.step == 'resize' and (window['width'], window['height']) == (800,600):
            frame = report.get('last_frame', {})
            if (frame.get('width'), frame.get('height')) == (800,600) and state['width'] < self.original_size[0] and state['height'] < self.original_size[1]:
                report['interaction_resize_passed'] = True
                self.command(op='close', window=self.window); self.advance('close')

def browser_log_snapshot(seconds):
    # Host-authorized file export at a coordinator boundary: no new guest
    # process, /proc reconstruction, or page/CPU instrumentation. Still mark
    # this run diagnostic; log I/O can perturb startup and scheduling.
    pending = a.output/'browser-log.pending'
    pending.unlink(missing_ok=True)
    b = json.dumps(dict(guest='/tmp/firefox-probe.log', host=str(pending.resolve()),
                        export=True, maximum_bytes=2<<20)).encode()
    result = api.trames_session_transfer(h, b, len(b))
    if result >= 0:
        pending.replace(a.output/'browser-live.log')
    report.setdefault('browser_log_snapshots', []).append(dict(seconds=seconds, exported=result>=0))
def host_memory():
    """Linux probe RSS includes guest storage and runtime bookkeeping."""
    status = Path('/proc/self/status')
    if not status.exists(): return {}
    return {key: int(value.split()[0])*1024 for key, value in
        (line.split(':',1) for line in status.read_text().splitlines() if ':' in line)
        if key in ('VmRSS','VmHWM','VmSize')}
memory_command = "cat /proc/meminfo; grep -E 'Name:|Pid:|PPid:|VmRSS:|VmSize:|Threads:' /proc/[0-9]*/status"
def pump():
    global queries
    chunk = read('output')
    output.extend(chunk)
    if chunk:
        with (a.output/'terminal-live.log').open('ab') as log: log.write(chunk)
    wanted = output.count(b'\x1b[6n')
    while queries < wanted:
        send('\x1b[1;5R'); queries += 1
    return json.loads(read('status'))
def wait(marker, timeout):
    began = time.monotonic()
    while marker not in output:
        s = pump()
        if s['phase'] != 'running':
            report['status'] = s
            return False
        if time.monotonic()-began > timeout:
            return False
        time.sleep(.01)
    return True

began = time.monotonic()
try:
    if a.sampler:
        sampler = C.CDLL(str(a.sampler.resolve()))
        sampler.trames_sample_start.argtypes = [C.c_char_p, C.c_char_p]
        assert sampler.trames_sample_start(str(a.ffi.resolve()).encode(), str((a.output/'samples.txt').resolve()).encode()) == 0
        report['host_pc_sampling'] = True
        maps = Path('/proc/self/maps')
        if maps.exists(): (a.output/'host-maps.txt').write_text(maps.read_text())
    while json.loads(read('status'))['phase'] == 'starting':
        assert time.monotonic()-began < 90
        time.sleep(.01)
    for _ in range(25): pump(); time.sleep(.01)
    if a.gpu:
        # Firefox's normal APK dependencies omit dynamically loaded Gallium
        # and the Vulkan loader. Do not mislabel that setup as a GPU test.
        send("test -r /usr/local/lib/libEGL.so.1 && test -r /usr/lib/libvulkan.so.1 && test -r /usr/lib/libGLESv2.so.2 && ls /usr/lib/libgallium-*.so >/dev/null 2>&1; printf '\\nFIREFOX_GPU_INPUTS:%d\\n' $?\n")
        assert wait(b'\r\nFIREFOX_GPU_INPUTS:', 20), 'GPU dependency check did not complete'
        assert wait(b'\r\nFIREFOX_GPU_INPUTS:0\r\n', 2), 'Prepare original GPU dependencies with firefox-prepare.py --gpu'
        report['gpu_inputs_verified'] = True
        for _ in range(25): pump(); time.sleep(.01)
    html = a.output/'page.html'
    html.write_text('''<!doctype html><meta charset="utf-8"><title>TRAMES_FIREFOX_START</title>
<body style="background:#eef2ff"><h1>Trames Firefox offline page</h1><canvas id="c" width="300" height="200"></canvas>
<script>let c=document.getElementById('c').getContext('2d');c.fillStyle='#16a34a';c.fillRect(0,0,300,200);
document.title='TRAMES_FIREFOX_JS_OK';document.body.dataset.test='javascript-and-canvas';</script>''')
    if a.interact:
        with html.open('a') as page:
            page.write('''
<style>textarea{display:block;width:280px;height:44px;border:0;padding:8px;margin:12px 0;font:18px monospace;resize:none;color:black}#a{background:rgb(213,57,169)}#b{background:rgb(38,139,210)}</style>
<textarea id="a" spellcheck="false"></textarea><textarea id="b" spellcheck="false"></textarea>
<script>
const first=document.getElementById('a'), second=document.getElementById('b');
let lastKey='',interacting=false;
function acknowledge(){
 if(!interacting)return;
 const active=document.activeElement;
 document.title='TRAMES_FIREFOX_INPUT:'+JSON.stringify({focus:active.id,a:first.value,b:second.value,cursor:active.selectionStart,key:lastKey,width:innerWidth,height:innerHeight});
}
for(const control of [first,second]){
 control.addEventListener('focus',()=>{interacting=true;acknowledge()});
 for(const name of ['input','select'])control.addEventListener(name,acknowledge);
 control.addEventListener('keyup',event=>{lastKey=event.key;acknowledge()});
}
document.addEventListener('selectionchange',acknowledge);
window.addEventListener('resize',acknowledge);
</script>''')
    guest_page = '/tmp/trames-firefox-'+str(time.monotonic_ns())+'.html'
    guest_screenshot = guest_page+'.png'
    report['guest_page'] = guest_page
    payload('transfer', dict(guest=guest_page, host=str(html.resolve()), export=False, replace=True))
    # Output directories can share a basename (e.g. baseline/run and
    # candidate/run). Never silently reuse the previous browser profile.
    profile = '/root/firefox-probe-'+a.output.name+'-'+str(time.monotonic_ns())
    report['profile'] = profile
    env = ('MOZ_ENABLE_WAYLAND=1 GDK_BACKEND=wayland' if a.backend == 'wayland' else 'MOZ_ENABLE_WAYLAND=0 GDK_BACKEND=x11 DISPLAY=:0')+' XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0'
    if a.disable_content_sandbox: env += ' MOZ_DISABLE_CONTENT_SANDBOX=1'
    if a.disable_wayland_proxy: env += ' MOZ_DISABLE_WAYLAND_PROXY=1'
    if a.avoid_opengl: env += ' MOZ_AVOID_OPENGL_ALTOGETHER=1'
    if a.wayland_debug: env += ' WAYLAND_DEBUG=1'
    if a.moz_log: env += ' MOZ_LOG='+shlex.quote('timestamp,'+a.moz_log)
    if report['cpu_vulkan_reference_diagnostic']:
        # The Linux reference is a real CPU Vulkan device. Original Zink
        # excludes it unless explicitly opted in; never use this on GPU gates.
        env += ' LIBGL_ALWAYS_SOFTWARE=true'
    for name, value in environment.items(): env += ' '+name+'='+shlex.quote(value)
    command = 'firefox --no-remote --profile '+shlex.quote(profile)
    if a.stage == 'headless':
        command += ' --headless --window-size 640,480 --screenshot '+shlex.quote(guest_screenshot)
    command += ' '+shlex.quote('file://'+guest_page)
    if a.stage == 'graphics': command = '/usr/lib/firefox/glxtest'+(' --wayland' if a.backend == 'wayland' else '')
    if a.backend == 'x11':
        ready = '/tmp/firefox-xwayland-ready-'+str(time.monotonic_ns())
        send(env+' trames-xwayland :0 -displayfd 3 3>'+shlex.quote(ready)+' >/tmp/firefox-xwayland.log 2>&1 &\n')
        for _ in range(25): pump(); time.sleep(.01)
        send('while ! test -s '+shlex.quote(ready)+'; do sleep 0.05; done; printf "\\nFIREFOX_XWAYLAND_READY\\n"\n')
        assert wait(b'\r\nFIREFOX_XWAYLAND_READY\r\n', 40), 'Xwayland did not signal display readiness'
        report['xwayland_ready'] = True
        for _ in range(25): pump(); time.sleep(.01)
    prepare = 'mkdir -p '+shlex.quote(profile)
    if preferences:
        user_js = '\n'.join('user_pref('+json.dumps(name)+', '+json.dumps(value)+');'
                            for name, value in preferences.items())
        (a.output/'user.js').write_text(user_js+'\n')
        prepare += "; printf '%s\\n' "+shlex.quote(user_js)+' > '+shlex.quote(profile+'/user.js')
    script = prepare+'; ( '+env+' '+command+' >/tmp/firefox-probe.log 2>&1; r=$?; printf "\\nFIREFOX_PROBE_EXIT:%d\\n" "$r"; ) </dev/null &\n'
    report['command'] = script
    send(script)
    start = time.monotonic()
    focused = set()
    revisions = set()
    captured_at = -15.0
    progress = 0.0
    next_browser_log = a.browser_log_interval
    interaction = Interaction() if a.interact else None
    while time.monotonic()-start < a.timeout:
        status = pump()
        if status['phase'] != 'running':
            report['status'] = status; break
        scene = json.loads(read('wayland_status'))
        if a.browser_log_interval and time.monotonic()-start >= next_browser_log:
            browser_log_snapshot(time.monotonic()-start)
            next_browser_log = time.monotonic()-start+a.browser_log_interval
        if time.monotonic()-start >= progress:
            sample = dict(seconds=time.monotonic()-start,status=status,scene=scene,host_memory=host_memory(),
                          frames=report['frames'],js_title_seen=report['js_title_seen'])
            if interaction: sample['interaction_step'] = interaction.step
            (a.output/'progress.json').write_text(json.dumps(sample,indent=2)+'\n')
            with (a.output/'progress.jsonl').open('a') as log: log.write(json.dumps(sample)+'\n')
            if progress and a.guest_progress:
                send('printf "\\nFIREFOX_PROGRESS\\n"; tail -n 12 /tmp/firefox-probe.log; '+memory_command+'; ps -T -o pid,ppid,stat,time,comm\n')
            progress += 15
        report['windows'] = scene.get('windows', [])
        if report['windows']: report.setdefault('first_window_seconds',time.monotonic()-start)
        for w in report['windows']:
            if w['id'] not in focused:
                payload('wayland_command', dict(op='focus', window=w['id'])); focused.add(w['id'])
            frame = api.trames_session_wayland_frame_acquire(h, w['id'])
            if frame:
                presented = False
                try:
                    info = Info()
                    assert api.trames_wayland_frame_info(frame, C.byref(info)) == 0
                    presented = info.images > 0
                    identity = (w['id'],info.revision)
                    if presented and identity not in revisions:
                        revisions.add(identity)
                        report['frames'] += 1
                        report.setdefault('first_image_seconds',time.monotonic()-start)
                        report['last_frame'] = dict(width=info.width, height=info.height, images=info.images, revision=info.revision)
                        kinds = set()
                        for index in range(info.images):
                            image = Image()
                            assert api.trames_wayland_frame_image(frame, index, C.byref(image)) == 0
                            kinds.add(image.kind)
                        report['image_kinds'] = sorted(set(report.get('image_kinds', [])) | kinds)
                        if 2 in kinds and 'TRAMES_FIREFOX_JS_OK' in w.get('title', ''):
                            report['js_window_gpu_frame_seen'] = True
                            report.setdefault('first_js_gpu_frame_seconds',time.monotonic()-start)
                        if capture_pixels and (time.monotonic()-start-captured_at >= 15
                                             or 'TRAMES_FIREFOX_JS_OK' in w.get('title','')):
                            green = capture(frame,info)
                            captured_at = time.monotonic()-start
                            report['canvas_green_pixels'] = green
                            report['canvas_green_seen'] = green is not None and green >= 45000
                finally:
                    assert api.trames_wayland_frame_release(frame, int(presented), int(time.monotonic()*1000)&0xffffffff) == 0
            if 'TRAMES_FIREFOX_JS_OK' in w.get('title',''):
                report['js_title_seen'] = True
                report.setdefault('js_title_seconds',time.monotonic()-start)
        if interaction:
            interaction.poll(scene)
            if interaction.step in ('done', 'failed'): break
        elif (a.stage == 'wayland' and report['js_title_seen'] and report['frames']
              and (not a.require_gpu_frame or report['js_window_gpu_frame_seen'])
              and (not capture_pixels or report.get('canvas_green_seen'))): break
        if re.search(rb'\r?\nFIREFOX_PROBE_EXIT:\d+\r?\n',output) and (not interaction or interaction.step != 'close'): break
        time.sleep(.01)
    report['launch_seconds'] = time.monotonic()-start
    match = re.search(rb'\r?\nFIREFOX_PROBE_EXIT:(\d+)\r?\n',output)
    report['firefox_exit'] = int(match[1]) if match else None
    report['host_memory'] = host_memory()
    send('printf "\\nFIREFOX_LOG_BEGIN\\n"; cat /tmp/firefox-probe.log; printf "\\nFIREFOX_DMESG_BEGIN\\n"; dmesg; printf "\\nFIREFOX_MEMORY_BEGIN\\n"; '+memory_command+'; printf "\\nFIREFOX_PROCESSES_BEGIN\\n"; ps -T -o pid,ppid,stat,time,comm; printf "\\nFIREFOX_DIAGNOSTICS_DONE\\n"\n')
    report['diagnostics_complete'] = wait(b'\r\nFIREFOX_DIAGNOSTICS_DONE\r\n',a.diagnostic_timeout)
    if a.stage == 'headless' and report['firefox_exit'] == 0:
        screenshot = a.output/'headless.png'
        payload('transfer', dict(guest=guest_screenshot, host=str(screenshot.resolve()), export=True))
        deadline = time.monotonic()+10
        while not screenshot.exists() and time.monotonic()<deadline: pump(); time.sleep(.01)
        report['screenshot_exported'] = screenshot.exists()
        if screenshot.exists() and a.capture_shm:
            with PillowImage.open(screenshot) as image:
                assert 0 < image.width <= 4096 and 0 < image.height <= 4096
                report['canvas_green_pixels'] = green_pixels(image)
                report['canvas_green_seen'] = report['canvas_green_pixels'] >= 45000
    if a.stage == 'graphics':
        log = bytes(output).split(b'\r\nFIREFOX_LOG_BEGIN\r\n')[-1].split(b'\r\nFIREFOX_DMESG_BEGIN\r\n')[0]
        for key in ('RENDERER', 'VENDOR', 'VERSION'):
            match = re.search(rb'(?:^|\r?\n)'+key.encode()+rb'\r?\n([^\r\n]+)', log)
            if match: report['graphics_'+key.lower()] = match[1].decode(errors='replace')
        report['passed'] = bool(report['firefox_exit'] == 0 and report.get('graphics_renderer') and not re.search(rb'(?:^|\r?\n)ERROR\r?\n', log))
    else:
        report['passed'] = (report['firefox_exit'] == 0 and report.get('screenshot_exported',False) and (not a.capture_shm or report.get('canvas_green_seen'))) if a.stage=='headless' else bool(report['frames'] and report['js_title_seen'] and (not capture_pixels or report.get('canvas_green_seen')))
        if a.interact: report['passed'] = report['passed'] and report.get('interaction_passed', False)
        if a.require_gpu_frame: report['passed'] = report['passed'] and report['js_window_gpu_frame_seen']
finally:
    report['seconds'] = time.monotonic()-began
    (a.output/'guest.log').write_bytes(output)
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    api.trames_session_close(h)
    if sampler: sampler.trames_sample_stop()
print(json.dumps({k:report.get(k) for k in ['stage','sandbox','passed','firefox_exit','launch_seconds','frames','windows']},indent=2))
sys.exit(0 if report.get('passed') else 1)
