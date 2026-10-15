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
    90: 'Guest capget v1/v2/v3, version negotiation including NULL-data query, namespace-local task lookup and per-thread E/P/I sets; bounded output preflight.',
    91: 'Guest capset self/own TID only; effective/permitted/inheritable subset and bounding/SETPCAP rules, ambient reduction, high-bit masking. No cross-task capset or file capability xattrs.',
    95: 'waitid P_ALL/P_PID/P_PGID, EXITED/STOPPED/CONTINUED, NOHANG/NOWAIT and WALL; namespace-local siginfo IDs, fault preflight, nohang zero info. No P_PIDFD/clone-signal variants; rusage is currently zero as with wait4.',
    167: 'Task-local KEEPCAPS, SECUREBITS/locks, CAPBSET_READ/DROP, irreversible NO_NEW_PRIVS and ambient query/raise/lower/clear; enforced capset/UID/exec semantics. GET/SET_DUMPABLE 0/1 shares with CLONE_VM, copies on fork, resets on exec/credential transitions and protects sensitive proc reads/links. Retains NAME and thread-group CHILD_SUBREAPER; no core-file writer, full ptrace or seccomp, other options EINVAL.',
    129: 'PID namespace visibility for process/group/broadcast kill; guest credentials, namespace init handler protection, ancestor KILL/STOP, exported signal sender translation. Standard pending signals coalesce.',
    130: 'tkill namespace-local TID lookup, credentials and init protection.',
    131: 'tgkill namespace-local TGID/TID checks, credentials and init protection.',
    260: 'wait4 namespace-local PID/PGID selection and returned child ID, exit/stop/continue and WALL; current rusage fields remain zero.',
    39: 'Guest umount2: normal EBUSY for live capabilities/submounts, MNT_DETACH subtree lifetime, UMOUNT_NOFOLLOW. CAP_SYS_ADMIN authorization; force/expire unsupported.',
    40: 'Guest private mount namespace: tmpfs, proc/sysfs/shared devpts, shared cgroup2 domain hierarchy, bind/rbind, selected remount readonly/nosuid/nodev/noexec attributes, MS_PRIVATE/REC, MS_RELATIME and MS_SILENT. Bound mount lifetime and tmpfs memory/inode quotas. Cgroup membership/lifecycle without resource controllers, delegation or cgroup namespaces. No shared/slave propagation, block FS/overlayfs, new devpts instances or modern mount API.',
    41: 'Guest pivot_root between mount roots with directory/ancestry validation, old-root attachment and shared-namespace root/cwd updates. Selected private mount model.',
    51: 'Guest chroot fs.root with CAP_SYS_CHROOT, absolute symlink/dotdot handling, fork/CLONE_FS inheritance and unshare separation. Retains cwd/dirfd capabilities as Linux does. Mount/inode fs.root capability retained; complete cross-process proc root magic-link views still pending.',
    97: 'CLONE_NEWUTS, CLONE_NEWNS, CLONE_NEWPID, CLONE_FS and CLONE_FILES; transactional flag/quota checks. NEWNS implies private fs context; NEWPID changes only future children and rejects multithreaded callers/repeated unshare. Network/user/IPC namespaces remain unsupported.',
    161: 'Set current guest UTS hostname, 0..64 bytes; CAP_SYS_ADMIN in initial guest user namespace; no user namespaces yet.',
    162: 'Set current guest UTS domain name, 0..64 bytes; same CAP_SYS_ADMIN authorization as sethostname.',
    268: 'Join UTS/mount or select a PID namespace for future children through retained namespace fd; type 0 or matching CLONE flag. PID setns accepts current/descendant namespaces only. Mount setns rejects shared fs contexts; other namespace types unsupported.',
    220: 'Fork/threads/resource sharing plus CLONE_NEWUTS/CLONE_NEWNS/CLONE_NEWPID. Hierarchical IDs, namespace init lifecycle, orphan reparenting, parent/child TID writes and nonleader exec. Reject NEWNS with CLONE_FS and NEWPID with THREAD/PARENT; no NEWNET/NEWUSER/NEWIPC or clone3.',
    29: 'Selected tty/network ioctls and FIONBIO, including standard streams, dup-shared status flags, output/input backpressure and descriptor-only CLOEXEC.',
    208: 'Selected socket options including TCP_KEEPIDLE/KEEPINTVL/KEEPCNT with Linux bounds. Native sockets use translated host options; virtual TCP uses the idle interval and retains interval/count as compatibility hints, not Linux probe/expiry timing.',
    31: 'IOPRIO_WHO_PROCESS query for current/existing virtual task; default CLASS_NONE/data 0. No host PID, priority setter, process-group or user query.',
    132: '24-byte AArch64 stack_t, SS_DISABLE/AUTODISARM, actual SA_ONSTACK delivery, nesting and ucontext restore; inherited by fork, cleared by exec and non-vfork CLONE_VM. 2048-byte ABI minimum; delivery still needs room for the signal frame.',
    283: 'Joined multicore checkpoint plus host GLOBAL/registered PRIVATE_EXPEDITED fences; QUERY/GET_REGISTRATIONS. Registration inherited by fork/shared by CLONE_VM, cleared by exec. No SYNC_CORE/RSEQ modes.',
    163: 'Query selected process resource limits; RLIMIT_FSIZE soft/hard is independent of storage quota and defaults to unlimited.',
    164: 'Update RLIMIT_FSIZE with hard-limit privilege checks and real write/truncate EFBIG/SIGXFSZ enforcement; other resources retain fixed selected limits.',
    261: 'Current process prlimit64 old/new preflight, including overlapping pointers; RLIMIT_FSIZE updates inherited by fork/shared by threads/preserved by exec. Other-PID changes and broader resource controls unsupported.',
    71: 'Bounded file-to-output sendfile; optional source offset, partial output and source-position updates. No zero-copy implementation.',
    118: 'Virtual SCHED_OTHER parameter set; priority must be zero, target lifetime and owner permissions checked.',
    119: 'SCHED_OTHER only; realtime and other scheduling policies explicitly unsupported.',
    120: 'Query current virtual SCHED_OTHER policy and validate target task.',
    121: 'Return virtual sched_priority zero with output preflight.',
    122: 'Per-task virtual CPU affinity, intersected with 1..32 configured CPUs and enforced by dispatch; reject empty effective masks and check owner/target permissions.',
    123: 'Return the task affinity as one 64-bit word; require at least eight output bytes. Masks are inherited across clone and preserved across exec.',
    125: 'Linux policy priority maxima; accepting a query does not implement realtime scheduling.',
    126: 'Linux policy priority minima; accepting a query does not implement realtime scheduling.',
    168: 'Assigned virtual CPU and NUMA node 0, with both output pointers checked before writing. Virtual affinity does not pin host cores.',
    213: 'Validated regular-file readahead hint; no host prefetch guarantee.',
    216: 'Shrink, grow, MAYMOVE and non-overlapping FIXED page-table remap; retain COW/shared backing. No zero-size alias duplication or DONTUNMAP.',
    222: 'Private and coherent shared file/anonymous mappings, checked maximum write rights and live EOF/truncation SIGBUS. Private anonymous PROT_NONE uses sparse reservations; mprotect commits within the page budget. Reservation metadata capped at 65536 intervals.',
    223: 'Validate regular-file offset/length and six Linux advice values; prefetch/eviction remains an optional hint.',
    227: 'Flush shared backing with MS_SYNC/ASYNC/INVALIDATE argument checks; no host eviction or pinning model.',
    232: 'Return residency of allocated valid page objects; inaccessible uncommitted reservations and pages beyond file EOF report zero. Absent mappings fail; output capped to the selected 512 MiB range.',
    233: 'Selected memory access/huge-page/dump hints and real private-page DONTNEED discard. No DONTFORK, FREE, REMOVE or paging model.',
    267: 'Sync the virtual filesystem selected by a valid guest descriptor.',
    279: 'Anonymous memfd with CLOEXEC/ALLOW_SEALING and four classic fcntl seals; shared mapping coherence, lifetime and write-seal busy checks. Aggregate RAM budget 128 MiB.',
    281: 'Inode-based execveat: absolute/dirfd-relative paths, AT_EMPTY_PATH, final AT_SYMLINK_NOFOLLOW, O_PATH executable references, original ELF/shebang loader, credentials and private close-on-exec table. FD script names use /dev/fd; inaccessible CLOEXEC script paths return ENOENT. Mount noexec/nosuid are enforced; full security/ptrace and cross-process proc root views remain incomplete.',
    221: 'Inode-based execve including proc fd/exe magic links, unlinked regular files and anonymous memfd. Retain executed inode after CLOEXEC closure, separate AT_EXECFN from argv[0], reuse existing ELF/interpreter/thread teardown and credential checks.',
    285: 'Bounded same-runtime regular-file copy with offset-pointer preflight, partial counts, overlap and append checks; no zero-copy path.',
    286: 'preadv2 selected RWF_DSYNC/SYNC flags, full ARM64 position and -1 current-position semantics; other flags unsupported.',
    287: 'pwritev2 selected RWF_DSYNC/SYNC and position semantics; RWF_APPEND/NOWAIT/HIPRI and newer flags unsupported.',
    437: 'Versioned open_how with size/tail validation and resolve=0. Every nonzero path-containment requirement is explicitly rejected, not ignored.',
    452: 'Mode changes with empty-path/fd and no-follow handling; symlink chmod explicitly unsupported.',
    69: 'Positioned vectored reads, bounded to 1024 iovecs and 1 MiB per transfer; full native ARM64 offset, destination preflight and unchanged shared file position.',
    70: 'Positioned vectored writes with all-input preflight, bounded transfer and existing Linux O_APPEND behavior; shared file position unchanged.',
    116: 'Session-scoped bounded runtime log; privileged READ/READ_ALL/READ_CLEAR/CLEAR and buffer size queries, independent /dev/kmsg cursors. No host kernel log or console control.',
    291: 'BASIC_STATS and virtual MNT_ID, symlink/no-follow/empty-path handling, device numbers and timestamps. No invented birth time, DIO alignment or newer optional statx fields.',
    436: 'Bounded descriptor range, CLOSE_RANGE_CLOEXEC and scheduler-mediated CLOSE_RANGE_UNSHARE with CLONE_FILES separation.',
    19: 'Bounded shared event counter; semaphore/nonblocking/cloexec, overflow and fault preflight.',
    20: 'Guest epoll open description; no host epoll descriptor.',
    21: 'Level-triggered and oneshot watches with dup identity, weak lifetime and fairness; nested/edge/exclusive modes explicitly unsupported.',
    22: 'Cooperative epoll wait with timeout, temporary signal mask and EINTR restoration.',
    25: 'Selected descriptor/status operations plus POSIX GETLK/SETLK/SETLKW byte-range locks; TGID ownership, split/merge, close and deadlock detection. Classic memfd GET_SEALS/ADD_SEALS, busy-map checks and shrink/grow/write enforcement. No OFD locks.',
    33: 'Persistent named FIFO and regular inode creation; umask and permissions. Device creation explicitly unsupported.',
    56: 'Selected openat flags including O_PATH metadata/exec references, trailing-slash semantics and named FIFO blocking/nonblocking handshake; signal interruption/restart.',
    72: 'Virtual pselect6 bounded by 256 descriptors.',
    73: 'Virtual ppoll bounded by 256 descriptors.',
    85: 'Guest timerfd, realtime/monotonic/virtual boottime; nonblocking/cloexec. Alarm clocks unsupported.',
    86: 'Relative/absolute timerfd arm/disarm, periodic expiration count and old-value preflight. CANCEL_ON_SET explicitly unsupported.',
    87: 'Timerfd interval/remaining time; shared open-description state.',
    199: 'Bounded AF_UNIX stream/datagram/seqpacket pairs; dup/fork, shutdown, peek/trunc and fault preflight. No named endpoints or ancillary FD passing.',
    134: 'Selected rt_sigaction flags including musl sign-extended SA_RESETHAND, actual alternate-stack delivery, nested virtual signal frames and synchronous CPU-fault context recovery. No SVE/SME signal context or full realtime signal queue.',
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
        'crates/trames-core/src/process.rs', 'crates/trames-core/src/process_caps.rs', 'crates/trames-vfs/src/capabilities.rs', 'crates/trames-vfs/src/cgroup.rs', 'crates/trames-core/src/process_pid.rs', 'crates/trames-core/src/process_events.rs', 'crates/trames-core/src/process_compat.rs',
        'crates/trames-linux/src/state.rs', 'crates/trames-linux/src/network.rs', 'crates/trames-linux/src/file_calls.rs',
        'crates/trames-vfs/src/events.rs', 'crates/trames-vfs/src/ipc.rs', 'crates/trames-vfs/src/locks.rs',
        'crates/trames-vfs/src/lib.rs', 'crates/trames-vfs/src/pseudo.rs', 'crates/trames-vfs/src/storage.rs',
        'crates/trames-vfs/src/kernel_log.rs', 'crates/trames-vfs/src/syscall_names.rs',
        'crates/trames-memory/src/lib.rs', 'crates/trames-memory/src/backing.rs',
        'crates/trames-memory/src/reservations.rs', 'crates/trames-core/src/lib.rs',
        'crates/trames-memory/src/cpu_memory.rs', 'crates/trames-memory/src/parallel.rs', 'crates/trames-core/src/workers.rs',
        'crates/trames-net/src/native.rs', 'crates/trames-net/src/virtualnet/sockets.rs',
        'crates/trames-vfs/src/mapped.rs', 'crates/trames-vfs/src/memfd.rs', 'crates/trames-vfs/src/executable.rs',
        'crates/trames-vfs/src/linux_namespace.rs', 'crates/trames-vfs/src/pid_namespace.rs', 'crates/trames-vfs/src/mounts.rs', 'crates/trames-net/src/direct_dns.rs',
        'crates/trames-net/src/interfaces.rs', 'crates/trames-net/src/native_icmp.rs',
        'crates/trames-vfs/src/namespace.rs', 'crates/trames-vfs/src/credentials.rs')},
}
(OUT / (args.prefix + '-syscalls.json')).write_text(json.dumps(metadata, indent=2) + '\n')
print(json.dumps({key: value for key, value in metadata.items() if key != 'source_sha256'}))
