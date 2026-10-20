#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build original Xwayland/glamor with the explicit DRM-less EGL transport.

No shader compiler/rasterizer replacement. Mesa/Gallium/Zink remain original.
Run after prepare-xwayland-guest.py and the normal Mesa EGL prerequisite build.
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

ROOT=Path(__file__).resolve().parents[1]
work=ROOT/'target/xwayland-native'
work.mkdir(parents=True,exist_ok=True)
version='24.1.9'
source_sha='f297af27a84508db9b80d1cbbcc69c3801da38eb64c72f3b5b50f582459afdd0'
archive=ROOT/'target/xwayland'/('xwayland-'+version+'.tar.xz')
archive.parent.mkdir(parents=True,exist_ok=True)
if not archive.exists():
    with urllib.request.urlopen('https://www.x.org/releases/individual/xserver/'+archive.name,timeout=180) as r:archive.write_bytes(r.read())
assert hashlib.sha256(archive.read_bytes()).hexdigest()==source_sha
spec=importlib.util.spec_from_file_location('m6',ROOT/'tests/integration/m6.py')
m6=importlib.util.module_from_spec(spec);spec.loader.exec_module(m6)
development=ROOT/'target/xwayland/development'
if not development.exists():
    m6.repository(development,('xorgproto','xtrans','libx11-dev','libxkbfile-dev','libxfont2-dev',
        'libxcvt-dev','pixman-dev','libepoxy-dev','libdrm-dev','mesa-dev','libxshmfence-dev',
        'libmd-dev','musl-dev','linux-headers','wayland-dev','wayland-protocols'))
sdk=work/'sdk';sdk.mkdir(exist_ok=True)
for package in sorted(development.rglob('*.apk')):
    with tarfile.open(package,ignore_zeros=True) as t:
        selected=[]
        for entry in t:
            path=Path(entry.name);assert not path.is_absolute() and '..' not in path.parts
            name=path.as_posix()
            if name.startswith(('usr/include/','usr/share/wayland-protocols/','usr/lib/pkgconfig/','usr/share/pkgconfig/')) or (
                name.startswith(('usr/lib/','lib/')) and '.so' in path.name) or (
                package.name.startswith('musl-dev-') and name.startswith('usr/lib/') and path.suffix in ('.o','.a')):
                selected.append(entry)
        t.extractall(sdk,members=selected,filter='data')
for name in ('wayland-protocols','xproto'):
    for pc in (sdk/'usr/share/pkgconfig').glob(name+'.pc'):
        pc.write_text(pc.read_text().replace('prefix=/usr','prefix='+str(sdk/'usr')))
source=work/('xwayland-'+version)
patch=ROOT/'patches/xwayland/0001-native-wayland-egl-wsi.patch'
adapter=[ROOT/'accelerator/xwayland'/name for name in ('native-wsi.c','native-wsi.h')]
patch_sha=hashlib.sha256(patch.read_bytes()).hexdigest()
fingerprint=hashlib.sha256(patch.read_bytes()+b''.join(p.read_bytes() for p in adapter)).hexdigest()
stamp=work/'source.sha256'
if not stamp.exists() or stamp.read_text().strip()!=patch_sha:
    if source.exists():shutil.rmtree(source)
    with tarfile.open(archive) as t:t.extractall(work,filter='data')
    subprocess.run(['patch','-p1','-i',str(patch)],cwd=source,check=True)
    stamp.write_text(patch_sha+'\n')
for path in adapter:
    destination=source/'hw/xwayland'/path.name
    if not destination.exists() or destination.read_bytes()!=path.read_bytes():
        shutil.copyfile(path,destination)
compiler=shlex.split(os.environ.get('AARCH64_CC','aarch64-linux-gnu-gcc'))
includes=subprocess.check_output([*compiler,'-print-file-name=include'],text=True).strip()
flags=['--sysroot='+str(sdk),'-nostdinc','-isystem',str(sdk/'usr/include'),'-isystem',includes,
       '-L'+str(sdk/'usr/lib'),'-L'+str(sdk/'lib'),'-Wl,--dynamic-linker=/lib/ld-musl-aarch64.so.1',
       '-Wl,-rpath-link,'+str(sdk/'usr/lib'),'-Wl,-rpath-link,'+str(sdk/'lib'),'-mno-outline-atomics']
wrapper=work/'musl-cc';wrapper.write_text('#!/bin/sh\nexec '+shlex.join([*compiler,*flags])+' "$@"\n');wrapper.chmod(0o700)
scanner=os.environ.get('TRAMES_WAYLAND_SCANNER',shutil.which('wayland-scanner'))
assert scanner,'native wayland-scanner is required'
native_pc=work/'native-pc';native_pc.mkdir(exist_ok=True)
scanner_version=subprocess.check_output([scanner,'--version'],stderr=subprocess.STDOUT,text=True).strip().split()[-1]
(native_pc/'wayland-scanner.pc').write_text('Name: Wayland Scanner\nDescription: Native scanner\nVersion: '+scanner_version+'\nwayland_scanner='+scanner+'\n')
cross=work/'cross.ini'
cross.write_text("[binaries]\nc = '"+str(wrapper)+"'\nar = '"+os.environ.get('AARCH64_AR','aarch64-linux-gnu-ar')+"'\npkg-config = 'pkg-config'\n[properties]\nneeds_exe_wrapper = true\nsys_root = '"+str(sdk)+"'\npkg_config_libdir = ['"+str(sdk/'usr/lib/pkgconfig')+"', '"+str(sdk/'usr/share/pkgconfig')+"']\n[host_machine]\nsystem = 'linux'\ncpu_family = 'aarch64'\ncpu = 'armv8-a'\nendian = 'little'\n")
options=['--buildtype=release','-Dglamor=true','-Dglx=false','-Dxvfb=false','-Dxwayland_ei=false',
         '-Dlibdecor=false','-Dxdmcp=false','-Dsecure-rpc=false','-Dinput_thread=false',
         '-Dsystemd_notify=false','-Dxselinux=false','-Ddocs=false','-Ddevel-docs=false','-Ddocs-pdf=false',
         '-Dsha1=libmd','-Dxkb_dir=/usr/share/X11/xkb','-Dxkb_output_dir=/tmp',
         '-Dxkb_bin_dir=/usr/bin','-Ddefault_font_path=/usr/share/fonts/misc,/usr/share/fonts/100dpi,/usr/share/fonts/75dpi']
env={**os.environ,'PKG_CONFIG_PATH_FOR_BUILD':str(native_pc)}
build=work/'build'
setup=['--reconfigure'] if (build/'build.ninja').exists() else []
subprocess.run(['meson','setup',*setup,str(build),str(source),'--cross-file',str(cross),*options],env=env,check=True)
subprocess.run(['ninja','-C',str(build),'-j4','hw/xwayland/Xwayland'],env=env,check=True)
subprocess.run([str(wrapper),'-O2','-Wall','-Wextra','-Werror',
    str(ROOT/'tests/fixtures/x11-pixels.c'),'-lX11','-o',str(work/'x11-pixels')],check=True)
subprocess.run([str(wrapper),'-O2','-Wall','-Wextra','-Werror','-fPIC','-shared',
    str(ROOT/'tests/fixtures/xwayland-wsi-pixels.c'),'-lEGL','-lGLESv2',
    '-o',str(work/'libwsi-pixels.so')],check=True)
out=ROOT/'target/vulkan-guest';out.mkdir(exist_ok=True)
binary=out/'Xwayland-native';shutil.copyfile(build/'hw/xwayland/Xwayland',binary)
manifest=dict(upstream='Xwayland '+version,source_sha256=source_sha,adapter_sha256=fingerprint,
    executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),architecture='aarch64-linux-musl',
    glx=False,scope='original glamor with native EGL/Wayland WSI, no DRM or SHM presentation',options=options)
(out/'xwayland-native-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
notices=out/'Notices/Xwayland';notices.mkdir(parents=True,exist_ok=True)
shutil.copyfile(source/'COPYING',notices/'COPYING')
shutil.copyfile(ROOT/'LICENSE',notices/'GPL-3.0.txt')
shutil.copyfile(patch,notices/patch.name)
with tarfile.open(work/'xwayland-native-source.tar.gz','w:gz') as bundle:
    for path,name in [(archive,archive.name),(patch,'patches/xwayland/'+patch.name),
        (ROOT/'LICENSE','LICENSE'),(Path(__file__),'scripts/build-xwayland-native.py'),
        (ROOT/'tests/integration/m6.py','tests/integration/m6.py')]:
        bundle.add(path,arcname=name)
    for path in adapter:
        bundle.add(path,arcname='accelerator/xwayland/'+path.name)
    bundle.add(ROOT/'tests/fixtures/x11-pixels.c',arcname='tests/fixtures/x11-pixels.c')
    bundle.add(ROOT/'tests/fixtures/xwayland-wsi-pixels.c',arcname='tests/fixtures/xwayland-wsi-pixels.c')
print('Built native Xwayland transport:',manifest['executable_sha256'],flush=True)
