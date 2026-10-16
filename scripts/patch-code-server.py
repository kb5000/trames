#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checked, opt-in slow-runtime connection timeouts for original Code 1.141.0.

Preserves authentication/signature checks and default 10/20/30-second behavior.
Keep upstream MIT notices and the original npm archive. No minified bundles are
checked into this repository. Verify every input before writing either file.
"""
import argparse
import hashlib
from pathlib import Path

FRONT = 'lib/vscode/out/vs/code/browser/workbench/workbench.js'
SERVER = 'lib/vscode/out/server-main.js'
HASHES = {
    FRONT: '5adda0751dea27657788e05f4ddd2afdcd34f8cd08369c8c814e6fc8ebd1af3f',
    SERVER: '176c23fecfbca3db8b0f58b00e89a9cb898cac61d59d1295d8339f9dbd7dff47',
}

def timeout(expression, default):
    return (f'(Number.isInteger({expression})&&{expression}>={default}'
            f'&&{expression}<=600000?{expression}:{default}') + ')'

BROWSER_TIMEOUT = 'globalThis.TRAMES_REMOTE_HANDSHAKE_TIMEOUT_MS'
SERVER_TIMEOUT = 'Number(process.env.CODE_SERVER_HANDSHAKE_TIMEOUT_MS)'
PATCHES = {
    FRONT: [
        ('kts(r,Cts(t,aBn(1e4)))', 'kts(r,Cts(t,aBn(' + timeout(BROWSER_TIMEOUT, 10000) + ')))'),
        ('var wts=30*1e3;', 'var wts=' + timeout(BROWSER_TIMEOUT, 30000) + ';'),
        ('if(e>=2e4&&t>=2e4&&i>=2e4&&!this._loadEstimator.hasHighLoad())',
         'const tramesAckTimeout=' + timeout(BROWSER_TIMEOUT, 20000) + ';if(e>=tramesAckTimeout&&t>=tramesAckTimeout&&i>=tramesAckTimeout&&!this._loadEstimator.hasHighLoad())'),
        ('Math.max(2e4-e,2e4-t,2e4-i,500)', 'Math.max(tramesAckTimeout-e,tramesAckTimeout-t,tramesAckTimeout-i,500)'),
        ('if(e>=2e4&&t>=2e4&&!this._loadEstimator.hasHighLoad())',
         'const tramesKeepAliveTimeout=' + timeout(BROWSER_TIMEOUT, 20000) + ';if(e>=tramesKeepAliveTimeout&&t>=tramesKeepAliveTimeout&&!this._loadEstimator.hasHighLoad())'),
        ('g("The remote extension host took longer than 60s to send its ready message.")},6e4)',
         'g("The remote extension host exceeded its ready-message timeout.")},' + timeout(BROWSER_TIMEOUT, 60000) + ')'),
    ],
    SERVER: [
        ('if(e>=2e4&&n>=2e4&&o>=2e4&&!this._loadEstimator.hasHighLoad())',
         'const tramesAckTimeout=' + timeout(SERVER_TIMEOUT, 20000) + ';if(e>=tramesAckTimeout&&n>=tramesAckTimeout&&o>=tramesAckTimeout&&!this._loadEstimator.hasHighLoad())'),
        ('Math.max(2e4-e,2e4-n,2e4-o,500)', 'Math.max(tramesAckTimeout-e,tramesAckTimeout-n,tramesAckTimeout-o,500)'),
        ('if(e>=2e4&&n>=2e4&&!this._loadEstimator.hasHighLoad())',
         'const tramesKeepAliveTimeout=' + timeout(SERVER_TIMEOUT, 20000) + ';if(e>=tramesKeepAliveTimeout&&n>=tramesKeepAliveTimeout&&!this._loadEstimator.hasHighLoad())'),
    ],
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    args = parser.parse_args()
    prepared = []
    for filename, patches in PATCHES.items():
        path = args.package / filename
        data = path.read_bytes()
        # Recognize only our complete patch, never a partially modified bundle.
        restored = data
        for old, new in patches:
            restored = restored.replace(new.encode(), old.encode())
        if all(data.count(new.encode()) == 1 for _, new in patches) and hashlib.sha256(restored).hexdigest() == HASHES[filename]:
            prepared.append((path, data))
            continue
        if hashlib.sha256(data).hexdigest() != HASHES[filename]:
            raise SystemExit('Unsupported or modified bundle; refusing to patch: ' + filename)
        for old, new in patches:
            if data.count(old.encode()) != 1:
                raise SystemExit('Non-unique patch location: ' + filename)
            data = data.replace(old.encode(), new.encode())
        prepared.append((path, data))
    for path, data in prepared:
        if path.read_bytes() != data:
            path.write_bytes(data)
        print(path.name, hashlib.sha256(data).hexdigest())

if __name__ == '__main__':
    main()
