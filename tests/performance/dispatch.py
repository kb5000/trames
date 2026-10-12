#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reproducible static dispatch research; results are not Trames/iPad speedups."""
import argparse
import json
from pathlib import Path
import resource
import subprocess
import tempfile


def small_stack():
    resource.setrlimit(resource.RLIMIT_STACK, (256 * 1024, 256 * 1024))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', default='clang++')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).with_suffix('.cpp')
    compiler = subprocess.check_output([args.clang, '--version'], text=True).splitlines()[0]
    results = []
    with tempfile.TemporaryDirectory(prefix='trames-dispatch-') as directory:
        folder = Path(directory)
        executables = []
        for style in range(3):
            executable = folder / str(style)
            subprocess.run([args.clang, '-std=c++17', '-O3', '-DDRIVER', f'-DSTYLE={style}',
                            str(source), '-o', str(executable)], check=True)
            executables.append(executable)
        for sample in range(3):
            for style in (range(3) if sample % 2 == 0 else range(2, -1, -1)):
                result = json.loads(subprocess.check_output([str(executables[style]), '100000000'],
                                                            preexec_fn=small_stack, text=True, timeout=30))
                results.append(dict(sample=sample + 1, **result))
        assert len({(r['checksum'], r['pc'], r['remaining']) for r in results}) == 1
        # With optimizations disabled, ordinary recursion would overflow this
        # stack. musttail is a compiler guarantee, independent of optimization.
        subprocess.run([args.clang, '-std=c++17', '-O0', '-DDRIVER', '-DSTYLE=2', str(source),
                        '-o', str(folder / 'tail-debug')], check=True)
        debug = json.loads(subprocess.check_output([str(folder / 'tail-debug'), '10000000'],
                                                  preexec_fn=small_stack, text=True, timeout=30))
        assert debug['remaining'] == 0
        reference = json.loads(subprocess.check_output([str(executables[0]), '10000000'], text=True))
        assert (debug['checksum'], debug['pc']) == (reference['checksum'], reference['pc'])
        # No Apple SDK or system headers are needed by the dispatch core.
        # Compilation verifies musttail is legal under the iOS arm64 ABI.
        args.output.parent.mkdir(parents=True, exist_ok=True)
        assembly = args.output.with_suffix('.arm64-ios.s')
        subprocess.run([args.clang, '-target', 'arm64-apple-ios17.0', '-std=c++17', '-O0',
                        '-ffreestanding', '-DSTYLE=2', '-S', str(source), '-o', str(assembly)], check=True)
    report = dict(scope='Synthetic integer dispatch only; no guest memory or real programs; no iPad timing',
                  compiler=compiler, stack_limit_bytes=256 * 1024, debug_tail=debug,
                  ios_arm64_musttail_compile=True, results=results)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
