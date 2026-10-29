#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build pinned ARM64-musl Mesa for native Wayland and typed PBO uploads.

Run inside an Alpine ARM64 build environment. This emits the paired EGL and
Gallium libraries and preserves the original source, patches and build record.
It does not install libraries or change a Firefox profile.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '25.1.9'
SOURCE_SHA = '412df33a1bb3c785ed698555a3972118a37c458e7accf6ae53f4bb87b3db454a'
PATCHES = ['0001-wayland-kopper-native-wsi.patch', '0002-preserve-pbo-upload-output-type.patch']
OPTIONS = ['--buildtype=release', '-Dgallium-drivers=zink', '-Dvulkan-drivers=',
           '-Dllvm=disabled', '-Dplatforms=wayland', '-Dglx=disabled', '-Dgbm=disabled',
           '-Degl=enabled', '-Dgles1=disabled', '-Dgles2=enabled', '-Dglvnd=disabled',
           '-Dvideo-codecs=', '-Dgallium-va=disabled', '-Dgallium-vdpau=disabled',
           '-Dvalgrind=disabled', '-Dlibunwind=disabled', '-Dlmsensors=disabled',
           '-Dbuild-tests=false', '-Dzstd=disabled']


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Original mesa-25.1.9.tar.xz')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if platform.system() != 'Linux' or platform.machine() not in ('aarch64', 'arm64'):
        parser.error('a native ARM64 Linux musl build environment is required')
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    machine = subprocess.check_output(compiler + ['-dumpmachine'], text=True).strip()
    if 'musl' not in machine or args.jobs < 1:
        parser.error('use an ARM64-musl compiler and positive job count')
    if digest(args.source) != SOURCE_SHA:
        parser.error('original Mesa archive SHA-256 mismatch')
    record = dict(mesa=VERSION, source_sha256=SOURCE_SHA, architecture=machine,
                  compiler=subprocess.check_output(compiler + ['--version'], text=True),
                  meson=subprocess.check_output(['meson', '--version'], text=True).strip(),
                  options=OPTIONS, patches={name: digest(ROOT / 'patches/mesa' / name) for name in PATCHES},
                  recipe_sha256=digest(Path(__file__)))
    key = hashlib.sha256(json.dumps(record, sort_keys=True).encode()).hexdigest()[:20]
    out = args.output.resolve()
    work = out / ('build-' + key)
    work.mkdir(parents=True, exist_ok=True)
    source = work / ('mesa-' + VERSION)
    if not source.exists():
        with tarfile.open(args.source) as archive:
            archive.extractall(work, filter='data')
        for name in PATCHES:
            subprocess.run(['patch', '-p1', '-i', str(ROOT / 'patches/mesa' / name)], cwd=source, check=True)
    build = work / 'build'
    if not (build / 'build.ninja').exists():
        subprocess.run(['meson', 'setup', str(build), str(source), *OPTIONS], check=True)
    targets = {'libgallium-' + VERSION + '.so': 'src/gallium/targets/dri/libgallium-' + VERSION + '.so',
               'libEGL.so.1': 'src/egl/libEGL.so.1.0.0'}
    subprocess.run(['ninja', '-C', str(build), '-j' + str(args.jobs), *targets.values()], check=True)
    for name, target in targets.items():
        shutil.copyfile(build / target, out / name)
    sources = out / 'sources'
    sources.mkdir(exist_ok=True)
    shutil.copyfile(args.source, sources / ('mesa-' + VERSION + '.tar.xz'))
    for name in PATCHES:
        shutil.copyfile(ROOT / 'patches/mesa' / name, sources / name)
    shutil.copyfile(__file__, sources / Path(__file__).name)
    record['libraries'] = {name: digest(out / name) for name in targets}
    record['source_files'] = {path.name: digest(path) for path in sources.iterdir() if path.is_file()}
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Built paired native-Wayland Mesa libraries:', json.dumps(record['libraries']))


if __name__ == '__main__':
    main()
