#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate Zink prerequisites without claiming that Mesa already renders GL.

Uses original signed Mesa and Vulkan loader, ordinary Vulkan/EGL clients, and a
real native Vulkan driver. No Mesa patch or software OpenGL substitution. Mesa
EGL output is retained as a compatibility probe; --require-render makes it a gate.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--store', type=Path, help='copy an already installed original Mesa store')
p.add_argument('--output', type=Path, default=ROOT / 'target/zink-foundation')
p.add_argument('--require-render', action='store_true')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
inputs = ROOT / 'target/zink-inputs'
fixtures = ROOT / 'tests/fixtures/bin'
runner = Path(os.environ.get('TRAMES_RUNNER', ROOT / 'target/release/trames-run')).resolve()
icd = ROOT / 'target/vulkan-guest/libtrames_vulkan_icd.so'
rootfs = Path(os.environ.get('TRAMES_ALPINE_CACHE', ROOT / 'target/alpine-download')) / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
report = dict(runtime_source='native Vulkan and guest ICD maintenance1/timeline/driver-properties; CPU/MMU/UI unchanged',
              runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
              icd_sha256=hashlib.sha256(icd.read_bytes()).hexdigest(),
              native_driver_scope='configured host Vulkan driver, local reference may be software',
              rendering_passed=False, cases={})


def execute(label, command, success=True, native=False):
    r = subprocess.run(command, capture_output=True, timeout=300)
    (a.output / (label + '.stdout')).write_bytes(r.stdout)
    (a.output / (label + '.stderr')).write_bytes(r.stderr)
    report['cases'][label] = dict(exit_code=r.returncode, scope='native oracle' if native else 'original guest client')
    if success:
        assert r.returncode == 0, (label, r.stdout[-2000:], r.stderr[-4000:])
    return r


try:
    with tempfile.TemporaryDirectory(prefix='trames-zink-') as tmp:
        store = Path(tmp) / 'store'
        if a.store:
            shutil.copytree(a.store, store)
        base = [str(runner), '--storage', str(store), '--network', 'off', '--cpus', '4',
                '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
                '--max-instructions', '50000000000']
        if not a.store:
            repo = inputs / 'repository'
            imports = [v for f in sorted(repo.rglob('*')) if f.is_file()
                       for v in ('--file', '/tmp/zink-repo/' + str(f.relative_to(repo)) + '=' + str(f))]
            execute('install', [*base, '--rootfs', str(rootfs), *imports, '--guest', '/bin/sh', '-ec',
                'printf "/tmp/zink-repo/main\\n/tmp/zink-repo/community\\n" >/tmp/zink.repositories; '
                'apk --repositories-file /tmp/zink.repositories --no-progress add mesa-dri-gallium mesa-egl mesa-gles vulkan-loader'])
        report['input_store_manifest_sha256'] = hashlib.sha256((store / 'manifest').read_bytes()).hexdigest()

        def guest(label, program, source, arguments=(), files=(), exports=(), egl=False):
            imported = [('/usr/lib/libtrames_vulkan_icd.so', icd),
                        ('/usr/share/vulkan/icd.d/trames.json', ROOT / 'target/vulkan-guest/trames.json'),
                        *files]
            execute(label + '-cleanup', [*base, '--guest', '/bin/rm', '-f', program, *[g for g, _ in imported]])
            command = [*base, '--vulkan', '--env', 'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json',
                       *[v for g, f in imported for v in ('--file', g + '=' + str(f))],
                       '--exec-file', program + '=' + str(source)]
            for g, f in exports:
                command += ['--export', g + '=' + str(f)]
            if egl:
                for value in ('GALLIUM_DRIVER=zink', 'MESA_LOADER_DRIVER_OVERRIDE=zink', 'EGL_PLATFORM=surfaceless'):
                    command += ['--env', value]
                # Lavapipe is a CPU physical device; original Zink excludes it
                # without this standard reference-only opt-in. Physical GPU
                # acceptance must run without this environment variable.
                if os.environ.get('TRAMES_ZINK_CPU_REFERENCE') == '1':
                    command += ['--env', 'LIBGL_ALWAYS_SOFTWARE=true']
            return execute(label, [*command, '--guest', program, *arguments], success=not egl)

        native = execute('queries-native', [str(inputs / 'vulkan-queries-host')], native=True)
        queries = guest('queries', '/tmp/queries', inputs / 'vulkan-queries', ('bridge',))
        assert b'vulkan-queries-ok' in queries.stdout and b'vulkan-queries-ok' in native.stdout
        report['missing_zink_hard_requirements'] = [line.split()[1] for line in queries.stdout.decode().splitlines()
                                                     if line.startswith('zink-required ') and line.endswith(' missing')]
        native = execute('timeline-native', [str(inputs / 'vulkan-timeline-host')], native=True)
        timeline = guest('timeline', '/tmp/timeline', inputs / 'vulkan-timeline', ('bridge',))
        assert native.stdout == timeline.stdout
        base[base.index('--cpus') + 1] = '1'
        single = guest('timeline-single-cpu', '/tmp/timeline-single', inputs / 'vulkan-timeline', ('bridge',))
        assert native.stdout == single.stdout
        base[base.index('--cpus') + 1] = '4'
        for mode in ('legacy', 'negative', 'negative-static'):
            args = [] if mode == 'legacy' else [mode]
            host_image = a.output / ('render-' + mode + '-native.ppm')
            native = execute('render-' + mode + '-native', [str(fixtures / 'vulkan-render-host'), str(fixtures / 'vulkan-render.vert.spv'), str(fixtures / 'vulkan-render.frag.spv'), str(host_image), *args], native=True)
            guest_image = a.output / ('render-' + mode + '-guest.ppm')
            rendered = guest('render-' + mode, '/tmp/render', fixtures / 'vulkan-render', ('/tmp/render.vert.spv', '/tmp/render.frag.spv', '/tmp/render.ppm', *args),
                             tuple(('/tmp/render.' + stage + '.spv', fixtures / ('vulkan-render.' + stage + '.spv')) for stage in ('vert', 'frag')),
                             (('/tmp/render.ppm', guest_image),))
            assert native.stdout == rendered.stdout and host_image.read_bytes() == guest_image.read_bytes()
        for mode in ('legacy', 'template'):
            args = ['template'] if mode == 'template' else []
            native = execute('compute-' + mode + '-native', [str(fixtures / 'vulkan-compute-host'), str(fixtures / 'vulkan-compute.spv'), *args], native=True)
            guest_result = guest('compute-' + mode, '/tmp/compute', fixtures / 'vulkan-compute', ('/tmp/compute.spv', *args), (('/tmp/compute.spv', fixtures / 'vulkan-compute.spv'),))
            assert native.stdout == guest_result.stdout
            host_image = a.output / ('texture-' + mode + '-native.ppm')
            native = execute('texture-' + mode + '-native', [str(fixtures / 'vulkan-texture-host'), str(fixtures / 'vulkan-texture.vert.spv'), str(fixtures / 'vulkan-texture.frag.spv'), str(host_image), *args], native=True)
            guest_image = a.output / ('texture-' + mode + '-guest.ppm')
            texture = guest('texture-' + mode, '/tmp/texture', fixtures / 'vulkan-texture', ('/tmp/texture.vert.spv', '/tmp/texture.frag.spv', '/tmp/texture.ppm', *args),
                            tuple(('/tmp/texture.' + s + '.spv', fixtures / ('vulkan-texture.' + s + '.spv')) for s in ('vert', 'frag')),
                            (('/tmp/texture.ppm', guest_image),))
            assert native.stdout == texture.stdout and host_image.read_bytes() == guest_image.read_bytes()
        egl = guest('mesa-egl-probe', '/tmp/zink-egl', inputs / 'zink-egl', egl=True)
        report['rendering_passed'] = egl.returncode == 0 and b'zink-egl-ok:' in egl.stdout
        if a.require_render:
            assert report['rendering_passed'], 'Original Mesa Zink still lacks required Vulkan capabilities; see retained probe and requirement list'
        print('Vulkan query/template foundation passed; original Mesa Zink rendering:', report['rendering_passed'], flush=True)
finally:
    (a.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
