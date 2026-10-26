#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a workflow job's shell steps locally, with per-step evidence and resume.

For the Linux runtime job use the native ARM64 Ubuntu CI VM. GitHub checkout,
toolchain/cache and upload actions are supplied by local setup/storage. Shell
commands come from the workflow, rather than a second handwritten test list.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def load_workflow(path):
    try:
        import yaml
        return yaml.safe_load(path.read_text())
    except ImportError:
        # macOS includes Ruby/Psych; the Linux VM installs python3-yaml.
        return json.loads(subprocess.check_output([
            'ruby', '-r', 'yaml', '-r', 'json', '-e',
            'puts YAML.load_file(ARGV[0]).to_json', str(path)], text=True))


def expand(value, root):
    value = str(value).replace('${{ github.workspace }}', str(root))
    if '${{' in value:
        raise ValueError('Unresolved workflow expression: ' + value)
    return value


def dependencies(root, text):
    """Conservative source closure for literal repository paths/imports."""
    pending = [(root, text)]
    found = set()
    while pending:
        parent, source = pending.pop()
        names = set(re.findall(r'(?:scripts|tests|crates|\.github)/[\w./*+@-]+', source))
        try:
            tree = ast.parse(source)
            names.update(n.value for n in ast.walk(tree)
                         if isinstance(n, ast.Constant) and isinstance(n.value, str)
                         and 0 < len(n.value) < 240
                         and re.fullmatch(r'[\w./*+@-]+', n.value))
            for node in ast.walk(tree):
                if isinstance(node, ast.ImportFrom) and node.module:
                    names.add(node.module.replace('.', '/') + '.py')
                elif isinstance(node, ast.Import):
                    names.update(n.name.replace('.', '/') + '.py' for n in node.names)
        except SyntaxError:
            pass
        for name in names:
            for base in (root, parent, root / 'tests/fixtures/bin'):
                for path in base.glob(name) if '*' in name and not name.startswith('/') else [base / name]:
                    try:
                        path = path.resolve()
                        path.relative_to(root)
                    except (ValueError, OSError):
                        continue
                    try:
                        if not path.is_file() or path in found:
                            continue
                    except OSError:
                        continue
                    found.add(path)
                    if path.suffix in ('.py', '.sh', '.rs', '.c', '.h') and path.stat().st_size < 1024 * 1024:
                        pending.append((path.parent, path.read_text(errors='replace')))
    return found


def file_map(root, paths):
    return {str(p.relative_to(root)): digest(p) for p in sorted(paths) if p.is_file()}


def rust_inputs(root):
    paths = {root / p for p in ('Cargo.toml', 'Cargo.lock', 'rust-toolchain.toml', '.gitmodules', 'runtime-version.json', 'runtime/Cargo.toml', 'runtime/Cargo.lock')}
    for folder in ('crates', 'runtime/crates', 'runtime/vendor', '.cargo'):
        paths.update(p for p in (root / folder).rglob('*') if p.suffix in
                     ('.rs', '.toml', '.lock', '.c', '.h', '.py', '.json', '.S'))
    return file_map(root, paths)


def input_key(root, step, env, context):
    body = step['run']
    paths = dependencies(root, body)
    commands = body + '\n' + '\n'.join(
        p.read_text(errors='replace') for p in paths
        if p.suffix in ('.py', '.sh') and p.stat().st_size < 1024 * 1024)
    native = 'cargo ' in commands
    inputs = file_map(root, paths)
    if native:
        inputs.update(rust_inputs(root))
        inputs.update(file_map(root, {p for p in (root / 'tests/fixtures').rglob('*')
                                      if p.suffix in ('.c', '.h', '.S', '.rs')}))
    else:
        for name in ('target/release/trames-run', 'target/qemu-oracle/bin/qemu-aarch64'):
            path = root / name
            inputs[name] = digest(path) if path.is_file() else None
        if any('differential/' in str(p) for p in paths):
            inputs.update(file_map(root, set((root / 'tests/differential').rglob('*.py'))
                                   | set((root / 'tests/fixtures').glob('*.S'))))
        if any('libtrames_ffi' in p.read_text(errors='replace') for p in paths if p.suffix == '.py'):
            for suffix in ('so', 'dylib'):
                p = root / ('target/release/libtrames_ffi.' + suffix)
                inputs[str(p.relative_to(root))] = digest(p) if p.is_file() else None
    # Fixture names are often assembled at runtime. Include fixture sources in
    # addition to the explicitly referenced compiled inputs. Unrelated binaries
    # built later in this job must not invalidate already passed comparisons.
    if re.search(r'python3 tests/integration/', body):
        inputs.update(file_map(root, {p for p in (root / 'tests/fixtures').rglob('*')
                                      if p.suffix in ('.c', '.h', '.S', '.rs')}))
    if re.search(r'python3 tests/(integration|differential)/', body):
        oracle = shutil.which(env.get('QEMU_AARCH64', 'qemu-aarch64'), path=env.get('PATH'))
        inputs['resolved-qemu'] = (oracle + ':' + digest(Path(oracle))) if oracle else None
    for value in env.values():
        path = Path(value)
        if '\n' not in value and len(value) < 240 and path.is_absolute() and path.is_file():
            inputs['env-file:' + value] = digest(path)
    # Cargo build successes also require their main products to still exist.
    if 'cargo build' in commands:
        target = 'aarch64-unknown-linux-musl/' if '--target aarch64-unknown-linux-musl' in commands else ''
        profile = 'release' if '--release' in commands else 'debug'
        targets = {target}
        if 'cargo build -p trames-cli --release' in commands:
            targets.add('')
        for target in targets:
            for name in ('trames-run', 'trames-mac-run', 'libtrames_ffi.so', 'libtrames_ffi.dylib', 'libtrames_ffi.a'):
                p = root / ('target/' + target + profile + '/' + name)
                inputs[str(p.relative_to(root))] = digest(p) if p.is_file() else None
    payload = dict(run=body, env=env, context=context, inputs=inputs)
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode()).hexdigest(), inputs


def write_json(path, data):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(data, indent=2, sort_keys=True) + '\n')
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--workflow', default='.github/workflows/linux.yml')
    parser.add_argument('--job', default='runtime')
    parser.add_argument('--output', type=Path, default=Path('target/local-ci/runtime'))
    parser.add_argument('--source-state', type=Path)
    parser.add_argument('--native-arm64', action='store_true')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--start', type=int, default=1, help='First shell step, one based')
    parser.add_argument('--stop', type=int, help='Last shell step, inclusive')
    args = parser.parse_args()
    root = args.root.resolve()
    job = load_workflow(root / args.workflow)['jobs'][args.job]
    steps = [dict(s) for s in job['steps'] if 'run' in s]
    if args.list:
        for i, step in enumerate(steps, 1):
            print(i, step.get('name', step['run'].splitlines()[0]))
        return 0
    if args.native_arm64 and (sys.platform != 'linux' or platform.machine() not in ('aarch64', 'arm64')):
        parser.error('--native-arm64 requires a native ARM64 Linux host')
    if any(s.get('if') for s in steps):
        parser.error('This job has conditional shell steps; select an unconditional job')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.update({k: expand(v, root) for k, v in job.get('env', {}).items()})
    environment.update(GITHUB_WORKSPACE=str(root), TRAMES_FULL_CHECK='true', CI='true')
    adaptations = []
    if args.native_arm64:
        environment['CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_RUNNER'] = 'env'
        environment['TRAMES_RUNNER'] = str(root / 'target/release/trames-run')
        adaptations = ['Native ARM64 CLI and musl test executables; pinned QEMU remains the instruction oracle.',
                       'ThreadSanitizer target follows the ARM64 Linux host instead of hosted x86_64.']
        for step in steps:
            step['run'] = step['run'].replace('--target x86_64-unknown-linux-gnu', '--target aarch64-unknown-linux-gnu')
            if 'cargo build -p trames-cli --target aarch64-unknown-linux-musl' in step['run']:
                step['run'] += '\ncargo build -p trames-cli --release --locked\n'
            if 'python3 tests/integration/vulkan-texture.py' in step['run']:
                # ARM64 lavapipe's fixed-point AOS fast path can exceed this
                # fixture's pixel bound. Keep the original pixel assertions and
                # use the driver's full-precision sampler for local comparisons.
                step['env'] = dict(step.get('env', {}), GALLIVM_PERF='no_aos_sampling')
                adaptations.append('ARM64 lavapipe uses full-precision sampling (GALLIVM_PERF=no_aos_sampling).')
            if step.get('env', {}).get('RUSTFLAGS') == '-Zsanitizer=thread':
                # The pinned Rust ARM64 builtins otherwise call TSan from an
                # outlined atomic during TSan's own pre-main initialization.
                # Use LLVM's compiler-runtime primitives; std stays instrumented.
                runtime = environment.get('LLVM_COMPILER_RT_LIB',
                    '/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-aarch64.a')
                if not Path(runtime).is_file():
                    parser.error('ARM64 TSan needs libclang-rt-18-dev or LLVM_COMPILER_RT_LIB')
                step['env'] = dict(step['env'], LLVM_COMPILER_RT_LIB=runtime,
                                   CARGO_TARGET_DIR='target/thread-sanitizer-c-builtins')
                step['run'] = step['run'].replace('cargo test -Zbuild-std ',
                    'cargo test -Zbuild-std -Zbuild-std-features=backtrace,panic-unwind,compiler-builtins-c ')
                adaptations.append('ARM64 TSan uses LLVM compiler-runtime builtins; Rust std remains instrumented.')
    path_file, env_file = out / 'github-path', out / 'github-env'
    path_file.write_text('')
    env_file.write_text('')
    environment.update(GITHUB_PATH=str(path_file), GITHUB_ENV=str(env_file))
    context = {'platform': platform.platform(), 'machine': platform.machine(),
               'rustc': subprocess.check_output(['rustc', '-vV'], text=True),
               'packages': hashlib.sha256(subprocess.check_output(['dpkg-query', '-W'])).hexdigest()
               if sys.platform == 'linux' else platform.mac_ver()[0]}
    report = dict(workflow=args.workflow, job=args.job, context=context, adaptations=adaptations,
                  runner_sha256=digest(Path(__file__)), completed=False, steps=[])
    report['local_actions'] = {
        'checkout': 'Isolated local source snapshot; source-state records HEAD and actual file hashes.',
        'toolchain': 'Provisioned pinned Rust toolchain, components and targets; rustc recorded above.',
        'cache': 'Persistent local target directories.',
        'upload-artifact': 'Logs and generated artifacts remain in the local checkout/output directory.',
    }
    if args.source_state:
        report['source'] = json.loads(args.source_state.read_text())
    for i, step in enumerate(steps, 1):
        setup = any(token in step['run'] for token in ('GITHUB_PATH', 'GITHUB_ENV'))
        if (i < args.start and not setup) or (args.stop and i > args.stop):
            report['steps'].append(dict(index=i, status='not-run', name=step.get('name')))
            continue
        name = step.get('name', step['run'].splitlines()[0])
        local_env = environment.copy()
        step_env = {k: expand(v, root) for k, v in step.get('env', {}).items()}
        local_env.update(step_env)
        if args.native_arm64 and step_env.get('TRAMES_RUNNER'):
            local_env['TRAMES_RUNNER'] = str(root / 'target/release/trames-run')
        relevant_env = {k: v for k, v in local_env.items() if k == 'PATH'
                        or k.startswith(('TRAMES_', 'CARGO_', 'RUST', 'QEMU_', 'AARCH64_', 'LLVM_'))
                        or k in step_env}
        key, inputs = input_key(root, step, relevant_env, context)
        record_path = out / ('step-%02d.json' % i)
        prior = json.loads(record_path.read_text()) if args.resume and record_path.is_file() else {}
        # Setup snippets must replay PATH/ENV changes even when cached products exist.
        can_reuse = not any(v in step['run'] for v in ('GITHUB_PATH', 'GITHUB_ENV', 'apt-get'))
        if can_reuse and prior.get('status') == 'passed' and prior.get('input_key') == key:
            print('[%02d/%02d] REUSE %s' % (i, len(steps), name), flush=True)
            report['steps'].append(dict(prior, reused=True))
            continue
        print('[%02d/%02d] START %s' % (i, len(steps), name), flush=True)
        script = out / ('step-%02d.sh' % i)
        script.write_text('set -euo pipefail\n' + step['run'] + '\n')
        began = time.time()
        with (out / ('step-%02d.log' % i)).open('w') as log:
            result = subprocess.run(['bash', str(script)], cwd=root, env=local_env,
                                    stdout=log, stderr=subprocess.STDOUT)
        key, inputs = input_key(root, step, relevant_env, context)
        record = dict(index=i, name=name, input_key=key, inputs=inputs,
                      status='passed' if result.returncode == 0 else 'failed',
                      exit_code=result.returncode, seconds=time.time() - began)
        write_json(record_path, record)
        report['steps'].append(record)
        write_json(out / 'summary.json', report)
        print('[%02d/%02d] %s %.2fs %s' % (i, len(steps), record['status'].upper(), record['seconds'], name), flush=True)
        if result.returncode:
            print((out / ('step-%02d.log' % i)).read_text(errors='replace')[-6000:], flush=True)
            return result.returncode
        for path in reversed(path_file.read_text().splitlines()):
            if path and path not in environment['PATH'].split(os.pathsep):
                environment['PATH'] = path + os.pathsep + environment['PATH']
        for line in env_file.read_text().splitlines():
            if '=' in line:
                k, v = line.split('=', 1)
                environment[k] = v
    report['completed'] = all(s.get('status') == 'passed' for s in report['steps'])
    write_json(out / 'summary.json', report)
    return 0


if __name__ == '__main__':
    sys.exit(main())
