#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fetch exact upstream archives and retain complete native dependency sources."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / 'scripts/vulkan-upstream.json').read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apple', action='store_true', help='also prepare pinned MoltenVK binaries and their source closure')
parser.add_argument('--source-build', action='store_true', help='prepare Apple source closure without unused prebuilt MoltenVK binaries')
args = parser.parse_args()
assert not args.source_build or args.apple, '--source-build requires --apple'
cache = ROOT / 'target/vulkan-upstream'
cache.mkdir(parents=True, exist_ok=True)
for name, record in manifest['archives'].items():
    if name == 'moltenvk-all.tar' and args.source_build:
        continue
    if name != 'headers.tar.gz' and not args.apple:
        continue
    archive = cache / name
    if not archive.exists():
        temporary = archive.with_suffix(archive.suffix + '.part')
        with urllib.request.urlopen(record['url'], timeout=120) as response, temporary.open('wb') as output:
            while chunk := response.read(1 << 20):
                output.write(chunk)
        temporary.replace(archive)
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == record['sha256'], ('upstream digest', name)
    directory = cache / name.split('.')[0]
    if not directory.exists():
        directory.mkdir()
        with tarfile.open(archive) as files:
            files.extractall(directory, filter='data')
    print('Verified', name, record['sha256'], flush=True)
if args.apple:
    output = ROOT / 'target/vulkan-guest'
    output.mkdir(parents=True, exist_ok=True)
    # Include the exact MoltenVK source plus every revision from its own
    # ExternalRevisions, rather than supplying binary links as GPL source.
    with tarfile.open(output / 'vulkan-native-upstream-source.tar.gz', 'w:gz') as package:
        for name, record in manifest['archives'].items():
            if name == 'moltenvk-all.tar':
                continue
            package.add(cache / name, arcname='upstream/' + name)
        package.add(ROOT / 'scripts/vulkan-upstream.json', arcname='vulkan-upstream.json')
        package.add(ROOT / 'scripts/prepare-vulkan.py', arcname='prepare-vulkan.py')
        package.add(ROOT / 'scripts/build-moltenvk.py', arcname='scripts/build-moltenvk.py')
        package.add(ROOT / 'patches/moltenvk', arcname='patches/moltenvk')
        package.add(ROOT / 'LICENSE', arcname='TRAMES-GPL-3.0.txt')
    notices = output / 'Notices'
    notices.mkdir(exist_ok=True)
    for name in manifest['archives']:
        if name == 'headers.tar.gz' or name == 'moltenvk-all.tar':
            continue
        directory = cache / name.split('.')[0]
        project = next(path for path in directory.iterdir() if path.is_dir())
        for path in project.iterdir():
            if path.is_file() and path.name.lower().startswith(('license', 'copying', 'copyright', 'notice')):
                shutil.copyfile(path, notices / (name.removesuffix('.tar.gz') + '-' + path.name + '.txt'))
    shutil.copyfile(ROOT / 'LICENSE', notices / 'Trames-Vulkan-GPL-3.0.txt')
