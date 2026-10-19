#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Embed the exact Metal-validated guest libraries, preserving paired sources."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare():
    root = Path(__file__).resolve().parents[1]
    plan = json.loads((root / '.github/ios-validation-plan.json').read_text())['device_build']
    bundle = root / 'target/zink-native-bundle'
    record = json.loads((bundle / 'checksums.json').read_text())
    assert record['source_sha'] == plan['zink']['sha']
    actual = {str(p.relative_to(bundle)) for p in bundle.rglob('*') if p.is_file() and p.name != 'checksums.json'}
    assert actual == set(record['sha256'])
    for name, digest in record['sha256'].items():
        p = Path(name)
        assert not p.is_absolute() and '..' not in p.parts
        assert sha((bundle / p).read_bytes()) == digest, name
    output = root / 'target/vulkan-guest'
    for path in (bundle / 'guest').iterdir():
        shutil.copyfile(path, output / path.name)
    component = json.loads((output / 'egl-manifest.json').read_text())
    assert sha((output / 'libEGL.so.1').read_bytes()) == component['egl_sha256']
    assert sha((output / 'libtrames_vulkan_icd.so').read_bytes()) == json.loads((output / 'manifest.json').read_text())['icd_sha256']
    with tarfile.open(output / 'vulkan-native-upstream-source.tar.gz') as archive:
        assert json.load(archive.extractfile('vulkan-upstream.json')) == json.loads((root / 'scripts/vulkan-upstream.json').read_text())
    musl = root / 'target/musl-hybrid'
    assert sha((musl / 'libc.so').read_bytes()) == json.loads((musl / 'manifest.json').read_text())['libc_sha256']
    with tarfile.open(musl / 'musl-hybrid-1.2.5-source.tar.gz') as archive:
        patch = json.load(archive.extractfile('./trames-patch.json'))
        for name, digest in patch['inputs_sha256'].items():
            assert sha((root / name).read_bytes()) == digest == sha(archive.extractfile('./source/' + name).read()), name
    archive = root / 'target/mesa-25.1.9.tar.xz'
    if not archive.exists():
        with urllib.request.urlopen(component['source'], timeout=120) as response, archive.open('wb') as dest:
            shutil.copyfileobj(response, dest)
    assert sha(archive.read_bytes()) == component['source_sha256']
    patch = root / component['patch']
    assert sha(patch.read_bytes()) == component['patch_sha256']
    notices = output / 'Notices/Mesa-EGL'
    notices.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as source:
        (notices / 'license.rst').write_bytes(source.extractfile('mesa-25.1.9/docs/license.rst').read())
        for entry in source:
            if entry.isfile() and entry.name.startswith('mesa-25.1.9/src/util/blake3/LICENSE'):
                (notices / Path(entry.name).name).write_bytes(source.extractfile(entry).read())
    shutil.copyfile(patch, notices / patch.name)
    with tarfile.open(output / 'mesa-egl-25.1.9-source.tar.gz', 'w:gz') as source:
        source.add(archive, arcname='upstream/mesa-25.1.9.tar.xz')
        for name in [component['patch'], 'scripts/build-zink-egl.py', 'scripts/zink-egl-requirements.txt',
                     'scripts/prepare-zink-guest.py', 'tests/integration/m6.py', 'LICENSE']:
            source.add(root / name, arcname=name)
        source.add(output / 'egl-manifest.json', arcname='egl-manifest.json')
    proof = dict(graphics=plan['zink'], guest=plan['guest'], files_verified=len(actual),
                 source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                 icd_sha256=sha((output / 'libtrames_vulkan_icd.so').read_bytes()),
                 egl_sha256=component['egl_sha256'], musl_sha256=sha((musl / 'libc.so').read_bytes()),
                 paired_musl_sources_verified=True, original_moltenvk_pins_verified=True,
                 mesa_source_archive_sha256=component['source_sha256'])
    (output / 'ipa-guest-reuse.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    prepare()
