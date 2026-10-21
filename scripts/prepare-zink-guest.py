#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare original Alpine-signed Mesa/Zink and ordinary EGL/Vulkan clients."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import urllib.request
import shutil

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--wayland', action='store_true', help='also prepare original Wayland EGL window clients')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('m6', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
out = ROOT / 'target/zink-inputs'
out.mkdir(parents=True, exist_ok=True)
m6.CACHE.mkdir(parents=True, exist_ok=True)
repo = out / 'repository'
if not repo.exists():
    m6.repository(repo, ('mesa-dri-gallium', 'mesa-egl', 'mesa-gles', 'mesa-dev', 'vulkan-loader'))
if args.wayland and not any(repo.rglob('wayland-protocols-*.apk')):
    with tempfile.TemporaryDirectory(prefix='zink-wayland-repo-') as temporary:
        extra = Path(temporary) / 'repository'
        m6.repository(extra, ('wayland-dev', 'wayland-protocols', 'wayland-libs-egl'))
        shutil.copytree(extra, repo, dirs_exist_ok=True)
packages = sorted(repo.rglob('*.apk'))
assert any(p.name == 'mesa-dri-gallium-25.1.9-r0.apk' for p in packages), 'Review requirements before changing Mesa version'
sdk = Path(os.environ.get('TRAMES_ZINK_SDK', out / 'sdk'))
sdk.mkdir(exist_ok=True)
for package in packages:
    with tarfile.open(package, ignore_zeros=True) as archive:
        for entry in archive:
            name = Path(entry.name)
            shared = '.so.' in name.name or name.name.endswith('.so')
            if shared and (entry.issym() or entry.islnk()):
                # ELF sonames do not always match the real file's basename:
                # libelf.so.1 -> libelf-0.193.so, for example.
                target_name = Path(entry.linkname).name
                # ../llvm20/lib/libclang-cpp.so.20.1 flattens to the same
                # basename. Its real file will populate this slot; a link here
                # would point to itself in a clean SDK.
                if target_name == name.name:
                    continue
                link = sdk / name.name
                if not link.exists() and not link.is_symlink():
                    link.symlink_to(target_name)
                continue
            if not entry.isfile():
                continue
            if args.wayland and name.as_posix().endswith('/stable/xdg-shell/xdg-shell.xml'):
                (sdk / 'xdg-shell.xml').write_bytes(archive.extractfile(entry).read())
                continue
            if name.as_posix().startswith(('usr/include/EGL/', 'usr/include/GLES2/', 'usr/include/GLES3/', 'usr/include/KHR/')) or (
                    args.wayland and name.parent.as_posix() == 'usr/include' and name.name.startswith('wayland-')):
                target = sdk / name
            elif shared:
                target = sdk / name.name
            else:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.is_symlink():
                target.unlink()
            target.write_bytes(archive.extractfile(entry).read())
            if '.so.' in name.name:
                base, version = name.name.split('.so.', 1)
                link = sdk / (base + '.so.' + version.split('.')[0])
                if not link.exists() and not link.is_symlink():
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
subprocess.run([*common, str(ROOT / 'tests/fixtures/zink-es3.c'), '-l:libEGL.so.1',
                '-l:libGLESv2.so.2', '-o', str(out / 'zink-es3')], check=True)
if args.wayland:
    scanner = os.getenv('TRAMES_WAYLAND_SCANNER', 'wayland-scanner')
    subprocess.run([scanner, 'client-header', str(sdk / 'xdg-shell.xml'), str(sdk / 'xdg-shell-client-protocol.h')], check=True)
    subprocess.run([scanner, 'private-code', str(sdk / 'xdg-shell.xml'), str(sdk / 'xdg-shell-protocol.c')], check=True)
    subprocess.run([*common, '-I' + str(sdk), str(ROOT / 'tests/fixtures/zink-wayland.c'),
                    str(sdk / 'xdg-shell-protocol.c'), '-l:libEGL.so.1', '-l:libGLESv2.so.2',
                    '-l:libwayland-client.so.0', '-l:libwayland-egl.so.1', '-o', str(out / 'zink-wayland')], check=True)
    subprocess.run([*common, str(ROOT / 'tests/fixtures/vulkan-copy.c'), '-l:libvulkan.so.1', '-o', str(out / 'vulkan-copy')], check=True)
    subprocess.run([os.getenv('HOST_CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(headers),
                    str(ROOT / 'tests/fixtures/vulkan-copy.c'), os.getenv('VULKAN_HOST_LINK', '-lvulkan'),
                    '-o', str(out / 'vulkan-copy-host')], check=True)
subprocess.run([*common, str(ROOT / 'tests/fixtures/vulkan-queries.c'), '-l:libvulkan.so.1',
                '-o', str(out / 'vulkan-queries')], check=True)
subprocess.run([os.getenv('HOST_CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror',
                '-I' + str(headers), str(ROOT / 'tests/fixtures/vulkan-queries.c'),
                os.getenv('VULKAN_HOST_LINK', '-lvulkan'), '-o', str(out / 'vulkan-queries-host')], check=True)
subprocess.run([*common, str(ROOT / 'tests/fixtures/vulkan-timeline.c'), '-l:libvulkan.so.1', '-pthread', '-o', str(out / 'vulkan-timeline')], check=True)
subprocess.run([os.getenv('HOST_CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(headers), str(ROOT / 'tests/fixtures/vulkan-timeline.c'), os.getenv('VULKAN_HOST_LINK', '-lvulkan'), '-pthread', '-o', str(out / 'vulkan-timeline-host')], check=True)
subprocess.run([*common, str(ROOT / 'tests/fixtures/vulkan-resources.c'), '-l:libvulkan.so.1', '-o', str(out / 'vulkan-resources')], check=True)
subprocess.run([os.getenv('HOST_CC', 'cc'), '-O2', '-Wall', '-Wextra', '-Werror', '-I' + str(headers), str(ROOT / 'tests/fixtures/vulkan-resources.c'), os.getenv('VULKAN_HOST_LINK', '-lvulkan'), '-o', str(out / 'vulkan-resources-host')], check=True)
report = dict(mesa='25.1.9-r0', packages_sha256={str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in packages},
              upstream_source='https://archive.mesa3d.org/mesa-25.1.9.tar.xz',
              upstream_source_sha256='412df33a1bb3c785ed698555a3972118a37c458e7accf6ae53f4bb87b3db454a',
              rootfs_sha256=hashlib.sha256(rootfs.read_bytes()).hexdigest())
(out / 'inputs.json').write_text(json.dumps(report, indent=2) + '\n')
print('Prepared original signed Mesa25.1.9, EGL pixel client and Vulkan query client', flush=True)
