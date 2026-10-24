#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise conditional editor commits on an initially paused production session.

The input Alpine store is copied. No guest instruction may execute before the
explicit resume; transfers, conflict detection and idempotent receipts must work
while paused. This is an ABI/integrity check, not an iPad performance claim.
"""
import argparse
import ctypes as C
import hashlib
import json
import resource
from pathlib import Path
import shutil
import tempfile
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--library', type=Path, required=True)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--large-mib', type=int, default=0, help='Also stream a large conditional replacement and export')
a = p.parse_args()
assert 0 <= a.large_mib <= 4096
api = C.CDLL(str(a.library.resolve()))
for name in ('open',):
    fn = getattr(api, 'trames_session_' + name)
    fn.argtypes = [C.c_char_p, C.c_uint32]
    fn.restype = C.c_uint64
for name in ('transfer', 'commit_edit'):
    fn = getattr(api, 'trames_session_' + name)
    fn.argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
api.trames_session_commit_edit.restype = C.c_uint64
for name in ('output', 'status'):
    getattr(api, 'trames_session_' + name).argtypes = [C.c_uint64, C.c_void_p, C.c_uint32]
api.trames_session_input.argtypes = [C.c_uint64, C.c_char_p, C.c_uint32]
api.trames_session_pause.argtypes = [C.c_uint64, C.c_uint32]
api.trames_session_close.argtypes = [C.c_uint64]
api.trames_last_error.argtypes = [C.c_void_p, C.c_uint32]


def error():
    output = C.create_string_buffer(65536)
    size = api.trames_last_error(output, len(output))
    return output.raw[:max(0, size)].decode()


with tempfile.TemporaryDirectory(prefix='trames-writeback-') as temporary:
    root = Path(temporary)
    store = root / 'store'
    shutil.copytree(a.store, store)
    options = dict(store=str(store), volume_total=16 << 30, volume_available=16 << 30,
                   network='off', initially_paused=True, store_owner_lock=str(root / 'owner.lock'),
                   memory_budget_bytes=512 << 20, process_memory_budget_bytes=512 << 20)
    encoded = json.dumps(options).encode()
    handle = api.trames_session_open(encoded, len(encoded))
    assert handle, error()

    def receive(kind):
        output = C.create_string_buffer(65536)
        size = getattr(api, 'trames_session_' + kind)(handle, output, len(output))
        assert size >= 0, error()
        return output.raw[:size]

    def call(name, options):
        encoded = json.dumps(options).encode()
        return getattr(api, 'trames_session_' + name)(handle, encoded, len(encoded))

    try:
        deadline = time.monotonic() + 30
        while json.loads(receive('status'))['phase'] == 'starting':
            assert time.monotonic() < deadline, 'Session startup timed out'
            time.sleep(.01)
        original, edited = b'original editor document\n', b'edited through FileProvider\n'
        source = root / 'payload'
        source.write_bytes(original)
        assert call('transfer', dict(guest='/tmp/writeback-transfer-check', host=str(source),
                                     export=False, replace=True)) == 0, error()
        guest = '/root/fileprovider-abi-check.txt'
        request = dict(guest=guest, host=str(source), export=False, replace=True)
        assert call('transfer', request) == 0, error()
        sha = lambda value: hashlib.sha256(value).hexdigest()
        request['expected'] = dict(inode=0, sha256=sha(original), replacement_sha256=sha(original))
        original_inode = call('commit_edit', request)
        assert original_inode, error()
        request['expected'] = dict(inode=original_inode, sha256=sha(original), replacement_sha256=sha(edited))
        source.write_bytes(edited)
        started = time.monotonic()
        edited_inode = call('commit_edit', request)
        commit_ms = (time.monotonic() - started) * 1000
        assert edited_inode and edited_inode != original_inode, error()
        assert call('commit_edit', request) == edited_inode, error()
        source.write_bytes(b'stale editor copy\n')
        request['expected']['replacement_sha256'] = sha(source.read_bytes())
        assert call('commit_edit', request) == 0
        assert 'changed' in error(), error()
        exported = root / 'exported'
        assert call('transfer', dict(guest=guest, host=str(exported), export=True)) == 0, error()
        assert exported.read_bytes() == edited
        large_result = {}
        if a.large_mib:
            before_rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            large_source, large_export = root / 'large-source', root / 'large-export'
            block = b'large FileProvider streaming fixture\n'.ljust(1 << 20, b'x')
            expected_hash = hashlib.sha256()
            with large_source.open('wb') as stream:
                for _ in range(a.large_mib):
                    stream.write(block)
                    expected_hash.update(block)
            large_request = dict(request)
            large_request['host'] = str(large_source)
            large_request['expected'] = dict(inode=edited_inode, sha256=sha(edited),
                                             replacement_sha256=expected_hash.hexdigest())
            started = time.monotonic()
            assert call('commit_edit', large_request), error()
            large_ms = (time.monotonic() - started) * 1000
            assert call('transfer', dict(guest=guest, host=str(large_export), export=True)) == 0, error()
            actual_hash = hashlib.sha256()
            with large_export.open('rb') as stream:
                while chunk := stream.read(1 << 20):
                    actual_hash.update(chunk)
            assert actual_hash.digest() == expected_hash.digest()
            assert large_export.stat().st_size == a.large_mib << 20
            # Restore the small document for the guest resume assertion below.
            source.write_bytes(edited)
            assert call('transfer', dict(guest=guest, host=str(source), export=False, replace=True)) == 0, error()
            after_rss = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            rss_unit = 1 if __import__('sys').platform == 'darwin' else 1024
            large_result = dict(large_mib=a.large_mib, large_commit_ms=round(large_ms, 3),
                                peak_rss_growth_mib=round((after_rss-before_rss)*rss_unit/(1 << 20), 2),
                                large_export_checksum=True)
        assert receive('output') == b'', 'Guest executed before resume'
        command = b"cat /root/fileprovider-abi-check.txt; printf 'WRITEBACK_RESUMED\\n'\n"
        assert api.trames_session_input(handle, command, len(command)) == len(command), error()
        assert receive('output') == b'', 'Queued terminal input ran while paused'
        assert api.trames_session_pause(handle, 0) == 0, error()
        deadline = time.monotonic() + 30
        output = bytearray()
        while b'\r\nWRITEBACK_RESUMED\r\n' not in output:
            assert time.monotonic() < deadline, (receive('status'), bytes(output[-1000:]))
            output.extend(receive('output'))
            time.sleep(.01)
        assert b'edited through FileProvider\r\n' in output, bytes(output[-1000:])
        print(json.dumps(dict(paused_commit=True, conflict_preserved=True,
                              retry_receipt=True, resume_readback=True,
                              host_commit_ms=round(commit_ms, 3), **large_result)))
    finally:
        api.trames_session_close(handle)
