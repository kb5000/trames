#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a separate Firefox store using original Alpine-signed ARM64 APKs.

Requires the release CLI and product hybrid musl. Repository indexes are cached
by m6.py; inputs.json records their exact bytes and all selected APKs. The guest
apk solver verifies signatures. No --allow-untrusted or browser binary changes.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import shutil
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
ROOTFS_NAME = 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
ROOTFS_SHA = '821565fa8f3953eefd12497b166b4b50add2f7c57fb312e75862f5867e06fefe'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'target/firefox')
    parser.add_argument('--runner', type=Path, default=ROOT/'target/release/trames-run')
    parser.add_argument('--musl', type=Path, default=ROOT/'target/musl-hybrid/libc.so')
    parser.add_argument('--repository-only', action='store_true')
    parser.add_argument('--gpu', action='store_true', help='also install original Gallium/Zink, GLES and Vulkan loader dependencies')
    parser.add_argument('--xwayland', action='store_true', help='also prepare the original X server runtime dependencies and XKB data')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    store = out/'store'
    if not args.repository_only and store.exists():
        parser.error('Use a fresh output directory; this probe never replaces an existing store')
    spec = importlib.util.spec_from_file_location('m6', ROOT/'tests/integration/m6.py')
    m6 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m6)
    m6.CACHE.mkdir(parents=True, exist_ok=True)
    repo = out/'repository'
    gpu_packages = ('mesa-dri-gallium', 'mesa-egl', 'mesa-gles', 'vulkan-loader') if args.gpu else ()
    extra_packages = (*gpu_packages, *(('xwayland', 'xkeyboard-config') if args.xwayland else ()))
    if not repo.exists():
        # GTK installs at-spi2-core conditionally; m6's dependency collector does
        # not evaluate install_if. Include it explicitly so stock apk can do so.
        m6.repository(repo, ('firefox', 'font-dejavu', 'at-spi2-core', *extra_packages))
    elif extra_packages and any(not list(repo.rglob(package+'-[0-9]*.apk')) for package in extra_packages):
        # A cached Firefox-only repository is valid for software rendering but
        # does not necessarily include the runtime-loaded Zink dependencies.
        with tempfile.TemporaryDirectory(prefix='firefox-gpu-', dir=out) as temporary:
            extra = Path(temporary)/'repository'
            m6.repository(extra, extra_packages)
            shutil.copytree(extra, repo, dirs_exist_ok=True)
    files = sorted(p for p in repo.rglob('*') if p.is_file())
    for package in ('firefox', 'font-dejavu', 'at-spi2-core', 'libxtst', *extra_packages):
        assert any(p.name.startswith(package+'-') and p.suffix == '.apk' for p in files), package
    rootfs = m6.CACHE/ROOTFS_NAME
    if not rootfs.exists():
        url = 'https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/'+ROOTFS_NAME
        with urllib.request.urlopen(url, timeout=120) as response:
            rootfs.write_bytes(response.read())
    assert hashlib.sha256(rootfs.read_bytes()).hexdigest() == ROOTFS_SHA
    inputs = dict(architecture='aarch64-linux-musl', rootfs=ROOTFS_NAME,
                  gpu_dependencies=args.gpu,
                  xwayland_dependencies=args.xwayland,
                  rootfs_sha256=ROOTFS_SHA,
                  firefox=[p.name for p in files if p.name.startswith('firefox-')],
                  files={p.relative_to(repo).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    (out/'inputs.json').write_text(json.dumps(inputs, indent=2)+'\n')
    if args.repository_only:
        return
    cmd = [str(args.runner.resolve()), '--storage', str(store), '--network', 'off',
           '--cpus', '4', '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
           '--max-instructions', '500000000000', '--accelerator', 'pure',
           '--musl-libc', str(args.musl.resolve()), '--rootfs', str(rootfs.resolve())]
    for path in files:
        cmd += ['--file', '/tmp/firefox-repo/'+path.relative_to(repo).as_posix()+'='+str(path)]
    guest_files = ['/tmp/firefox-repo/'+p.relative_to(repo).as_posix() for p in files]
    # Use known input paths; deletion while enumerating a large mutable guest
    # directory has a separate getdents/rm diagnostic, not a Firefox requirement.
    cleanup = 'rm -f '+' '.join(map(shlex.quote, guest_files))+'; '
    directories = sorted((p for p in repo.rglob('*') if p.is_dir()), key=lambda p: len(p.parts), reverse=True)
    cleanup += 'rmdir '+' '.join(shlex.quote('/tmp/firefox-repo/'+p.relative_to(repo).as_posix()) for p in directories)+' /tmp/firefox-repo; '
    script = "printf '/tmp/firefox-repo/main\\n/tmp/firefox-repo/community\\n' >/tmp/firefox.repositories; "
    script += 'apk --no-network --repositories-file /tmp/firefox.repositories --no-progress add firefox font-dejavu'+''.join(' '+package for package in extra_packages)+'; r=$?; '
    script += cleanup+'if [ "$r" = 0 ]; then firefox --version; else exit "$r"; fi'
    cmd += ['--guest', '/bin/sh', '-c', script]
    began = time.monotonic()
    with (out/'install.stdout').open('wb') as stdout, (out/'install.stderr').open('wb') as stderr:
        result = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=900)
    (out/'install.json').write_text(json.dumps(dict(returncode=result.returncode,
        seconds=time.monotonic()-began, command=cmd), indent=2)+'\n')
    assert result.returncode == 0, 'See install.stdout/install.stderr'


if __name__ == '__main__':
    main()
