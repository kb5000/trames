#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh the Linux 6.12 number inventory and probe all undispatched entries.

Input numbers are retained from the published 0.4 audit. Matching a dispatch arm
establishes an implementation entry, not conformance of every argument/flag.
"""
import argparse
import collections
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'docs/validation'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', default='compatibility-current', help='Output basename; historical reports are kept when explicitly named')
args = parser.parse_args()
assert re.fullmatch(r'[a-z0-9][a-z0-9.-]*', args.prefix), 'invalid report prefix'
found = {}
for source in ('crates/trames-core/src/process.rs', 'crates/trames-linux/src/state.rs'):
    lines = (ROOT / source).read_text().splitlines()
    function = next(i for i, line in enumerate(lines) if line.startswith('    fn syscall('))
    start = next(i for i in range(function, len(lines)) if lines[i] == '        match number {')
    for i in range(start + 1, len(lines)):
        line = lines[i]
        if line == '        }':
            break
        if not re.match(r'^ {12}\d', line):
            continue
        arm = line.strip()
        next_line = i + 1
        while '=>' not in arm:
            arm += ' ' + lines[next_line].strip()
            next_line += 1
        arm = arm.split('=>')[0].split(' if ')[0]
        for branch in arm.split('|'):
            branch = branch.strip()
            if re.fullmatch(r'\d+', branch):
                numbers = [int(branch)]
            elif re.fullmatch(r'\d+\.\.=\d+', branch):
                low, high = map(int, branch.split('..='))
                numbers = range(low, high + 1)
            else:
                continue
            for number in numbers:
                found.setdefault(number, f'{source}:{i + 1}')
rows = list(csv.DictReader((OUT / 'compatibility-0.4-syscalls.csv').open()))
notes = {
    19: 'Bounded shared event counter; semaphore/nonblocking/cloexec, overflow and fault preflight.',
    20: 'Guest epoll open description; no host epoll descriptor.',
    21: 'Level-triggered and oneshot watches with dup identity, weak lifetime and fairness; nested/edge/exclusive modes explicitly unsupported.',
    22: 'Cooperative epoll wait with timeout, temporary signal mask and EINTR restoration.',
    25: 'Selected descriptor/status operations plus POSIX GETLK/SETLK/SETLKW byte-range locks; TGID ownership, split/merge, close and deadlock detection. No OFD locks.',
    33: 'Persistent named FIFO and regular inode creation; umask and permissions. Device creation explicitly unsupported.',
    56: 'Selected openat flags, trailing-slash semantics and named FIFO blocking/nonblocking handshake; signal interruption/restart.',
    72: 'Virtual pselect6 bounded by 256 descriptors.',
    73: 'Virtual ppoll bounded by 256 descriptors.',
    85: 'Guest timerfd, realtime/monotonic/virtual boottime; nonblocking/cloexec. Alarm clocks unsupported.',
    86: 'Relative/absolute timerfd arm/disarm, periodic expiration count and old-value preflight. CANCEL_ON_SET explicitly unsupported.',
    87: 'Timerfd interval/remaining time; shared open-description state.',
    199: 'Bounded AF_UNIX stream/datagram/seqpacket pairs; dup/fork, shutdown, peek/trunc and fault preflight. No named endpoints or ancillary FD passing.',
    134: 'Selected rt_sigaction flags, virtual signal frames and synchronous CPU-fault delivery/context recovery; SA_ONSTACK uses the ordinary stack while the alternate stack is disabled. No sigaltstack registration.',
    221: 'Exec preserves UID/GID/groups and set-ID results; auxv uses the same credentials.',
    441: 'Timespec epoll wait; not dispatched by the pinned QEMU 10.0.13 oracle, independently checked by M6 fixtures.',
}
for row in rows:
    number = int(row['number'])
    row['baseline_status'] = row['status']
    row['dispatch_source'] = found.get(number, '')
    row['probe_return'] = ''
    if number in found:
        row['status'] = 'implemented_subset'
    if number in notes:
        row['notes'] = notes[number]
missing = [int(row['number']) for row in rows if not row['dispatch_source']]
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
with tempfile.TemporaryDirectory(prefix='trames-syscalls-') as directory:
    tmp = Path(directory)
    assembly = ['.text', '.global _start', '_start:', 'adrp x20, results', 'add x20,x20,:lo12:results']
    for i, number in enumerate(missing):
        assembly += [f'mov x8,#{number}', *[f'mov x{register},#0' for register in range(6)], 'svc #0', f'str x0,[x20,#{i * 8}]']
    assembly += ['mov x0,#1', 'mov x1,x20', f'mov x2,#{len(missing) * 8}', 'mov x8,#64', 'svc #0', 'mov x0,#0', 'mov x8,#93', 'svc #0', '.data', '.balign 8', 'results:', f'.skip {len(missing) * 8}']
    (tmp / 'probe.S').write_text('\n'.join(assembly) + '\n')
    subprocess.run([os.getenv('AARCH64_AS', 'aarch64-linux-gnu-as'), '-o', str(tmp / 'probe.o'), str(tmp / 'probe.S')], check=True)
    subprocess.run([os.getenv('AARCH64_LD', 'aarch64-linux-gnu-ld'), '-o', str(tmp / 'probe'), str(tmp / 'probe.o')], check=True)
    result = subprocess.run([runner, '--memory', str(tmp / 'probe')], capture_output=True, timeout=30)
    assert result.returncode == 0 and not result.stderr, (result.returncode, result.stderr)
    returns = struct.unpack('<' + 'q' * len(missing), result.stdout)
    assert all(value == -38 for value in returns), list(zip(missing, returns))
    for row in rows:
        if not row['dispatch_source']:
            row['probe_return'] = -38
with (OUT / (args.prefix + '-syscalls.csv')).open('w', newline='') as output:
    writer = csv.DictWriter(output, fieldnames=list(rows[0]), lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
metadata = {
    'scope': 'Linux v6.12 native AArch64 numbered entries; entry presence is not full ABI conformance',
    'entries': len(rows),
    'statuses': dict(collections.Counter(row['status'] for row in rows)),
    'undispatched_probes_per_backend': len(missing),
    'backends': ['cached'],
    'unhandled_return': -38,
    'source_sha256': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in (
        'crates/trames-core/src/process.rs', 'crates/trames-core/src/process_events.rs',
        'crates/trames-linux/src/state.rs', 'crates/trames-linux/src/network.rs',
        'crates/trames-vfs/src/events.rs', 'crates/trames-vfs/src/ipc.rs', 'crates/trames-vfs/src/locks.rs',
        'crates/trames-vfs/src/lib.rs', 'crates/trames-vfs/src/pseudo.rs', 'crates/trames-vfs/src/storage.rs',
        'crates/trames-memory/src/lib.rs')},
}
(OUT / (args.prefix + '-syscalls.json')).write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps({key: value for key, value in metadata.items() if key != 'source_sha256'}))
