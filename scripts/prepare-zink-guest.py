#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare original Alpine-signed Mesa/Zink and ordinary EGL/Vulkan clients."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('m6', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
out = ROOT / 'target/zink-inputs'
out.mkdir(parents=True, exist_ok=True)
m6.CACHE.mkdir(parents=True, exist_ok=True)
repo = out / 'repository'
if not repo.exists():
    m6.repository(repo, ('mesa-dri-gallium', 'mesa-egl', 'mesa-gles', 'mesa-dev', 'vulkan-loader'))
packages = sorted(repo.rglob('*.apk'))
assert any(p.name == 'mesa-dri-gallium-25.1.9-r0.apk' for p in packages), 'Review requirements before changing Mesa version'
sdk = out / 'sdk'
sdk.mkdir(exist_ok=True)
for package in packages:
    with tarfile.open(package, ignore_zeros=True) as archive:
        for entry in archive:
            if not entry.isfile():
                continue
            name = Path(entry.name)
            if name.as_posix().startswith(('usr/include/EGL/', 'usr/include/GLES2/', 'usr/include/KHR/')):
                target = sdk / name
            elif '.so.' in name.name:
                target = sdk / name.name
            else:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.extractfile(entry).read())
            if '.so.' in name.name:
                base, version = name.name.split('.so.', 1)
                link = sdk / (base + '.so.' + version.split('.')[0])
                if not link.exists():
                    link.symlink_to(target.name)
rootfs = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
if not rootfs.exists():
    with urllib.request.urlopen('https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/' + rootfs.name, timeout=120) as response:
        rootfs.write_bytes(response.read())
headers = next((ROOT / 'target/vulkan-upstream/headers').glob('*/include'))
compiler = os.getenv('MUSL_CC', str(ROOT / 'target/busybox-build/musl/bin/musl-gcc'))
common = [compiler, '-O2', '-mno-outline-atomics', '-Wall', '-Wextra', '-Werror',
          '-I' + str(headers), '-I' + str(sdk / 'usr/include'), '-L' + str(sdk),
          '-L' + str(ROOT / 'target/alpine-build/lib'), '-Wl,-rpath-link,' + str(sdk)]
subprocess.run([*common, str(ROOT / 'tests/fixtures/zink-egl.c'), '-l:libEGL.so.1',
                '-l:libGLESv2.so.2', '-o', str(out / 'zink-egl')], check=True)
subprocess.run([*common, str(ROOT / 'tests/fixtures/vulkan-queries.c'), '-l:libvulkan.so.1',
                '-o', str(out / 'vulkan-queries')], check=True)
subprocess.run([os.getenv('HOST_CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(headers), str(ROOT / 'tests/fixtures/vulkan-queries.c'),
                os.getenv('VULKAN_HOST_LINK', '-lvulkan'), '-o', str(out / 'vulkan-queries-host')], check=True)
report = dict(mesa='25.1.9-r0', packages_sha256={str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in packages},
              upstream_source='https://archive.mesa3d.org/mesa-25.1.9.tar.xz',
              upstream_source_sha256='412df33a1bb3c785ed698555a3972118a37c458e7accf6ae53f4bb87b3db454a',
              rootfs_sha256=hashlib.sha256(rootfs.read_bytes()).hexdigest())
(out / 'inputs.json').write_text(json.dumps(report, indent=2) + '\n')
print('Prepared original signed Mesa25.1.9, EGL pixel client and Vulkan query client', flush=True)
