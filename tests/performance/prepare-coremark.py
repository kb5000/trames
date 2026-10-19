#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a pinned upstream CoreMark with original signed Alpine guest GCC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests/integration'))
import m6

COMMIT = '1f483d5b8316753a742cbf5590caf5bd0a4e4777'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
m6.CACHE.mkdir(parents=True, exist_ok=True)
m6.repository(out / 'repository', ('gcc', 'musl-dev', 'make'))
packages = [p for p in sorted((out / 'repository').rglob('*.apk'))
            if not p.name.startswith(('isl25-', 'dash-', 'yash-'))]
archive = m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'
if not archive.exists():
    with urllib.request.urlopen('https://dl-cdn.alpinelinux.org/alpine/v3.22/releases/aarch64/' + archive.name, timeout=120) as response:
        archive.write_bytes(response.read())
source = out / 'coremark-source.tar.gz'
with urllib.request.urlopen('https://codeload.github.com/eembc/coremark/tar.gz/' + COMMIT, timeout=120) as response:
    source.write_bytes(response.read())
base = [str(a.runner.resolve()), '--storage', str(out / 'store'), '--network', 'off',
        '--cpus', '1', '--max-instructions', '50000000000']
imports = [v for f in packages for v in ('--file', '/tmp/' + f.name + '=' + str(f))]
install = subprocess.run([*base, '--rootfs', str(archive), *imports,
    '--file', '/tmp/coremark-source.tar.gz=' + str(source), '--guest', '/sbin/apk',
    '--repositories-file', '/dev/null', '--no-progress', 'add',
    *['/tmp/' + f.name for f in packages]], capture_output=True, timeout=300)
(out / 'install.log').write_bytes(install.stdout + install.stderr)
assert install.returncode == 0, install.stderr[-2000:]
compile = subprocess.run([*base, '--export', '/root/coremark/coremark.exe=' + str(out / 'coremark-alpine'),
    '--guest', '/bin/sh', '-ec', 'mkdir -p /root/coremark; cd /root/coremark; tar xzf /tmp/coremark-source.tar.gz --strip-components=1; make PORT_DIR=linux compile'], capture_output=True, timeout=400)
(out / 'compile.log').write_bytes(compile.stdout + compile.stderr)
assert compile.returncode == 0, (compile.stdout[-2000:], compile.stderr[-2000:])
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
report = dict(upstream_commit=COMMIT, upstream_archive_sha256=sha(source),
              rootfs_sha256=sha(archive), packages_sha256={f.name: sha(f) for f in packages},
              elf_sha256=sha(out / 'coremark-alpine'), runner_sha256=sha(a.runner),
              guest_command='make PORT_DIR=linux compile')
(out / 'inputs.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2), flush=True)
