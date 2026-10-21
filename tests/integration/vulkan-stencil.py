#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run an ordinary Vulkan loader client against the exact guest/native bridge.

Open the disposable signed-Mesa store directly, without cloning it. Never use
this store concurrently. No GL version override or Firefox diagnostic inputs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--native-client', type=Path)
a = p.parse_args()
runner = ROOT/'target/release/trames-run'
client = ROOT/'target/zink-inputs/vulkan-stencil'
shaders = [ROOT/'tests/fixtures/bin'/name for name in (
    'vulkan-stencil.vert.spv', 'vulkan-stencil.frag.spv', 'vulkan-stencil-sample.frag.spv')]
shaders = [path if path.is_file() else ROOT/'target/zink-native-bundle/shaders'/path.name for path in shaders]
icd = ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so'
for path in (a.store/'manifest', runner, client, *shaders, icd):
    if not path.is_file(): p.error('missing input: '+str(path))
a.output.mkdir(parents=True, exist_ok=False)
prefix = '/tmp/trames-stencil-'+uuid.uuid4().hex
config = a.output/'icd.json'
config.write_text(json.dumps(dict(file_format_version='1.0.0', ICD=dict(
    api_version='1.1.0', library_path=prefix+'.so')))+'\n')
report = dict(physical_ipad=False,
    cpu_vulkan_reference_diagnostic=os.environ.get('TRAMES_ZINK_CPU_REFERENCE') == '1', cases={}, sha256={
    name: hashlib.sha256(path.read_bytes()).hexdigest()
    for name, path in {**dict(runner=runner, client=client, icd=icd), **{p.name: p for p in shaders}}.items()})
base = [str(runner.resolve()), '--storage', str(a.store.resolve()), '--network', 'off',
        '--cpus', '4', '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
        '--max-instructions', '5000000000', '--vulkan']
try:
    commands = [('guest', [*base, '--env', 'VK_DRIVER_FILES='+prefix+'.json',
        '--file', prefix+'.so='+str(icd), '--file', prefix+'.json='+str(config),
        *[v for i, path in enumerate(shaders) for v in ('--file', prefix+str(i)+'.spv='+str(path))],
        '--exec-file', prefix+'='+str(client), '--guest', prefix,
        *[prefix+str(i)+'.spv' for i in range(len(shaders))]])]
    if a.native_client:
        commands.insert(0, ('native', [str(a.native_client.resolve()), *[str(path.resolve()) for path in shaders]]))
    for name, command in commands:
        result = subprocess.run(command, capture_output=True, timeout=180)
        (a.output/(name+'.stdout')).write_bytes(result.stdout)
        (a.output/(name+'.stderr')).write_bytes(result.stderr)
        markers = ('stencil-transfer-ok:', 'stencil-render-ok:', 'stencil-sample-ok:', 'vulkan-stencil-ok')
        passed = result.returncode == 0 and all(marker.encode() in result.stdout for marker in markers)
        report['cases'][name] = dict(exit_code=result.returncode, passed=passed)
        print(name, 'passed' if passed else 'failed', flush=True)
    report['passed'] = all(case['passed'] for case in report['cases'].values())
finally:
    cleanup = subprocess.run([*base, '--guest', '/bin/rm', '-f', prefix, prefix+'.so', prefix+'.json', *[prefix+str(i)+'.spv' for i in range(len(shaders))]],
                             capture_output=True, timeout=30)
    report['cleanup_exit'] = cleanup.returncode
    (a.output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
raise SystemExit(0 if report.get('passed') and report['cleanup_exit'] == 0 else 1)
