#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original ARM64 Mesa EGL windows through production session/compositor/WSI."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--store', type=Path, help='installed signed Mesa plus wayland-libs-egl store; copied; otherwise install original APKs')
p.add_argument('--in-place-store', action='store_true', help='use a disposable supplied store directly; no concurrent sessions')
p.add_argument('--relay', action='store_true', help='forward Wayland bytes/SCM_RIGHTS through a guest socket pair, like Firefox')
p.add_argument('--clear-only', action='store_true', help='only new in-pass attachment clear pixels; reuse passed relayed window and sampling checks')
p.add_argument('--damage-only', action='store_true', help='one relayed EGL damage-swap lifecycle; reuse passed descriptor, sampling, copy and second-launch checks')
p.add_argument('--budget-only', action='store_true', help='native heap quota and 128 MiB of simultaneous coherent mappings; reuse passed rendering checks')
p.add_argument('--snapshot-store', type=Path, help='save original installed inputs before session mutations')
p.add_argument('--metal-validator', type=Path, help='native test-only exported-MTLTexture pixel oracle')
p.add_argument('--egl-library', type=Path, help='Mesa EGL platform adapter; Gallium/Zink and loader remain original')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--ffi', type=Path, default=ROOT / 'target/release' / ('libtrames_ffi.dylib' if sys.platform == 'darwin' else 'libtrames_ffi.so'))
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
if a.in_place_store and not a.store: p.error('--in-place-store requires --store')
if a.damage_only and a.clear_only: p.error('--damage-only and --clear-only are separate checks')
if a.budget_only and (a.damage_only or a.clear_only): p.error('--budget-only is a separate check')
store = a.store if a.in_place_store else a.output / 'store'
if a.store and not a.in_place_store:
    shutil.copytree(a.store, store)
elif not a.store:
    runner = Path(os.environ.get('TRAMES_RUNNER', ROOT / 'target/release/trames-run'))
    rootfs = Path(os.environ.get('TRAMES_ALPINE_CACHE', ROOT / 'target/alpine-download')) / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
    repo = ROOT / 'target/zink-inputs/repository'
    imports = [v for f in sorted(repo.rglob('*')) if f.is_file() for v in ('--file', '/tmp/zink-repo/' + str(f.relative_to(repo)) + '=' + str(f))]
    command = [str(runner), '--storage', str(store), '--network', 'off', '--cpus', '4', '--memory-budget', '4GiB', '--process-memory-budget', '4GiB', '--max-instructions', '50000000000', '--rootfs', str(rootfs), *imports,
               '--guest', '/bin/sh', '-ec', 'printf "/tmp/zink-repo/main\\n/tmp/zink-repo/community\\n" >/tmp/zink.repositories; apk --repositories-file /tmp/zink.repositories --no-progress add mesa-dri-gallium mesa-egl mesa-gles vulkan-loader wayland-libs-egl']
    r = subprocess.run(command, capture_output=True, timeout=300)
    (a.output / 'install.stdout').write_bytes(r.stdout)
    (a.output / 'install.stderr').write_bytes(r.stderr)
    assert r.returncode == 0, (r.returncode, r.stdout[-2000:], r.stderr[-4000:])
input_manifest = hashlib.sha256((store / 'manifest').read_bytes()).hexdigest()
if a.snapshot_store:
    shutil.copytree(store, a.snapshot_store)
metal = None
if sys.platform == 'darwin':
    assert a.metal_validator, 'Apple acceptance requires native exported-texture pixel verification'
    metal = C.CDLL(str(a.metal_validator.resolve()))
    metal.trames_zink_check_texture.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32]
api = C.CDLL(str(a.ffi.resolve()))
api.trames_session_open.argtypes = [C.c_char_p, C.c_uint32]
api.trames_session_open.restype = C.c_uint64
for name in ('input', 'transfer', 'wayland_command'):
    getattr(api, 'trames_session_' + name).argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
for name in ('status', 'output', 'wayland_status'):
    getattr(api, 'trames_session_' + name).argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_close.argtypes = [C.c_uint64]
api.trames_last_error.argtypes = [C.c_void_p, C.c_uint32]
api.trames_session_wayland_frame_acquire.argtypes = [C.c_uint64, C.c_uint64]
api.trames_session_wayland_frame_acquire.restype = C.c_uint64
api.trames_wayland_frame_release.argtypes = [C.c_uint64, C.c_uint32, C.c_uint32]

class Info(C.Structure):
    _fields_ = [('width', C.c_uint32), ('height', C.c_uint32), ('images', C.c_uint32), ('reserved', C.c_uint32), ('revision', C.c_uint64)]

class Image(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in ('kind', 'address', 'length', 'offset')] + [(n, C.c_uint32) for n in ('width', 'height', 'stride', 'alpha')] + [(n, C.c_int32) for n in ('x', 'y', 'scale')] + [('transform', C.c_uint32)]

api.trames_wayland_frame_info.argtypes = [C.c_uint64, C.POINTER(Info)]
api.trames_wayland_frame_image.argtypes = [C.c_uint64, C.c_uint32, C.POINTER(Image)]
options = dict(store=str(store.resolve()), volume_total=16 << 30, volume_available=16 << 30,
               memory_budget_bytes=4 << 30, process_memory_budget_bytes=4 << 30,
               network='off', wayland=True, vulkan_accelerator=True,
               vulkan_icd=str(ROOT / 'target/vulkan-guest/libtrames_vulkan_icd.so'))
data = json.dumps(options).encode()
session = api.trames_session_open(data, len(data))
assert session
text = ''
held = []
report = dict(source='original signed Mesa25.1.9 EGL/GLES2 and libwayland; production FFI and native Vulkan WSI',
              icd_sha256=hashlib.sha256((ROOT / 'target/vulkan-guest/libtrames_vulkan_icd.so').read_bytes()).hexdigest(),
              input_store_manifest_sha256=input_manifest,
              rounds=[], passed=False, physical_ipad=False)
report['wayland_relay'] = a.relay or a.damage_only
report['attachment_clear_only'] = a.clear_only
report['damage_swap_only'] = a.damage_only
report['gpu_budget_only'] = a.budget_only
bundled_egl = ROOT / 'target/vulkan-guest/libEGL.so.1'
if bundled_egl.is_file():
    report['source'] = 'Mesa25.1.9 EGL native-WSI platform adapter; original signed Gallium/Zink/GLES2, libwayland and Vulkan loader; production FFI'
    report['egl_adapter_sha256'] = hashlib.sha256(bundled_egl.read_bytes()).hexdigest()
    report['automatic_session_egl_setup'] = True

def payload(method, value):
    data = json.dumps(value).encode()
    result = getattr(api, 'trames_session_' + method)(session, data, len(data))
    if result < 0:
        error = C.create_string_buffer(4096)
        api.trames_last_error(error, len(error))
        raise AssertionError((method, value, error.value.decode(errors='replace')))

def read(method):
    data = C.create_string_buffer(65536)
    n = getattr(api, 'trames_session_' + method)(session, data, len(data))
    assert n >= 0, (method, n)
    return data.raw[:n]

def send(value):
    data = value.encode()
    assert api.trames_session_input(session, data, len(data)) == len(data)

def command(op, window, **values):
    payload('wayland_command', dict(op=op, window=window, **values))

def release(frame, presented=True):
    assert api.trames_wayland_frame_release(frame, int(presented), int(time.monotonic() * 1000) & 0xffffffff) == 0

try:
    start = time.monotonic()
    while json.loads(read('status'))['phase'] == 'starting':
        assert time.monotonic() - start < 60
        time.sleep(.01)
    state = json.loads(read('status'))
    assert state['phase'] == 'running', state
    if a.budget_only:
        payload('transfer', dict(guest='/tmp/vulkan-budget', host=str(ROOT/'target/zink-inputs/vulkan-budget'), export=False, replace=True))
        send('chmod +x /tmp/vulkan-budget; VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json /tmp/vulkan-budget 536870912; printf "\\nZINK_BUDGET_RESULT:%d\\n" $?\n')
        start = time.monotonic()
        while '\r\nZINK_BUDGET_RESULT:0\r\n' not in text:
            text += read('output').decode(errors='replace')
            assert not any('\r\nZINK_BUDGET_RESULT:'+str(n)+'\r\n' in text for n in (1,127,134,139)), text[-12000:]
            assert time.monotonic()-start < 90, text[-12000:]
            time.sleep(.003)
        assert 'vulkan-budget-ok: two rounds, 8 simultaneous 16 MiB coherent mappings' in text
        report['gpu_budget_bytes'] = 512<<20
        report['coherent_mapping_bytes'] = 128<<20
        report['passed'] = True
        print('zink-budget-ok: reported session quota, native allocation/map/coherence and release/reallocate', flush=True)
        raise SystemExit(0)
    if a.clear_only:
        payload('transfer', dict(guest='/tmp/vulkan-render', host=str(ROOT/'target/zink-inputs/vulkan-render'), export=False, replace=True))
        for name in ('vulkan-render.vert.spv', 'vulkan-render.frag.spv'):
            shader = ROOT/'target/zink-native-bundle/shaders'/name
            if not shader.is_file(): shader = ROOT/'tests/fixtures/bin'/name
            payload('transfer', dict(guest='/tmp/'+name, host=str(shader), export=False, replace=True))
        send('chmod +x /tmp/vulkan-render; /tmp/vulkan-render /tmp/vulkan-render.vert.spv /tmp/vulkan-render.frag.spv /tmp/clear.ppm clear-attachments; printf "\\nZINK_CLEAR_RESULT:%d\\n" $?\n')
        clear_start = time.monotonic()
        while '\r\nZINK_CLEAR_RESULT:0\r\n' not in text:
            text += read('output').decode(errors='replace')
            assert not any('\r\nZINK_CLEAR_RESULT:'+str(n)+'\r\n' in text for n in (1,127,134,139)), text[-12000:]
            assert time.monotonic()-clear_start < 60, text[-12000:]
            time.sleep(.003)
        assert 'vulkan-attachments-clear-ok:' in text
        report['attachment_clear_gpu'] = report['passed'] = True
        print('zink-attachment-clear-ok: native in-pass rectangles, color/alpha and scissor independence', flush=True)
        raise SystemExit(0)
    for name in ('zink-wayland', 'vulkan-copy'):
        payload('transfer', dict(guest='/tmp/' + name, host=str(ROOT / 'target/zink-inputs' / name), export=False, replace=True))
    if a.relay and not a.damage_only:
        payload('transfer', dict(guest='/tmp/vulkan-resources', host=str(ROOT/'target/zink-inputs/vulkan-resources'), export=False, replace=True))
        shader = ROOT/'target/zink-native-bundle/shaders/vulkan-resources.spv'
        if not shader.is_file(): shader = ROOT/'tests/fixtures/bin/vulkan-resources.spv'
        payload('transfer', dict(guest='/tmp/vulkan-resources.spv', host=str(shader), export=False, replace=True))
        send('chmod +x /tmp/vulkan-resources; /tmp/vulkan-resources /tmp/vulkan-resources.spv batch; printf "\\nZINK_BATCH_RESULT:%d\\n" $?\n')
        batch_start = time.monotonic()
        while '\r\nZINK_BATCH_RESULT:0\r\n' not in text:
            text += read('output').decode(errors='replace')
            assert not any('\r\nZINK_BATCH_RESULT:'+str(n)+'\r\n' in text for n in (1,127,134,139)), text[-12000:]
            assert time.monotonic()-batch_start < 60, text[-12000:]
            time.sleep(.003)
        assert 'vulkan-descriptor-batch-ok: 100 sets' in text
        report['descriptor_batch_gpu'] = True
        payload('transfer', dict(guest='/tmp/vulkan-render', host=str(ROOT/'target/zink-inputs/vulkan-render'), export=False, replace=True))
        for name in ('vulkan-render.vert.spv', 'vulkan-render.frag.spv'):
            shader = ROOT/'target/zink-native-bundle/shaders'/name
            if not shader.is_file(): shader = ROOT/'tests/fixtures/bin'/name
            payload('transfer', dict(guest='/tmp/'+name, host=str(shader), export=False, replace=True))
        for mask in ('one', 'zero', 'upper', 'rate'):
            marker = '\r\nZINK_MASK_'+mask+':0\r\n'
            mode = 'sample-rate' if mask == 'rate' else 'sample-mask-'+mask
            send('chmod +x /tmp/vulkan-render; /tmp/vulkan-render /tmp/vulkan-render.vert.spv /tmp/vulkan-render.frag.spv /tmp/mask.ppm '+mode+'; printf "\\nZINK_MASK_'+mask+':%d\\n" $?\n')
            mask_start = time.monotonic()
            while marker not in text:
                text += read('output').decode(errors='replace')
                assert not any('\r\nZINK_MASK_'+mask+':'+str(n)+'\r\n' in text for n in (1,127,134,139)), text[-12000:]
                assert time.monotonic()-mask_start < 60, text[-12000:]
                time.sleep(.003)
        report['single_sample_mask_gpu'] = ['one', 'zero', 'upper']
        report['sample_rate_shading_gpu'] = True
    if a.egl_library:
        payload('transfer', dict(guest='/usr/local/lib/libEGL.so.1', host=str(a.egl_library.resolve()), export=False, replace=True))
        report['egl_adapter_sha256'] = hashlib.sha256(a.egl_library.read_bytes()).hexdigest()
    if not a.damage_only:
        send('chmod +x /tmp/zink-wayland /tmp/vulkan-copy; VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json /tmp/vulkan-copy; printf "\\nZINK_COPY_RESULT:%d\\n" $?\n')
        start = time.monotonic()
        while '\r\nZINK_COPY_RESULT:0\r\n' not in text:
            text += read('output').decode(errors='replace')
            assert not any('\r\nZINK_COPY_RESULT:' + str(n) + '\r\n' in text for n in (1, 127, 134, 139)), text[-12000:]
            assert time.monotonic() - start < 60, text[-12000:]
            time.sleep(.003)
        assert 'vulkan-copy-ok: 64 GPU pixels' in text
        report['native_image_copy_gpu_pixels'] = 64
    for round_no in range(1 if a.damage_only else 2):
        base = len(text)
        # Mesa25.1.9's GL-thread ancillary-discard internal dispatch is tagged
        # ES3 in upstream gl_API.xml. Its ES2-only context reports INVALID_OPERATION
        # after SwapBuffers. This upstream option uses direct GL dispatch; Zink
        # and Vulkan/Metal rendering, threads and presentation remain original.
        environment = 'EGL_PLATFORM=wayland'
        if a.relay or a.damage_only: environment += ' TRAMES_TEST_WAYLAND_RELAY=1'
        if not bundled_egl.is_file():
            environment += ' GALLIUM_DRIVER=zink MESA_LOADER_DRIVER_OVERRIDE=zink mesa_glthread=false VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json'
        if a.egl_library:
            environment += ' LIBGL_KOPPER_WAYLAND_NO_DRM=true'
        if os.environ.get('TRAMES_ZINK_CPU_REFERENCE') == '1':
            environment += ' LIBGL_ALWAYS_SOFTWARE=true'
        if os.environ.get('TRAMES_VULKAN_TRACE'):
            environment += ' TRAMES_VULKAN_TRACE=1 WAYLAND_DEBUG=1 MESA_DEBUG=context'
        args = ' 10000 damage' if a.damage_only else ''
        send('chmod +x /tmp/zink-wayland; ' + environment + ' /tmp/zink-wayland'+args+'; printf "\\nZINK_WINDOW_RESULT:%d\\n" $?\n')
        start = time.monotonic()
        phase = 0
        ids = []
        seen = set()
        revisions = set()
        metal_verified = set()
        frames = 0
        while '\r\nZINK_WINDOW_RESULT:0\r\n' not in text[base:]:
            text += read('output').decode(errors='replace')
            if any('\r\nZINK_WINDOW_RESULT:' + str(n) + '\r\n' in text[base:] for n in (1, 127, 134, 139)):
                send('dmesg; printf "\\nZINK_DMESG_DONE\\n"\n')
                deadline = time.monotonic() + 5
                while '\r\nZINK_DMESG_DONE\r\n' not in text[base:] and time.monotonic() < deadline:
                    text += read('output').decode(errors='replace')
                    time.sleep(.01)
                raise AssertionError(text[base:][-12000:])
            windows = json.loads(read('wayland_status'))['windows']
            live = {w['id']: w for w in windows if w['app_id'] == 'trames-zink-wayland-test'}
            for window, w in live.items():
                frame = api.trames_session_wayland_frame_acquire(session, window)
                if frame:
                    info, image = Info(), Image()
                    assert api.trames_wayland_frame_info(frame, C.byref(info)) == 0
                    assert info.images == 1
                    assert api.trames_wayland_frame_image(frame, 0, C.byref(image)) == 0
                    assert image.kind == 2, 'EGL window fell back to CPU SHM'
                    if sys.platform == 'darwin':
                        assert image.address, 'missing retained Metal texture'
                        key = (window, image.width, image.height)
                        if key not in metal_verified:
                            blue = int(w['title'].endswith('2'))
                            assert metal.trames_zink_check_texture(image.address, image.width, image.height, blue) == 0, (key, 'exported Metal image pixels incorrect')
                            metal_verified.add(key)
                    assert (info.width, info.height) == (image.width, image.height)
                    seen.add((window, image.width, image.height))
                    frames += 1
                    revisions.add((window, info.revision))
                    if not ids and len(live) == 2:
                        ids = sorted(live, key=lambda i: live[i]['title'])
                    if phase == 0 and not held:
                        held.append((frame, image.width, image.height, int(w['title'].endswith('2'))))
                    else:
                        release(frame)
            if phase == 0 and ids and all(any(v[0] == i for v in seen) for i in ids):
                command('resize', ids[0], width=160, height=96)
                command('resize', ids[1], width=128, height=72)
                phase = 1
            elif phase == 1 and (ids[0], 160, 96) in seen and (ids[1], 128, 72) in seen:
                command('mode', ids[0], mode='minimized')
                command('mode', ids[0], mode='floating')
                command('close', ids[0])
                phase = 2
            elif phase == 2 and ids[0] not in live and ids[1] in live:
                command('resize', ids[1], width=144, height=80)
                phase = 3
            elif phase == 3 and (ids[1], 144, 80) in seen:
                command('close', ids[1])
                phase = 4
            assert time.monotonic() - start < 180, (phase, windows, text[base:][-12000:])
            time.sleep(.003)
        assert phase == 4 and frames >= 5
        assert 'zink-wayland-ok:' in text[base:]
        if a.damage_only: assert 'zink-wayland-damage: two present-region hints per swap' in text[base:]
        for frame, width, height, blue in held:
            info = Info()
            assert api.trames_wayland_frame_info(frame, C.byref(info)) == 0
            assert (info.width, info.height) == (width, height)
            if metal:
                image = Image()
                assert api.trames_wayland_frame_image(frame, 0, C.byref(image)) == 0
                assert metal.trames_zink_check_texture(image.address, width, height, blue) == 0
            release(frame, False)
        held.clear()
        report['rounds'].append(dict(frame_acquisitions=frames, unique_scene_revisions=len(revisions), sizes=sorted(seen), metal_texture_sizes_verified=sorted(metal_verified), resize=True, close=True, retained_old_frame=True))
    assert not json.loads(read('wayland_status'))['windows']
    report['passed'] = True
    print('zink-wayland-integration-ok: two windows, '+('damage swaps, one launch' if a.damage_only else 'two launches')+', native GPU frames, resize, close and retained old frame', flush=True)
finally:
    for frame, _, _, _ in held:
        release(frame, False)
    api.trames_session_close(session)
    (a.output / 'guest.log').write_text(text.replace('\r\n', '\n').replace('\r', '\n'))
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
