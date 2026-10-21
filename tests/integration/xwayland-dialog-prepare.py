#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare only signed mousepad/Xwayland inputs; reuse the built EGL/server/musl."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('m6', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
m6.CACHE.mkdir(parents=True, exist_ok=True)
rootfs = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
if not rootfs.exists():
    url = 'https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/' + rootfs.name
    with urllib.request.urlopen(url, timeout=120) as response:
        rootfs.write_bytes(response.read())
    with urllib.request.urlopen(url + '.sha256', timeout=120) as response:
        assert hashlib.sha256(rootfs.read_bytes()).hexdigest() == response.read().decode().split()[0]
work = ROOT / 'target/xwayland-dialog-inputs'
repo = work / 'repository'
packages = ('mousepad', 'xwayland', 'xkeyboard-config', 'xprop', 'font-dejavu',
            'mesa-dri-gallium', 'mesa-egl', 'mesa-gles', 'vulkan-loader', 'wayland-libs-egl')
assert not repo.exists(), 'Use a fresh fixture directory'
m6.repository(repo, (*packages, 'libx11-dev', 'libxrender-dev', 'musl-dev'))
sdk = work / 'sdk'
sdk.mkdir()
for package in sorted(repo.rglob('*.apk')):
    with tarfile.open(package, ignore_zeros=True) as archive:
        selected = []
        for entry in archive:
            path = Path(entry.name)
            assert not path.is_absolute() and '..' not in path.parts
            name = path.as_posix()
            if name.startswith('usr/include/') or (name.startswith(('usr/lib/', 'lib/')) and '.so' in path.name) or (package.name.startswith('musl-dev-') and name.startswith('usr/lib/') and path.suffix in ('.o', '.a')):
                selected.append(entry)
        archive.extractall(sdk, members=selected, filter='data')
compiler = shlex.split(os.getenv('AARCH64_CC', 'aarch64-linux-gnu-gcc'))
includes = subprocess.check_output([*compiler, '-print-file-name=include'], text=True).strip()
flags = ['--sysroot=' + str(sdk), '-nostdinc', '-isystem', str(sdk / 'usr/include'), '-isystem', includes,
         '-L' + str(sdk / 'usr/lib'), '-L' + str(sdk / 'lib'), '-Wl,--dynamic-linker=/lib/ld-musl-aarch64.so.1',
         '-Wl,-rpath-link,' + str(sdk / 'usr/lib'), '-Wl,-rpath-link,' + str(sdk / 'lib'), '-mno-outline-atomics']
wrapper = ROOT / 'target/busybox-build/musl/bin/musl-gcc'
wrapper.parent.mkdir(parents=True, exist_ok=True)
wrapper.write_text('#!/bin/sh\nexec ' + shlex.join([*compiler, *flags]) + ' "$@"\n')
wrapper.chmod(0o700)
libc = next(p for p in (sdk / 'lib/libc.musl-aarch64.so.1', sdk / 'lib/ld-musl-aarch64.so.1') if p.is_file())
original = ROOT / 'target/alpine-build/lib/libc.so'
original.parent.mkdir(parents=True, exist_ok=True)
shutil.copyfile(libc, original)
subprocess.run(['bash', 'scripts/build-vulkan-guest.sh'], cwd=ROOT, check=True)
store = work / 'store'
imports = [v for f in sorted(repo.rglob('*')) if f.is_file() for v in ('--file', '/tmp/dialog-repo/' + str(f.relative_to(repo)) + '=' + str(f))]
command = [str(ROOT / 'target/release/trames-run'), '--storage', str(store), '--rootfs', str(rootfs),
           '--network', 'off', '--cpus', '4', '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
           '--accelerator', 'pure', '--musl-libc', str(ROOT / 'target/musl-hybrid/libc.so'),
           '--max-instructions', '100000000000', *imports, '--guest', '/bin/sh', '-ec',
           'printf "/tmp/dialog-repo/main\\n/tmp/dialog-repo/community\\n" >/tmp/dialog.repositories; apk --repositories-file /tmp/dialog.repositories --no-progress add ' + ' '.join(packages)]
result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=600)
(work / 'install.stdout').write_bytes(result.stdout)
(work / 'install.stderr').write_bytes(result.stderr)
assert result.returncode == 0, (result.stdout[-2000:], result.stderr[-2000:])
# No SDK, APK cache, or diagnostic data is needed by the Apple runner.
shutil.copytree(ROOT / 'target/vulkan-guest', work / 'guest')
shutil.copytree(ROOT / 'target/musl-hybrid', work / 'musl')
manifest = dict(source=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(), sha256={
    str(p.relative_to(work)): hashlib.sha256(p.read_bytes()).hexdigest()
    for folder in ('store', 'guest', 'musl') for p in sorted((work / folder).rglob('*')) if p.is_file()})
(work / 'inputs.json').write_text(json.dumps(manifest, indent=2) + '\n')
with tarfile.open(work / 'inputs.tar.gz', 'w:gz') as archive:
    for name in ('store', 'guest', 'musl', 'inputs.json'):
        archive.add(work / name, arcname=name)
print('Prepared exact original mousepad and native WSI inputs; runtime and sources remain paired.')
