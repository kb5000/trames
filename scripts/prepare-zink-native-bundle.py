#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Carry exact Linux-built guest binaries and original Mesa into native Metal CI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--verify', action='store_true')
p.add_argument('--wayland', action='store_true', help='include exact Wayland EGL and image-copy clients')
a = p.parse_args()
out = ROOT / 'target/zink-native-bundle'
manifest = out / 'checksums.json'
sha = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
if a.verify:
    data = json.loads(manifest.read_text())
    assert data['source_sha'] == sha, 'Guest bundle/source mismatch'
    assert data['source_sha'] == os.environ.get('GITHUB_SHA', sha)
    actual = {str(f.relative_to(out)) for f in out.rglob('*') if f.is_file() and f != manifest}
    assert actual == set(data['sha256'])
    for name, expected in data['sha256'].items():
        path = Path(name)
        assert not path.is_absolute() and '..' not in path.parts
        assert hashlib.sha256((out / path).read_bytes()).hexdigest() == expected, name
    (ROOT / 'target/vulkan-guest').mkdir(parents=True, exist_ok=True)
    (ROOT / 'target/zink-inputs').mkdir(parents=True, exist_ok=True)
    for f in (out / 'guest').iterdir():
        shutil.copyfile(f, ROOT / 'target/vulkan-guest' / f.name)
    shutil.copyfile(out / 'bin/zink-egl', ROOT / 'target/zink-inputs/zink-egl')
    for name in ('zink-wayland', 'vulkan-copy'):
        if (out / 'bin' / name).is_file():
            shutil.copyfile(out / 'bin' / name, ROOT / 'target/zink-inputs' / name)
    # Guest ELF execute bits do not need to survive an artifact ZIP roundtrip:
    # trames-run imports --exec-file and sets the guest executable metadata.
    print('Verified exact guest ICD, original Mesa store, clients and shaders:', sha)
else:
    assert (out / 'store/manifest').is_file(), 'Run integration with --snapshot-store first'
    for folder in ('guest', 'bin', 'shaders'):
        (out / folder).mkdir(exist_ok=True)
    for name in ('libtrames_vulkan_icd.so', 'trames.json', 'manifest.json'):
        shutil.copyfile(ROOT / 'target/vulkan-guest' / name, out / 'guest' / name)
    shutil.copyfile(ROOT / 'target/zink-inputs/zink-egl', out / 'bin/zink-egl')
    if a.wayland:
        for name in ('zink-wayland', 'vulkan-copy'):
            shutil.copyfile(ROOT / 'target/zink-inputs' / name, out / 'bin' / name)
    if not a.wayland:
        shutil.copyfile(ROOT / 'tests/fixtures/bin/vulkan-resources.spv', out / 'shaders/vulkan-resources.spv')
    shutil.copyfile(ROOT / 'target/zink-inputs/inputs.json', out / 'upstream-inputs.json')
    data = dict(source_sha=sha, sha256={str(f.relative_to(out)): hashlib.sha256(f.read_bytes()).hexdigest()
                for f in sorted(out.rglob('*')) if f.is_file() and f != manifest})
    manifest.write_text(json.dumps(data, indent=2) + '\n')
    print('Prepared source-bound native GPU bundle:', len(data['sha256']), 'files')
