#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Manual CoreMark benchmark and Linux host PC sampling; no timing CI gates.

Build upstream CoreMark with PORT_DIR=linux, then supply its ELF and optionally
an existing guest store for the dynamically linked Alpine build. Sampling uses
an optimized runner built with CARGO_PROFILE_RELEASE_DEBUG=1, never a debug build.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def symbolize(binary, sample_file):
    rows = sample_file.read_text().splitlines()
    assert rows[0] == 'TRAMES-PC-SAMPLES-v1', rows[:4]
    counters = collections.Counter(int(row, 16) for row in rows[4:])
    addresses = sorted(set(counters)-{0})
    symbols = subprocess.run(['addr2line', '-a', '-f', '-C', '-i', '-e', str(binary)],
        input=''.join(hex(pc)+'\n' for pc in addresses), text=True, capture_output=True, check=True).stdout.splitlines()
    frames, current, pending = {}, None, []
    for line in [*symbols, '0xffffffffffffffff']:
        if re.fullmatch('0x[0-9a-fA-F]+', line):
            if current is not None:
                assert len(pending) % 2 == 0
                frames[current] = list(zip(pending[::2], pending[1::2]))
            current, pending = int(line, 16), []
        else:
            pending.append(line)
    categories, functions, lines = collections.Counter(), collections.Counter(), collections.Counter()
    for pc, count in counters.items():
        chain = frames.get(pc, [])
        names = '\n'.join(f for f, _ in chain)
        paths = '\n'.join(p for _, p in chain)
        if pc == 0:
            category = 'outside_executable'
        elif 'trames_memory::' in names or 'trames-memory/src/' in paths:
            category = 'guest_memory'
        elif 'execute_decoded' in names or 'execute_instruction' in names:
            category = 'instruction_execution'
        elif any(s in names for s in ('cached_slice', 'run_cached', 'cached::Cache::', 'cached::Instruction::')):
            category = 'dispatch_and_code_cache'
        elif 'trames_cpu::' in names:
            category = 'instruction_execution'
        elif 'trames_core::process::' in names:
            category = 'scheduler_and_kernel'
        else:
            category = 'other'
        categories[category] += count
        functions[chain[0][0] if chain else category] += count
        lines[(chain[0][0]+' '+chain[0][1]) if chain else category] += count
    total = sum(counters.values())
    assert total > 0, 'No CPU samples captured; check profiler target and timer setup'
    def ranked(values):
        return [{'name': k, 'samples': v, 'percent': 100*v/total} for k, v in values.most_common()]
    return {'method': 'Linux ITIMER_PROF saved user PC, requested 1 ms; actual tick resolution applies. Exclusive samples classified using inline source frames; whole process including startup/calibration. Not hardware cycle counters or iPad measurements.',
        'samples': total, 'unique_pcs': len(counters), 'header': rows[:4],
        'categories': ranked(categories), 'functions': ranked(functions)[:25], 'source_lines': ranked(lines)[:40]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--store', type=Path)
    parser.add_argument('--guest-program', default='/root/coremark/coremark.exe')
    parser.add_argument('--iterations', type=int, default=3000)
    parser.add_argument('--samples', type=int, default=3)
    parser.add_argument('--profile-runner', type=Path)
    parser.add_argument('--sampler', type=Path)
    parser.add_argument('--native-elf', type=Path, help='Same CoreMark source built for this host, default -O2')
    parser.add_argument('--upstream-commit', help='Optional provenance; caller must pin/verify its source')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.iterations > 0 and args.samples > 0
    assert bool(args.profile_runner) == bool(args.sampler)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    directory = args.output.parent/(args.output.stem+'-runs')
    directory.mkdir(exist_ok=True)
    report = {'scope': 'Unmodified CoreMark Linux/posix -O2, single thread; shared Linux host with warm caches. CPU architecture/compiler/thermal differences prevent an iPad speedup claim.',
        'host': {'platform': platform.platform(), 'machine': platform.machine(), 'logical_cpus': os.cpu_count()},
        'upstream_commit': args.upstream_commit,
        'runner_sha256': sha(args.runner), 'elf_sha256': sha(args.elf), 'iterations': args.iterations, 'runs': []}
    def save():
        args.output.write_text(json.dumps(report, indent=2)+'\n')
    def run(label, cpus=1, validation=False, profiled=False, runner=None):
        runner = (runner or args.runner).resolve()
        cmd = [str(runner), '--network', 'off', '--cpus', str(cpus), '--metrics', '--max-instructions', '20000000000']
        if args.store:
            cmd += ['--storage', str(args.store.resolve()), '--guest', args.guest_program]
        else:
            cmd += ['--memory', str(args.elf.resolve())]
        cmd += ['0x3415' if validation else '0', '0x3415' if validation else '0', '0x66', str(args.iterations), '7', '1', '2000']
        env = os.environ.copy()
        samples = directory/(label+'.pcs')
        if profiled:
            assert not samples.exists(), 'Use a new output directory for another profile'
            env['LD_PRELOAD'] = str(args.sampler.resolve())
            env['TRAMES_PC_SAMPLES'] = str(samples.resolve())
            env['TRAMES_PC_EXECUTABLE'] = str(runner)
        began = time.monotonic()
        result = subprocess.run(cmd, capture_output=True, env=env, timeout=300)
        elapsed = time.monotonic()-began
        stdout, stderr = result.stdout.decode(), result.stderr.decode()
        (directory/(label+'.stdout')).write_text(stdout)
        (directory/(label+'.stderr')).write_text(stderr)
        assert result.returncode == 0 and 'Correct operation validated.' in stdout, (label, result.returncode, stdout, stderr[-3000:])
        metrics = next(json.loads(s[len('trames metrics: '):]) for s in stderr.splitlines() if s.startswith('trames metrics: '))
        seconds = float(re.search(r'Total time \(secs\):\s*([0-9.]+)', stdout)[1])
        score = float(re.search(r'Iterations/Sec\s*:\s*([0-9.]+)', stdout)[1])
        assert seconds >= 10, 'CoreMark reporting requires >= 10 seconds; increase --iterations'
        row = {'label': label, 'cpus': cpus, 'validation_seeds': validation, 'profiled': profiled,
            'score': score, 'coremark_seconds': seconds, 'wall_seconds': elapsed,
            'instructions_per_iteration_including_startup': metrics['instructions']/args.iterations,
            'metrics': metrics, 'stdout': stdout, 'command': cmd}
        report['runs'].append(row)
        save()
        print(json.dumps({'label': label, 'score': score, 'seconds': seconds, 'guest_ips': metrics['instructions_per_second']}), flush=True)
        return row, samples
    for i in range(args.samples):
        run(f'performance-{i+1}')
    run('validation', validation=True)
    run('single-thread-four-vcpus', cpus=4)
    scores = [x['score'] for x in report['runs'] if x['label'].startswith('performance-')]
    report['performance_summary'] = {'median': statistics.median(scores), 'min': min(scores), 'max': max(scores)}
    if args.native_elf:
        native, iterations = [], '0'
        for i in range(args.samples):
            command = [str(args.native_elf.resolve()), '0', '0', '0x66', iterations, '7', '1', '2000']
            result = subprocess.run(command, capture_output=True, timeout=300)
            stdout = result.stdout.decode()
            (directory/f'native-{i+1}.stdout').write_text(stdout)
            assert result.returncode == 0 and 'Correct operation validated.' in stdout, (stdout, result.stderr)
            seconds = float(re.search(r'Total time \(secs\):\s*([0-9.]+)', stdout)[1])
            assert seconds >= 10, 'Increase native iteration count for a valid score'
            score = float(re.search(r'Iterations/Sec\s*:\s*([0-9.]+)', stdout)[1])
            count = int(re.search(r'^Iterations\s*:\s*(\d+)', stdout, re.M)[1])
            native.append({'sample': i+1, 'score': score, 'iterations': count, 'seconds': seconds, 'stdout': stdout, 'command': command})
            iterations = str(max(count, int(score*15)))
            print(json.dumps({'label': f'native-{i+1}', 'score': score, 'seconds': seconds}), flush=True)
        values = [r['score'] for r in native]
        median = statistics.median(values)
        report['native_baseline'] = {'elf_sha256': sha(args.native_elf), 'runs': native,
            'median': median, 'min': min(values), 'max': max(values),
            'trames_throughput_fraction': report['performance_summary']['median']/median,
            'slowdown': median/report['performance_summary']['median'],
            'scope': 'Same-source, default -O2, single-thread end-to-end CoreMark throughput ratio on this host. Guest/host ISA and toolchain differences are included; not an iPad native efficiency or an instruction/cycle ratio.'}
    if args.profile_runner:
        report['profile_runner_sha256'] = sha(args.profile_runner)
        plain, _ = run('symbols-only', runner=args.profile_runner)
        sampled, path = run('pc-sampling', profiled=True, runner=args.profile_runner)
        report['sampling_elapsed_ratio'] = sampled['coremark_seconds']/plain['coremark_seconds']
        report['profile'] = symbolize(args.profile_runner.resolve(), path)
        assert report['profile']['samples'] >= 500, 'Too few samples; increase --iterations'
    save()
    print(json.dumps(report.get('profile', {}).get('categories', [])), flush=True)


if __name__ == '__main__':
    main()
