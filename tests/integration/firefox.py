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
p.add_argument('--stage', choices=['headless', 'wayland'], required=True)
p.add_argument('--ffi', type=Path, default=ROOT/'target/perf-runtime/release/libtrames_ffi.so')
p.add_argument('--musl', type=Path, default=ROOT/'target/musl-hybrid/libc.so')
p.add_argument('--timeout', type=float, default=180)
p.add_argument('--diagnostic-timeout', type=float, default=15)
p.add_argument('--guest-progress', action='store_true', help='intrusive diagnosis: periodic guest ps/proc commands; disabled for startup gates')
p.add_argument('--memory-gib', type=int, default=4, help='aggregate guest memory budget; overrides are diagnostic')
p.add_argument('--process-memory-gib', type=int, default=4)
p.add_argument('--gpu', action='store_true')
p.add_argument('--disable-content-sandbox', action='store_true')
p.add_argument('--disable-wayland-proxy', action='store_true', help='diagnosis only: bypass the known FIONREAD blocker')
p.add_argument('--avoid-opengl', action='store_true', help='diagnosis only: skip Firefox graphics probing')
p.add_argument('--wayland-debug', action='store_true')
p.add_argument('--moz-log', help='diagnosis only: original Firefox MOZ_LOG modules, e.g. Process:5,nsDocShell:5,DocumentChannel:5')
p.add_argument('--browser-log-interval', type=float, default=0, help='diagnosis only: export the latest browser log without executing guest ps/cat; 0 disables')
p.add_argument('--capture-shm', action='store_true', help='save software-composited PNG and require the offline canvas green rectangle; requires Pillow')
p.add_argument('--sampler', type=Path, help='optional Linux host PC sampling; not an uninstrumented timing run')
a = p.parse_args()
if a.browser_log_interval and a.browser_log_interval < 5:
    p.error('--browser-log-interval must be 0 or at least 5 seconds')
if a.capture_shm:
    from PIL import Image as PillowImage, ImageChops
a.output.mkdir(parents=True, exist_ok=False)
if hasattr(os, 'sched_setaffinity'):
    os.sched_setaffinity(0, sorted(os.sched_getaffinity(0))[:4])

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

def capture(frame, info):
    """Read only retained SHM capabilities; native Metal handles are opaque."""
    if not (0 < info.width <= 4096 and 0 < info.height <= 4096): return None
    canvas = PillowImage.new('RGBA', (info.width, info.height), (0,0,0,255))
    for i in range(info.images):
        image = Image()
        assert api.trames_wayland_frame_image(frame, i, C.byref(image)) == 0
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
    channels = canvas.convert('RGB').split()
    matches = [channel.point([255 if i == expected else 0 for i in range(256)])
               for channel, expected in zip(channels, (22,163,74))]
    green = ImageChops.multiply(ImageChops.multiply(matches[0],matches[1]),matches[2])
    return green.histogram()[255]

options = dict(store=str(a.store.resolve()), volume_total=16<<30, volume_available=16<<30,
    network='off', wayland=True, pure_accelerator=True, musl_libc=str(a.musl.resolve()),
    memory_budget_bytes=a.memory_gib<<30, process_memory_budget_bytes=a.process_memory_gib<<30,
    vulkan_accelerator=a.gpu)
if a.gpu:
    options['vulkan_icd'] = str(ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so')
report = dict(stage=a.stage, sandbox='content-disabled-diagnostic' if a.disable_content_sandbox else 'default',
    wayland_proxy='disabled-diagnostic' if a.disable_wayland_proxy else 'default',
    avoid_opengl_diagnostic=a.avoid_opengl,
    physical_ipad=False, options=options, frames=0, windows=[], js_title_seen=False,
    guest_progress_diagnostic=a.guest_progress,
    moz_log_diagnostic=a.moz_log,
    browser_log_interval_diagnostic=a.browser_log_interval,
    capture_shm=a.capture_shm,
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
    html = a.output/'page.html'
    html.write_text('''<!doctype html><meta charset="utf-8"><title>TRAMES_FIREFOX_START</title>
<body style="background:#eef2ff"><h1>Trames Firefox offline page</h1><canvas id="c" width="300" height="200"></canvas>
<script>let c=document.getElementById('c').getContext('2d');c.fillStyle='#16a34a';c.fillRect(0,0,300,200);
document.title='TRAMES_FIREFOX_JS_OK';document.body.dataset.test='javascript-and-canvas';</script>''')
    guest_page = '/tmp/trames-firefox-'+str(time.monotonic_ns())+'.html'
    guest_screenshot = guest_page+'.png'
    report['guest_page'] = guest_page
    payload('transfer', dict(guest=guest_page, host=str(html.resolve()), export=False, replace=True))
    # Output directories can share a basename (e.g. baseline/run and
    # candidate/run). Never silently reuse the previous browser profile.
    profile = '/root/firefox-probe-'+a.output.name+'-'+str(time.monotonic_ns())
    report['profile'] = profile
    env = 'MOZ_ENABLE_WAYLAND=1 GDK_BACKEND=wayland XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-0'
    if a.disable_content_sandbox: env += ' MOZ_DISABLE_CONTENT_SANDBOX=1'
    if a.disable_wayland_proxy: env += ' MOZ_DISABLE_WAYLAND_PROXY=1'
    if a.avoid_opengl: env += ' MOZ_AVOID_OPENGL_ALTOGETHER=1'
    if a.wayland_debug: env += ' WAYLAND_DEBUG=1'
    if a.moz_log: env += ' MOZ_LOG='+shlex.quote('timestamp,'+a.moz_log)
    command = 'firefox --no-remote --profile '+shlex.quote(profile)
    if a.stage == 'headless':
        command += ' --headless --window-size 640,480 --screenshot '+shlex.quote(guest_screenshot)
    command += ' '+shlex.quote('file://'+guest_page)
    script = 'mkdir -p '+shlex.quote(profile)+'; ( '+env+' '+command+' >/tmp/firefox-probe.log 2>&1; r=$?; printf "\\nFIREFOX_PROBE_EXIT:%d\\n" "$r"; ) </dev/null &\n'
    report['command'] = script
    send(script)
    start = time.monotonic()
    focused = set()
    revisions = set()
    captured_at = -15.0
    progress = 0.0
    next_browser_log = a.browser_log_interval
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
                        if a.capture_shm and (time.monotonic()-start-captured_at >= 15
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
        if (a.stage == 'wayland' and report['js_title_seen'] and report['frames']
            and (not a.capture_shm or report.get('canvas_green_seen'))): break
        if re.search(rb'\r?\nFIREFOX_PROBE_EXIT:\d+\r?\n',output): break
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
    report['passed'] = (report['firefox_exit'] == 0 and report.get('screenshot_exported',False)) if a.stage=='headless' else bool(report['frames'] and report['js_title_seen'] and (not a.capture_shm or report.get('canvas_green_seen')))
finally:
    report['seconds'] = time.monotonic()-began
    (a.output/'guest.log').write_bytes(output)
    (a.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    api.trames_session_close(h)
    if sampler: sampler.trames_sample_stop()
print(json.dumps({k:report.get(k) for k in ['stage','sandbox','passed','firefox_exit','launch_seconds','frames','windows']},indent=2))
sys.exit(0 if report.get('passed') else 1)
