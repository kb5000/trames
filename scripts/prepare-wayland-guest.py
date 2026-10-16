#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare original signed Wayland clients and a C ABI peer for Apple acceptance.

Runtime APKs are installed and signature-verified by the ordinary guest apk.
Nothing replaces or patches Weston, vkcube, libwayland, or their protocols.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('m6', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
output = ROOT / 'target/vulkan-guest/wayland-acceptance'
output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wayland-acceptance-') as temporary:
    tmp = Path(temporary)
    runtime = tmp / 'runtime'
    m6.repository(runtime, ('wayland-utils', 'weston-clients', 'vulkan-tools', 'vulkan-loader', 'font-dejavu', 'breeze-cursors'))
    sdk = tmp / 'sdk'
    sdk.mkdir()
    development = tmp / 'development'
    m6.repository(development, ('wayland-dev', 'wayland-protocols'))
    includes = sdk / 'include'
    includes.mkdir()
    xml = sdk / 'xdg-shell.xml'
    for package in sorted([*runtime.rglob('*.apk'), *development.rglob('*.apk')]):
        with tarfile.open(package, ignore_zeros=True) as archive:
            for item in archive:
                if not item.isfile():
                    continue
                name = Path(item.name)
                if '.so.' in name.name:
                    (sdk / name.name).write_bytes(archive.extractfile(item).read())
                    base, version = name.name.split('.so.', 1)
                    soname = sdk / (base + '.so.' + version.split('.')[0])
                    if not soname.exists():
                        soname.symlink_to(name.name)
                elif name.parent.as_posix().lstrip('./') == 'usr/include' and name.name.startswith('wayland-'):
                    (includes / name.name).write_bytes(archive.extractfile(item).read())
                elif item.name.endswith('/stable/xdg-shell/xdg-shell.xml'):
                    xml.write_bytes(archive.extractfile(item).read())
    scanner = os.getenv('TRAMES_WAYLAND_SCANNER', 'wayland-scanner')
    subprocess.run([scanner, 'client-header', str(xml), str(sdk / 'xdg-shell-client-protocol.h')], check=True)
    subprocess.run([scanner, 'private-code', str(xml), str(sdk / 'xdg-shell-protocol.c')], check=True)
    compiler = os.getenv('MUSL_CC', str(ROOT / 'target/busybox-build/musl/bin/musl-gcc'))
    library = next(sdk.glob('libwayland-client.so.0.*')).name
    subprocess.run([compiler, '-O2', '-mno-outline-atomics', '-Wall', '-Wextra', '-Werror',
        '-I' + str(includes), '-I' + str(sdk), str(ROOT / 'tests/fixtures/wayland-client.c'),
        str(sdk / 'xdg-shell-protocol.c'), '-L' + str(sdk), '-L' + str(ROOT / 'target/alpine-build/lib'),
        '-Wl,-rpath-link,' + str(sdk), '-Wl,-rpath-link,' + str(ROOT / 'target/alpine-build/lib'),
        '-l:' + library, '-o', str(output / 'wayland-client')], check=True)
    for package in runtime.rglob('*.apk'):
        shutil.copyfile(package, output / package.name)
    # Weston splits its original CSD assets from weston-clients. Retain the full
    # signed package for guest verification, while installing only its PNG data.
    with tarfile.open(m6.CACHE / 'APKINDEX-community.tar.gz') as index:
        records = index.extractfile('APKINDEX').read().decode().split('\n\n')
    record = next(r for r in records if '\nP:weston\n' in '\n' + r + '\n')
    version = next(line[2:] for line in record.splitlines() if line.startswith('V:'))
    name = 'weston-' + version + '.apk'
    package = m6.CACHE / name
    if not package.exists():
        with urllib.request.urlopen('https://dl-cdn.alpinelinux.org/alpine/v3.22/community/aarch64/' + name, timeout=120) as response:
            package.write_bytes(response.read())
    shutil.copyfile(package, output / 'weston-assets.apk.input')
    with tarfile.open(package, ignore_zeros=True) as archive:
        for item in archive:
            if item.isfile() and item.name.startswith('usr/share/weston/') and item.name.endswith('.png'):
                destination = output / 'assets' / item.name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.extractfile(item).read())
with tarfile.open(output / 'assets.tar', 'w') as archive:
    for path in sorted((output / 'assets').rglob('*.png')):
        archive.add(path, arcname=path.relative_to(output / 'assets').as_posix())
manifest = {p.relative_to(output).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'manifest.json'}
(output / 'manifest.json').write_text(json.dumps({'architecture':'aarch64-linux-musl',
    'inputs_sha256':manifest, 'clients':'original Alpine v3.22 packages; fixture uses original libwayland-client C ABI'}, indent=2) + '\n')
print('Wayland acceptance inputs:', len(manifest), 'files', flush=True)
