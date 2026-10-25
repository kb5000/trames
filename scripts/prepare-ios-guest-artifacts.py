#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Normalize verified graphics artifact layouts before building the iPad app."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

REQUIRED = ('libtrames_vulkan_icd.so', 'manifest.json', 'GPL-3.0.txt')


def prepare(root):
    root = Path(root)
    nested = root / 'vulkan-guest'
    if nested.is_symlink():
        raise ValueError('Symlink in graphics artifact')
    if nested.exists():
        if any((root / name).exists() for name in REQUIRED):
            raise ValueError('Ambiguous flat and nested graphics payloads')
        if not all((nested / name).is_file() for name in REQUIRED):
            raise ValueError('Incomplete nested graphics artifact')
        # The producer uploads target/vulkan-guest together with a sibling
        # source archive. upload-artifact consequently preserves both folders.
        # Other validated producers upload just the flat graphics directory.
        for item in nested.rglob('*'):
            if item.is_symlink():
                raise ValueError('Symlink in graphics artifact')
        for item in nested.iterdir():
            destination = root / item.name
            if destination.exists():
                raise ValueError('Graphics artifact destination already exists')
            shutil.move(str(item), destination)
        nested.rmdir()
    for name in REQUIRED:
        path = root / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing regular graphics payload: ' + name)
    record = json.loads((root / 'manifest.json').read_text())
    digest = hashlib.sha256()
    with (root / REQUIRED[0]).open('rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            digest.update(chunk)
    actual = digest.hexdigest()
    if actual != record['icd_sha256']:
        raise ValueError('Graphics payload checksum mismatch')
    paired = root / 'xwayland-native/xwayland-native-source.tar.gz'
    if paired.exists():
        destination = root / paired.name
        if paired.is_symlink() or not paired.is_file() or destination.exists():
            raise ValueError('Ambiguous paired Xwayland source')
        shutil.move(str(paired), destination)
        paired.parent.rmdir()
    print('Verified graphics layout and ICD checksum; paired sources retained')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--graphics', type=Path, default=Path('target/vulkan-guest'))
    prepare(parser.parse_args().graphics)
