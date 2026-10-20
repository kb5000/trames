#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Retain the original new Wayland dependency notices in the iPad resources.

Run after cargo fetch/build; --check verifies the committed bundle. No package
is downloaded or substituted by this script. Cargo.lock fixes source versions.
"""
import argparse
import os
from pathlib import Path
import tomllib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
registry = Path(os.environ.get('CARGO_HOME', str(Path.home() / '.cargo'))) / 'registry/src'
lock = tomllib.loads((ROOT / 'Cargo.lock').read_text())
names = ('downcast-rs', 'errno', 'linux-raw-sys', 'quick-xml', 'rustix', 'smallvec',
         'wayland-backend', 'wayland-protocols', 'wayland-scanner', 'wayland-server',
         'wayland-sys', 'cc', 'find-msvc-tools', 'pkg-config', 'shlex',
         'gethostname', 'x11rb', 'x11rb-protocol')
parts = [b'Trames native Wayland: upstream notices\n\n'
         b'Trames modifications are GPL-3.0-or-later. The following original\n'
         b'components retain their upstream terms. MIT is selected where offered.\n'
         b'Build-only and Linux-only dependencies are included for completeness.\n']
# Include the complete locked XIM dependency closure, including target/build
# dependencies, while preserving the existing protocol/keymap notice bundle.
def resolve(selector):
    pieces=selector.split()
    choices=[p for p in lock['package'] if p['name']==pieces[0] and
             (len(pieces)==1 or p['version']==pieces[1])]
    assert len(choices)==1, selector
    return choices[0]
selected={}
pending=['xim']
while pending:
    record=resolve(pending.pop())
    key=(record['name'],record['version'])
    if key in selected:continue
    selected[key]=record
    pending.extend(record.get('dependencies',[]))
for name in names:
    record=resolve(name);selected[(name,record['version'])]=record
ordered=[resolve(name) for name in names]
original={(r['name'],r['version']) for r in ordered}
ordered.extend(record for key,record in sorted(selected.items()) if key not in original)
packages = {}
for record in ordered:
    name=record['name']
    label = name + '-' + record['version']
    if name == 'wayland-backend':
        directory = ROOT / 'vendor/wayland-backend'
    else:
        matches = list(registry.glob('*/' + label))
        if len(matches) != 1:
            raise SystemExit('Missing or ambiguous Cargo package: ' + label)
        directory = matches[0]
    packages[name] = directory
    notices = sorted(p for p in directory.iterdir() if p.is_file() and
                     p.name.upper().startswith(('LICENSE', 'COPYRIGHT', 'COPYING', 'NOTICE')))
    if not notices and name in ('xim-parser','xim-ctext'):
        # These crate archives omit the workspace-root MIT license. Retain the
        # original LICENSE from both exact .cargo_vcs_info commits; identical
        # bytes: parser 7300de35, ctext c4b89780 (Riey/xim-rs).
        import hashlib
        license=ROOT/'crates/trames-wayland/data/COPYING.xim-rs'
        assert hashlib.sha256(license.read_bytes()).hexdigest()=='efa1224931b98468f5005f77285457f8d2eab0a77a15941aa10abbab1ae9a74e'
        notices=[license]
    assert notices, label
    for path in notices:
        parts.extend([b'\n\n===== ' + label.encode() + b'/' + path.name.encode() + b' =====\n\n', path.read_bytes()])

protocols = [packages['wayland-server'] / 'wayland.xml']
protocols += [packages['wayland-protocols'] / 'protocols' / p for p in (
    'stable/xdg-shell/xdg-shell.xml',
    'unstable/text-input/text-input-unstable-v1.xml',
    'unstable/text-input/text-input-unstable-v3.xml',
    'staging/xwayland-shell/xwayland-shell-v1.xml')]
for path in protocols:
    notice = ET.parse(path).getroot().findtext('copyright')
    assert notice and 'Copyright' in notice, path
    notice = '\n'.join(line.rstrip() for line in notice.split('\n'))
    parts.extend([b'\n\n===== Protocol ' + path.name.encode() + b' =====\n', notice.encode()])
parts.extend([b'\n\n===== xkeyboard-config US keymap =====\n\n',
              (ROOT / 'crates/trames-wayland/data/COPYING.xkeyboard-config').read_bytes(), b'\n'])
result = b''.join(parts)
output = ROOT / 'apps/ios/Trames/Notices/Wayland-native-dependencies-NOTICES.txt'
if args.check:
    assert output.read_bytes() == result, 'Wayland dependency notices differ from the locked sources'
else:
    output.write_bytes(result)
print('Verified original Wayland dependency/protocol/keymap notices:', len(result), 'bytes')
