#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check stock FFmpeg/libva frame export and two-process native Vulkan/EGL import.

Build vaapi-native-import.c on ARM64 Linux with pkg-config media libraries.
Use -DTEST_EGL and link egl/glesv2 for the original Mesa EGL/GLES variant.
A separately signed Mac runner and an isolated guest store are required.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('runner', 'storage', 'program', 'driver', 'icd', 'sample', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--rootfs', type=Path)
    parser.add_argument('--libraries', type=Path, help='Flat directory of exact Linux runtime .so dependencies')
    parser.add_argument('--egl', type=Path, help='Paired Mesa EGL library; requires --gallium and an EGL fixture')
    parser.add_argument('--gallium', type=Path)
    args = parser.parse_args()
    if bool(args.egl) != bool(args.gallium):
        parser.error('--egl and --gallium must be paired')
    files = {'check': args.program, 'trames_drv_video.so': args.driver,
             'libtrames_vulkan_icd.so': args.icd, 'sample.h264': args.sample}
    if args.libraries:
        for file in args.libraries.iterdir():
            if file.is_file():
                files[file.name] = file
    if args.egl:
        files.update({'libEGL.so.1': args.egl, 'libgallium-25.1.9.so': args.gallium})
    checksums = {name: hashlib.sha256(file.read_bytes()).hexdigest() for name, file in sorted(files.items())}
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    guest = '/opt/trames-video-check-' + uuid.uuid4().hex
    manifest = out / 'trames.json'
    manifest.write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
        'api_version': '1.2.0', 'library_path': guest + '/libtrames_vulkan_icd.so'}}) + '\n')
    files['trames.json'] = manifest
    command = [str(args.runner.resolve()), '--storage', str(args.storage.resolve()),
               '--no-windows', '--no-pty', '--video', '--vulkan', '--network', 'off',
               '--cpus', '4', '--timeout', '120']
    if args.rootfs:
        command += ['--rootfs', str(args.rootfs.resolve())]
    for name, file in files.items():
        command += ['--exec-file', guest + '/' + name + '=' + str(file.resolve())]
    env = {'LIBVA_DRIVER_NAME': 'trames', 'LIBVA_DRIVERS_PATH': guest,
           'VK_DRIVER_FILES': guest + '/trames.json', 'LD_LIBRARY_PATH': guest,
           'GALLIUM_DRIVER': 'zink', 'MESA_LOADER_DRIVER_OVERRIDE': 'zink',
           'EGL_PLATFORM': 'surfaceless'}
    script = 'export ' + ' '.join(k + '=' + shlex.quote(v) for k, v in env.items())
    script += '; exec ' + shlex.join([guest + '/check', guest + '/sample.h264'])
    result = subprocess.run(command + ['/bin/sh', '-ec', script], capture_output=True,
                            env={**os.environ, 'MVK_CONFIG_LOG_LEVEL': '1'})
    (out / 'stdout.log').write_bytes(result.stdout)
    (out / 'stderr.log').write_bytes(result.stderr)
    passed = result.returncode == 0 and b'vaapi-native-import-ok:' in result.stdout
    if args.egl:
        passed &= b'EGL renderer: zink Vulkan' in result.stderr and b'MOLTENVK' in result.stderr
    report = {'passed': bool(passed), 'returncode': result.returncode, 'sha256': checksums,
              'scope': 'hardware H.264; export before sync; fork/SCM_RIGHTS; native GPU plane pixels',
              'frontend': 'EGL/GLES/Zink' if args.egl else 'Vulkan'}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': bool(passed), 'frontend': report['frontend'], 'output': str(out)}))
    if not passed:
        print(result.stderr.decode(errors='replace')[-4000:])
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
