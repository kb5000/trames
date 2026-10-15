#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real vertex/fragment rasterization, native oracle and guest image export."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('m6_repository', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
fixtures = ROOT / 'tests/fixtures/bin'
artifacts = ROOT / 'target/vulkan-render'
artifacts.mkdir(exist_ok=True)
reference = subprocess.run([str(fixtures / 'vulkan-render-host'),
    str(fixtures / 'vulkan-render.vert.spv'), str(fixtures / 'vulkan-render.frag.spv'),
    str(artifacts / 'native.ppm')], capture_output=True, timeout=60)
assert reference.returncode == 0 and reference.stdout.endswith(b'vulkan-render-ok\n'), reference
print(reference.stderr.decode(), end='', flush=True)
image = (artifacts / 'native.ppm').read_bytes()
assert image.startswith(b'P6\n256 192\n255\n') and len(image) == len(b'P6\n256 192\n255\n') + 256 * 192 * 3
with tempfile.TemporaryDirectory(prefix='trames-vulkan-render-') as directory:
    tmp = Path(directory)
    m6.repository(tmp / 'repository', ('vulkan-loader',))
    packages = sorted((tmp / 'repository').rglob('*.apk'))
    for cpus in ('1', '4'):
        destination = artifacts / ('guest-' + cpus + '.ppm')
        # CLI exports intentionally create new files. Remove only this test's
        # prior generated output so repeated local runs obey the same contract.
        destination.unlink(missing_ok=True)
        base = [runner, '--storage', str(tmp / ('store-' + cpus)), '--cpus', cpus,
            '--vulkan', '--metrics', '--max-instructions', '10000000000']
        imported = []
        for package in packages:
            imported += ['--file', '/tmp/' + package.name + '=' + str(package)]
        p = subprocess.run([*base, '--rootfs', str(m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
            'add', *['/tmp/' + package.name for package in packages]], capture_output=True, timeout=180)
        assert p.returncode == 0 and b'ERROR:' not in p.stdout + p.stderr, p
        p = subprocess.run([*base,
            '--env', 'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json',
            '--file', '/usr/lib/libtrames_vulkan_icd.so=' + str(ROOT / 'target/vulkan-guest/libtrames_vulkan_icd.so'),
            '--file', '/usr/share/vulkan/icd.d/trames.json=' + str(ROOT / 'target/vulkan-guest/trames.json'),
            '--file', '/tmp/vertex.spv=' + str(fixtures / 'vulkan-render.vert.spv'),
            '--file', '/tmp/fragment.spv=' + str(fixtures / 'vulkan-render.frag.spv'),
            '--exec-file', '/tmp/vulkan-render=' + str(fixtures / 'vulkan-render'),
            '--export', '/tmp/render.ppm=' + str(destination),
            '--guest', '/tmp/vulkan-render', '/tmp/vertex.spv', '/tmp/fragment.spv', '/tmp/render.ppm',
            'bridge-check'], capture_output=True, timeout=180)
        if p.returncode != 0:
            print(p.stdout.decode(errors='replace'), flush=True)
            print(p.stderr.decode(errors='replace'), flush=True)
            log = subprocess.run([*base, '--guest', '/bin/dmesg'], capture_output=True, timeout=15)
            print(log.stdout.decode(errors='replace'), flush=True)
        assert p.returncode == 0 and p.stdout == reference.stdout, p
        assert destination.read_bytes() == image
        print(f'{cpus}-CPU guest: 3 real graphics draws, independent pixel oracle and exported PPM match native driver byte-for-byte', flush=True)
print(reference.stdout.decode(), end='')
