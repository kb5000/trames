#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise a real Linux development workspace on a native shared directory.

Use an isolated output directory, an Alpine ARM64 minirootfs, and a native tools
tree containing gcc, musl-dev, binutils, git, python3 and sqlite. The tools tree's
/usr and /lib are mounted read-only; sources, libraries and outputs are writable
shared files. No existing user store is used.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--rootfs', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    shared = out / 'shared'
    shared.mkdir()
    fixture = Path(__file__).resolve().parents[1] / 'fixtures/hostfs-workspace.c'
    shutil.copyfile(fixture, shared / 'workspace.c')
    (shared / 'plugin.c').write_text('int answer(void) { return 42; }\n')
    (shared / 'script').write_text('#!/bin/sh -e\nprintf "SCRIPT_OK:%s\\n" "$1"\n')
    (shared / 'script').chmod(0o755)
    (shared / 'module.py').write_text('answer = 42\n')
    (shared / 'python-script').write_text('#!/usr/bin/env python3\nimport module\nassert module.answer == 42\nprint("PYTHON_SCRIPT_OK")\n')
    (shared / 'python-script').chmod(0o755)
    base = [str(args.runner.resolve()), '--storage', str(out / 'store'),
            '--no-windows', '--no-pty', '--network', 'off', '--cpus', '4', '--timeout', '300']
    base += ['--mount-ro', '/usr=' + str((args.tools / 'usr').resolve()),
             '--mount-ro', '/lib=' + str((args.tools / 'lib').resolve())]
    records = []

    def run(name, command, marker, readonly=False, setup=False):
        invocation = base + (['--rootfs', str(args.rootfs.resolve())] if setup else [])
        invocation += ['--mount-ro' if readonly else '--mount', '/work=' + str(shared)] + command
        result = subprocess.run(invocation, capture_output=True, timeout=330)
        row = {'case': name, 'exit': result.returncode,
               'stdout': result.stdout.decode(errors='replace'),
               'stderr': result.stderr.decode(errors='replace')}
        records.append(row)
        (out / 'result.json').write_text(json.dumps({'complete': False, 'records': records}, indent=2) + '\n')
        print(json.dumps(row), flush=True)
        assert result.returncode == 0 and marker in row['stdout'], row

    run('build', ['/bin/sh', '-ec',
        'cd /work; cc -Wall -Wextra -Werror -O2 -fPIC -shared plugin.c -o libwork.so; '
        'cp /lib/ld-musl-aarch64.so.1 ld.so; '
        "cc -Wall -Wextra -Werror -O2 workspace.c -ldl -Wl,--dynamic-linker=/work/ld.so -o dynamic; "
        'cc -Wall -Wextra -Werror -O2 -DSTATIC_TEST -static workspace.c -o static; printf BUILD_OK'], 'BUILD_OK', setup=True)
    run('static', ['/work/static'], 'WORKSPACE_OK')
    run('dynamic_and_dlopen', ['/work/dynamic'], 'WORKSPACE_OK')
    run('readonly_cow_execution', ['/work/dynamic', 'readonly'], 'WORKSPACE_OK', readonly=True)
    run('initial_script', ['/work/script', 'argument'], 'SCRIPT_OK:argument')
    run('initial_env_python_script', ['/work/python-script'], 'PYTHON_SCRIPT_OK')
    run('guest_exec_script_and_symlink', ['/bin/sh', '-ec',
        'cd /work; ln -s dynamic linked; ./linked; exec ./script nested'], 'SCRIPT_OK:nested')
    run('git', ['/bin/sh', '-ec',
        'cd /work; git init -q repo; cd repo; git config user.name kb5000; '
        'git config user.email 19368660+kb5000@users.noreply.github.com; '
        'cp ../workspace.c .; git add workspace.c; git -c commit.gpgsign=false commit -qm fixture; '
        'git gc; test -z "$(git status --porcelain)"; printf GIT_OK'], 'GIT_OK')
    run('python_sqlite_ctypes', ['/bin/sh', '-ec',
        'cd /work; python3 -c \'import module, ctypes, sqlite3; '
        'assert module.answer == 42; assert ctypes.CDLL("/work/libwork.so").answer() == 42; '
        'c=sqlite3.connect("/work/data.db"); c.execute("create table t (n integer)"); '
        'c.execute("insert into t values (42)"); c.commit(); '
        'assert c.execute("select n from t").fetchone() == (42,); c.close(); print("PYTHON_SQLITE_OK")\''], 'PYTHON_SQLITE_OK')
    run('execute_permission', ['/bin/sh', '-ec',
        'cd /work; cp dynamic denied; chmod 600 denied; '
        'if ./denied; then exit 1; fi; printf PERMISSION_OK'], 'PERMISSION_OK')
    run('noexec_mount', ['/bin/sh', '-ec',
        'mount -o remount,noexec /work; if /work/dynamic; then exit 1; fi; printf NOEXEC_OK'], 'NOEXEC_OK')
    (out / 'result.json').write_text(json.dumps({'complete': True, 'records': records}, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
