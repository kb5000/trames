#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build Mesa's small EGL platform adapter, linking the original Alpine Gallium.

Only the explicit DRM-less Kopper platform option changes runtime source. Mesa's
Zink/NIR/GLSL/SPIR-V/GLES2, libwayland and Vulkan loader remain original APKs.
Requires scripts/zink-egl-requirements.txt, ninja/pkg-config/bison/flex/wayland-scanner and AArch64
GCC/G++; no ARM executable is run by this build.
"""
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

ROOT = Path(__file__).resolve().parents[1]
VERSION = '25.1.9'
SOURCE_SHA = '412df33a1bb3c785ed698555a3972118a37c458e7accf6ae53f4bb87b3db454a'
PATCH = ROOT / 'patches/mesa/0001-wayland-kopper-native-wsi.patch'
work = ROOT / 'target/mesa-egl'
work.mkdir(parents=True, exist_ok=True)
inputs = ROOT / 'target/zink-inputs'
archive = inputs / ('mesa-' + VERSION + '.tar.xz')
if not archive.exists():
    with urllib.request.urlopen('https://archive.mesa3d.org/' + archive.name, timeout=180) as response:
        archive.write_bytes(response.read())
assert hashlib.sha256(archive.read_bytes()).hexdigest() == SOURCE_SHA

spec = importlib.util.spec_from_file_location('m6', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
development = work / 'development'
if not development.exists():
    m6.repository(development, ('libdrm-dev', 'wayland-dev', 'wayland-protocols',
                               'expat-dev', 'zlib-dev', 'libstdc++-dev', 'musl-dev', 'linux-headers'))
packages = sorted({*development.rglob('*.apk'), *(inputs / 'repository').rglob('*.apk')})
sdk = work / 'sdk'
sdk.mkdir(exist_ok=True)
for package in packages:
    with tarfile.open(package, ignore_zeros=True) as tar:
        members = []
        for item in tar:
            path = Path(item.name)
            assert not path.is_absolute() and '..' not in path.parts
            name = path.as_posix()
            if name.startswith(('usr/include/', 'usr/share/wayland-protocols/',
                                'usr/lib/pkgconfig/', 'usr/share/pkgconfig/')) or (
                    name.startswith(('usr/lib/', 'lib/')) and '.so' in path.name) or (
                    package.name.startswith('musl-dev-') and name.startswith('usr/lib/')
                    and path.suffix in ('.o', '.a')):
                members.append(item)
        tar.extractall(sdk, members=members, filter='data')
# pkg-config does not sysroot-rewrite arbitrary --variable values.
protocols = sdk / 'usr/share/pkgconfig/wayland-protocols.pc'
protocols.write_text(protocols.read_text().replace('prefix=/usr', 'prefix=' + str(sdk / 'usr')))
gallium = sdk / 'usr/lib' / ('libgallium-' + VERSION + '.so')
assert gallium.is_file()

source = work / ('mesa-' + VERSION)
patch_sha = hashlib.sha256(PATCH.read_bytes()).hexdigest()
stamp = work / 'source-patch.sha256'
if not stamp.exists() or stamp.read_text().strip() != patch_sha:
    if source.exists():
        shutil.rmtree(source)
    with tarfile.open(archive) as tar:
        tar.extractall(work, filter='data')
    subprocess.run(['patch', '-p1', '-i', str(PATCH)], cwd=source, check=True)
    # Build only libEGL. Link the exact original signed Gallium shared library,
    # rather than rebuilding its shader compilers, driver or internal algorithms.
    meson = source / 'src/egl/meson.build'
    text = meson.read_text()
    needle = '  link_for_egl += libgallium_dri'
    assert text.count(needle) == 1
    meson.write_text(text.replace(needle, '  link_args_for_egl += [' + repr(str(gallium)) + ']'))
    stamp.write_text(patch_sha + '\n')

cc = shlex.split(os.environ.get('MESA_CC', shutil.which('aarch64-linux-gnu-gcc-14') or 'aarch64-linux-gnu-gcc'))
cpp = shlex.split(os.environ.get('MESA_CXX', shutil.which('aarch64-linux-gnu-g++-14') or 'aarch64-linux-gnu-g++'))
includes = subprocess.check_output([*cc, '-print-file-name=include'], text=True).strip()
cxx_headers = next((sdk / 'usr/include/c++').iterdir())
cxx_arch = next(p for p in cxx_headers.iterdir() if p.is_dir() and 'aarch64' in p.name)
common = ['--sysroot=' + str(sdk), '-nostdinc', '-isystem', str(sdk / 'usr/include'),
          '-isystem', includes, '-L' + str(sdk / 'usr/lib'), '-L' + str(sdk / 'lib'),
          '-Wl,--dynamic-linker=/lib/ld-musl-aarch64.so.1', '-Wl,-rpath-link,' + str(sdk / 'usr/lib'),
          '-Wl,-rpath-link,' + str(sdk / 'lib'), '-mno-outline-atomics']
for name, compiler, flags in [('cc', cc, common), ('cpp', cpp, ['-nostdinc++', '-isystem', str(cxx_headers), '-isystem', str(cxx_arch), *common])]:
    wrapper = work / ('musl-' + name)
    wrapper.write_text('#!/bin/sh\nexec ' + shlex.join([*compiler, *flags]) + ' "$@"\n')
    wrapper.chmod(0o700)

scanner = os.environ.get('TRAMES_WAYLAND_SCANNER', shutil.which('wayland-scanner'))
assert scanner, 'native wayland-scanner required'
scanner_version = subprocess.check_output([scanner, '--version'], stderr=subprocess.STDOUT, text=True).strip().split()[-1]
native_pc = work / 'native-pc'
native_pc.mkdir(exist_ok=True)
(native_pc / 'wayland-scanner.pc').write_text('Name: Wayland Scanner\nDescription: Native protocol generator\nVersion: ' + scanner_version + '\nwayland_scanner=' + scanner + '\n')
cross = work / 'cross.ini'
cross.write_text("[binaries]\nc = '" + str(work / 'musl-cc') + "'\ncpp = '" + str(work / 'musl-cpp') + "'\nar = '" + os.environ.get('MESA_AR', 'aarch64-linux-gnu-ar') + "'\npkg-config = 'pkg-config'\n[properties]\nneeds_exe_wrapper = true\nsys_root = '" + str(sdk) + "'\npkg_config_libdir = ['" + str(sdk / 'usr/lib/pkgconfig') + "', '" + str(sdk / 'usr/share/pkgconfig') + "']\n[host_machine]\nsystem = 'linux'\ncpu_family = 'aarch64'\ncpu = 'armv8-a'\nendian = 'little'\n")
env = {**os.environ, 'PKG_CONFIG_PATH_FOR_BUILD': str(native_pc)}
options = ['-Dgallium-drivers=zink,softpipe', '-Dvulkan-drivers=', '-Dllvm=disabled', '-Dplatforms=wayland',
           '-Dglx=disabled', '-Dgbm=disabled', '-Degl=enabled', '-Dgles1=disabled', '-Dgles2=enabled',
           '-Dglvnd=disabled', '-Dvideo-codecs=', '-Dgallium-va=disabled', '-Dgallium-vdpau=disabled',
           '-Dvalgrind=disabled', '-Dlibunwind=disabled', '-Dlmsensors=disabled', '-Dbuild-tests=false',
           '-Dzstd=disabled', '--buildtype=release']
build = work / 'build'
subprocess.run(['meson', 'setup', '--wipe', str(build), str(source), '--cross-file', str(cross), *options], env=env, check=True)
subprocess.run(['ninja', '-C', str(build), '-j4', 'src/egl/libEGL.so.1.0.0'], env=env, check=True)
out = ROOT / 'target/vulkan-guest'
library = out / 'libEGL.so.1'
shutil.copyfile(build / 'src/egl/libEGL.so.1.0.0', library)
needed = subprocess.check_output([os.environ.get('AARCH64_READELF', 'aarch64-linux-gnu-readelf'), '-d', str(library)], text=True)
assert 'libc.musl-aarch64.so.1' in needed and 'libc.so.6' not in needed
assert 'libgallium-' + VERSION + '.so' in needed
manifest = dict(mesa=VERSION, architecture='aarch64-linux-musl', soname='libEGL.so.1',
                source='https://archive.mesa3d.org/' + archive.name, source_sha256=SOURCE_SHA,
                patch=str(PATCH.relative_to(ROOT)), patch_sha256=patch_sha,
                original_gallium_sha256=hashlib.sha256(gallium.read_bytes()).hexdigest(),
                egl_sha256=hashlib.sha256(library.read_bytes()).hexdigest(), options=options,
                scope='opt-in DRM-less Wayland Kopper initialization; original Gallium/Zink and Vulkan capability checks',
                compiler=subprocess.check_output([*cc, '--version'], text=True).splitlines()[0],
                development_apk_sha256={str(p.relative_to(development)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(development.rglob('*.apk'))})
(out / 'egl-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
notices = out / 'Notices/Mesa-EGL'
notices.mkdir(parents=True, exist_ok=True)
shutil.copyfile(source / 'docs/license.rst', notices / 'license.rst')
shutil.copyfile(PATCH, notices / PATCH.name)
for path in (source / 'src/util/blake3').glob('LICENSE*'):
    shutil.copyfile(path, notices / path.name)
print('Built Mesa EGL native WSI adapter:', manifest['egl_sha256'], flush=True)
