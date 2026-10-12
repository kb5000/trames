#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Persistent sessions, real disk files and independently executed Linux ABI."""
import hashlib, os, select, shlex, shutil, struct, subprocess, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
RUNNER = str(ROOT/'target/debug/trames-run')
BUSYBOX = str(ROOT/'target/busybox-build/busybox')
def run(args, cwd=None, expected=0):
    p = subprocess.run(args, cwd=cwd, capture_output=True, timeout=45)
    if p.returncode != expected:
        raise AssertionError((args, p.returncode, p.stdout, p.stderr, expected))
    return p.returncode, p.stdout, p.stderr
def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(65536), b''): h.update(chunk)
    return h.digest()
def main():
    qemu = shutil.which(os.getenv('QEMU_AARCH64', 'qemu-aarch64'))
    if not qemu: raise RuntimeError('QEMU unavailable')
    comparisons = 0
    with tempfile.TemporaryDirectory(prefix='trames-storage-') as folder:
        folder = Path(folder); ref = folder/'reference'; ref.mkdir()
        probe = str(ROOT/'tests/fixtures/bin/storage')
        actual = run([RUNNER, '--storage', str(folder/'abi'), probe])
        reference = run([qemu, '-cpu', 'cortex-a53', probe], ref)
        if actual != reference: raise AssertionError(('storage ABI', actual, reference))
        comparisons += 1
        bin_ = folder/'bin'; bin_.mkdir()
        for name in ['echo', 'cat', 'cp', 'mv', 'ln', 'chmod', 'touch', 'sync', 'mkdir', 'rm', 'dd']:
            p = bin_/name; p.write_text('#!/bin/sh\nexec '+shlex.join([qemu, '-cpu', 'cortex-a53', BUSYBOX, name])+' "$@"\n'); p.chmod(0o755)
        script = 'set -e; mkdir data; printf "alpha\\nbeta\\n" >data/a; cp data/a data/copy; ln data/copy data/link; mv data/a data/moved; chmod 600 data/link; touch data/touched; dd bs=4 count=3 <data/link >data/dd 2>/dev/null; cat data/link data/moved data/dd; rm data/copy; sync; test -f data/touched && echo files-ok'
        store = folder/'shell'
        actual = run([RUNNER, '--storage', str(store), '--install-busybox', BUSYBOX, 'ash', '-c', script])
        p = subprocess.run([qemu, '-cpu', 'cortex-a53', BUSYBOX, 'ash', '-c', script], cwd=ref,
                           env={'PATH': str(bin_), 'LANG': 'C'}, capture_output=True, timeout=45)
        reference = p.returncode, p.stdout, p.stderr
        if actual != reference: raise AssertionError(('storage applets', actual, reference))
        comparisons += 1
        def shell(script, *options, store=store):
            return run([RUNNER, '--storage', str(store), *options, '--install-busybox', BUSYBOX, 'ash', '-c', script])
        if shell('cat /data/link; test ! -f /data/copy; test -f /data/touched')[1] != b'alpha\nbeta\n':
            raise AssertionError('restart lost file lifecycle')
        if shell('echo gamma >>/data/link; cat /data/link')[1] != b'alpha\nbeta\ngamma\n':
            raise AssertionError('persistent append')
        source = folder/'host-source'; pattern = bytes(range(256)) * 256
        with source.open('wb') as f:
            for _ in range(320): f.write(pattern)
            f.write(b'\0\xfftail')
        exported = folder/'exported'
        shell('mkdir /results; cat /imports/big >/results/copied; sync', '--max-instructions', '100000000',
              '--storage-budget', '64MiB', '--file', f'/imports/big={source}', '--export', f'/results/copied={exported}')
        if digest(source) != digest(exported) or exported.stat().st_size != source.stat().st_size:
            raise AssertionError('stream import/export changed bytes')
        info = shell('true', '--storage-info')[2]
        if b'budget_bytes: 67108864' not in info: raise AssertionError(('budget not retained', info))
        # Default CLI backend is persistent in the explicitly selected host cwd.
        default = folder/'default'; default.mkdir()
        run([RUNNER, '--install-busybox', BUSYBOX, 'ash', '-c', 'echo default >saved'], default)
        result = run([RUNNER, '--install-busybox', BUSYBOX, 'ash', '-c', 'cat saved'], default)
        if result[1] != b'default\n': raise AssertionError('default disk mode did not persist')
        manifest = (default/'.trames/storage/manifest').read_bytes()
        if struct.unpack_from('<Q', manifest, 12)[0] != 16 * 1024**3: raise AssertionError('new-store default is not 16 GiB')
        automatic = folder/'auto'
        info = shell('true', '--storage-budget', 'auto', '--storage-info', store=automatic)[2].decode()
        manifest = (automatic/'manifest').read_bytes(); budget = struct.unpack_from('<Q', manifest, 12)[0]
        if f'budget_bytes: {budget}' not in info or f'volume_total_bytes: Some({budget})' not in info:
            raise AssertionError(('auto capacity mismatch', info, budget))
        # A guest path cannot access this neighboring host file.
        outside = folder/'host-only'; outside.write_bytes(b'host-only-secret')
        p = run([RUNNER, '--storage', str(store), '--install-busybox', BUSYBOX, 'cat', str(outside)], expected=1)
        if p[1] or outside.read_bytes() != b'host-only-secret': raise AssertionError('guest escaped private namespace')
        # Exports create new destinations and never silently overwrite existing files.
        exported.write_bytes(b'keep-existing')
        run([RUNNER, '--storage', str(store), '--export', f'/results/copied={exported}', BUSYBOX, 'true'], expected=125)
        if exported.read_bytes() != b'keep-existing': raise AssertionError('export overwrote existing destination')
        invalid_export = folder/'invalid-export'
        run([RUNNER, '--storage', str(store), '--export', f'/missing={invalid_export}', BUSYBOX, 'true'], expected=125)
        if invalid_export.exists(): raise AssertionError('invalid guest export created a host file')
        p = run([RUNNER, '--storage', str(store), '--max-instructions', '1000000', '--install-busybox', BUSYBOX,
                 'ash', '-c', 'echo before-fault >/before-fault; while :; do :; done'], expected=125)
        if b'instruction limit exceeded' not in p[2] or shell('cat /before-fault')[1] != b'before-fault\n':
            raise AssertionError('runtime-error teardown did not retain prior writes')
        crash = folder/'crash'
        command = 'echo durable >/saved; echo orphan >/orphan; exec 3<>/orphan; rm /orphan; sync; echo READY; sleep 1000'
        p = subprocess.Popen([RUNNER, '--storage', str(crash), '--install-busybox', BUSYBOX, 'ash', '-c', command],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            if not select.select([p.stdout], [], [], 15)[0] or p.stdout.readline() != b'READY\n':
                raise AssertionError('crash fixture never reached synced state')
            objects_before = len(list((crash/'objects').iterdir()))
            run([RUNNER, '--storage', str(crash), BUSYBOX, 'true'], expected=125)
            p.kill(); p.wait(timeout=5)
        finally:
            if p.poll() is None: p.kill(); p.wait(timeout=5)
            p.stdout.close(); p.stderr.close()
        if shell('cat /saved; test ! -f /orphan', store=crash)[1] != b'durable\n': raise AssertionError('synced state lost after SIGKILL')
        if len(list((crash/'objects').iterdir())) != objects_before - 1: raise AssertionError('orphan not reclaimed on recovery')
    print(f'Storage integration passed: {comparisons} QEMU comparisons; restart/append, >20 MiB streamed binary copy, default/auto budgets, host scope, export preflight/refusal, runtime-error teardown, exclusive lock and SIGKILL recovery')
if __name__ == '__main__': main()
