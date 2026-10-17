#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Copy the extension's expanded group setting into the unsigned containing app.

Xcode's generated plist does not propagate arbitrary INFOPLIST_KEY settings.
Only bundle metadata changes; compiled executables and guest payloads are intact.
"""
import argparse
from pathlib import Path
import plistlib


def finalize(app: Path) -> None:
    info_path = app / 'Info.plist'
    info = plistlib.loads(info_path.read_bytes())
    providers = list((app / 'PlugIns').glob('*.appex/Info.plist'))
    if len(providers) != 1:
        raise ValueError('Expected exactly one File Provider extension')
    extension = plistlib.loads(providers[0].read_bytes())
    metadata = extension.get('NSExtension', {})
    group = extension.get('TramesAppGroup')
    if not isinstance(group, str) or not group.startswith('group.') or '$' in group:
        raise ValueError('File Provider group must be expanded by the build')
    if metadata.get('NSExtensionPointIdentifier') != 'com.apple.fileprovider-nonui' or metadata.get('NSExtensionFileProviderDocumentGroup') != group:
        raise ValueError('Invalid File Provider shared-group metadata')
    if not extension.get('CFBundleIdentifier', '').startswith(info['CFBundleIdentifier'] + '.'):
        raise ValueError('Extension must belong to this app')
    if any(extension.get(key) != info[key] for key in ('CFBundleVersion', 'CFBundleShortVersionString')):
        raise ValueError('App and extension versions must match')
    if info.get('TramesAppGroup') not in (None, group):
        raise ValueError('Refusing to replace a different app group')
    if (app / '_CodeSignature').exists() or (providers[0].parent / '_CodeSignature').exists():
        raise ValueError('Finalize metadata before signing')
    info['TramesAppGroup'] = group
    info_path.write_bytes(plistlib.dumps(info, fmt=plistlib.FMT_BINARY, sort_keys=False))
    print('Finalized unsigned app File Provider group:', group)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('app', type=Path)
    finalize(parser.parse_args().app)
