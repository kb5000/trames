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
    payload = nested if nested.exists() else root
    if payload == nested:
        if any((root / name).exists() for name in REQUIRED):
            raise ValueError('Ambiguous flat and nested graphics payloads')
        for item in nested.rglob('*'):
            if item.is_symlink():
                raise ValueError('Symlink in graphics artifact')
    for name in REQUIRED:
        path = payload / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Missing regular graphics payload: ' + name)
    record = json.loads((payload / 'manifest.json').read_text())
    digest = hashlib.sha256()
    with (payload / REQUIRED[0]).open('rb') as stream:
        for chunk in iter(lambda: stream.read(65536), b''):
            digest.update(chunk)
    if digest.hexdigest() != record['icd_sha256']:
        raise ValueError('Graphics payload checksum mismatch')

    paired = root / 'xwayland-native/xwayland-native-source.tar.gz'
    source_folder = paired.parent if paired.exists() else None
    if source_folder is not None:
        destination = root / paired.name
        if (source_folder.is_symlink() or paired.is_symlink() or not paired.is_file()
                or set(source_folder.iterdir()) != {paired} or destination.exists()
                or (payload == nested and (nested / paired.name).exists())):
            raise ValueError('Ambiguous paired Xwayland source')
    # Validate every destination before changing the download. On macOS the
    # sibling source folder xwayland-native collides with Xwayland-native, the
    # actual executable. Retain the archive and remove that folder FIRST.
    items = list(nested.iterdir()) if payload == nested else []
    for item in items:
        destination = root / item.name
        if destination.exists() and not (
                source_folder is not None and destination.samefile(source_folder)):
            raise ValueError('Graphics artifact destination already exists: ' + item.name)
    if source_folder is not None:
        shutil.move(str(paired), root / paired.name)
        source_folder.rmdir()
    for item in items:
        shutil.move(str(item), root / item.name)
    if items:
        nested.rmdir()
    print('Verified graphics layout and ICD checksum; paired sources retained')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--graphics', type=Path, default=Path('target/vulkan-guest'))
    prepare(parser.parse_args().graphics)
