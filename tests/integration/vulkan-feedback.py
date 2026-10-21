#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run an ordinary Vulkan loader client against the exact guest/native bridge.

Open the disposable signed-Mesa store directly, without cloning it. Never use
this store concurrently. No GL version override or Firefox diagnostic inputs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--store', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--require-feedback', action='store_true')
p.add_argument('--native-client', type=Path)
a = p.parse_args()
runner = ROOT/'target/release/trames-run'
client = ROOT/'target/zink-inputs/vulkan-feedback'
shader = ROOT/'tests/fixtures/bin/vulkan-feedback.spv'
if not shader.is_file():
    shader = ROOT/'target/zink-native-bundle/shaders/vulkan-feedback.spv'
icd = ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so'
for path in (a.store/'manifest', runner, client, shader, icd):
    if not path.is_file(): p.error('missing input: '+str(path))
a.output.mkdir(parents=True, exist_ok=False)
prefix = '/tmp/trames-feedback-'+uuid.uuid4().hex
config = a.output/'icd.json'
config.write_text(json.dumps(dict(file_format_version='1.0.0', ICD=dict(
    api_version='1.1.0', library_path=prefix+'.so')))+'\n')
report = dict(physical_ipad=False, cases={}, sha256={
    name: hashlib.sha256(path.read_bytes()).hexdigest()
    for name, path in dict(runner=runner, client=client, shader=shader, icd=icd).items()})
base = [str(runner.resolve()), '--storage', str(a.store.resolve()), '--network', 'off',
        '--cpus', '4', '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
        '--max-instructions', '5000000000', '--vulkan']
try:
    commands = [('guest', [*base, '--env', 'VK_DRIVER_FILES='+prefix+'.json',
        '--file', prefix+'.so='+str(icd), '--file', prefix+'.json='+str(config),
        '--file', prefix+'.spv='+str(shader), '--exec-file', prefix+'='+str(client),
        '--guest', prefix, prefix+'.spv'])]
    if a.native_client:
        commands.insert(0, ('native', [str(a.native_client.resolve()), str(shader.resolve())]))
    for name, command in commands:
        result = subprocess.run(command, capture_output=True, timeout=180)
        (a.output/(name+'.stdout')).write_bytes(result.stdout)
        (a.output/(name+'.stderr')).write_bytes(result.stderr)
        captured = b'guest-feedback-ok:' in result.stdout
        unavailable = b'guest-feedback-unavailable:' in result.stdout
        passed = result.returncode == 0 and b'guest-query-ok:' in result.stdout and (
            captured if a.require_feedback else captured != unavailable)
        report['cases'][name] = dict(exit_code=result.returncode, queries_passed=passed,
                                    transform_feedback=captured, passed=passed)
        print(name, 'passed' if passed else 'failed', 'feedback='+str(captured), flush=True)
    report['passed'] = all(case['passed'] for case in report['cases'].values())
finally:
    cleanup = subprocess.run([*base, '--guest', '/bin/rm', '-f', prefix, prefix+'.so', prefix+'.json', prefix+'.spv'],
                             capture_output=True, timeout=30)
    report['cleanup_exit'] = cleanup.returncode
    (a.output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
raise SystemExit(0 if report.get('passed') and report['cleanup_exit'] == 0 else 1)
