#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe real original-Mesa MRT/GLES3 with the configured native Vulkan driver.

The supplied store is a disposable installed signed-Mesa store. It is opened
directly, without a large rootfs copy; never run another session on it concurrently.
Only unique probe inputs are imported, then removed. No GL version override.
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
p.add_argument('--stage', choices=('mrt', 'es3', 'all'), default='all')
p.add_argument('--runner', type=Path, default=ROOT/'target/release/trames-run')
p.add_argument('--timeout', type=float, default=180)
a = p.parse_args()
fixture = ROOT/'target/zink-inputs/zink-es3'
icd = ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so'
for path in (a.store/'manifest', a.runner, fixture, icd):
    if not path.is_file(): p.error('missing input: '+str(path))
a.output.mkdir(parents=True, exist_ok=False)
prefix = '/tmp/trames-es3-'+uuid.uuid4().hex
driver_json = a.output/'icd.json'
driver_json.write_text(json.dumps(dict(file_format_version='1.0.0', ICD=dict(
    api_version='1.1.0', library_path=prefix+'.so')))+'\n')
report = dict(stage=a.stage, physical_ipad=False,
    cpu_vulkan_reference_diagnostic=os.environ.get('TRAMES_ZINK_CPU_REFERENCE') == '1',
    runner_sha256=hashlib.sha256(a.runner.read_bytes()).hexdigest(),
    icd_sha256=hashlib.sha256(icd.read_bytes()).hexdigest(),
    fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
    store_manifest_sha256=hashlib.sha256((a.store/'manifest').read_bytes()).hexdigest(), cases={})
base = [str(a.runner.resolve()), '--storage', str(a.store.resolve()), '--network', 'off',
    '--cpus', '4', '--memory-budget', '4GiB', '--process-memory-budget', '4GiB',
    '--max-instructions', '5000000000', '--vulkan']
environment = [('--env', value) for value in (
    'GALLIUM_DRIVER=zink', 'MESA_LOADER_DRIVER_OVERRIDE=zink', 'EGL_PLATFORM=surfaceless',
    'MESA_SHADER_CACHE_DIR=/tmp/trames-es3-shader-cache', 'VK_DRIVER_FILES='+prefix+'.json')]
if report['cpu_vulkan_reference_diagnostic']:
    environment.append(('--env', 'LIBGL_ALWAYS_SOFTWARE=true'))
imports = ['--file', prefix+'.so='+str(icd), '--file', prefix+'.json='+str(driver_json),
    '--exec-file', prefix+'='+str(fixture)]
try:
    for stage in (('mrt', 'es3') if a.stage == 'all' else (a.stage,)):
        command = [*base, *[v for pair in environment for v in pair], *imports,
                   '--guest', prefix, *(['--mrt'] if stage == 'mrt' else [])]
        result = subprocess.run(command, capture_output=True, timeout=a.timeout)
        imports = []
        (a.output/(stage+'.stdout')).write_bytes(result.stdout)
        (a.output/(stage+'.stderr')).write_bytes(result.stderr)
        marker = b'zink-es2-mrt-ok' if stage == 'mrt' else b'zink-es3-ok'
        passed = result.returncode == 0 and marker in result.stdout
        report['cases'][stage] = dict(exit_code=result.returncode, passed=passed)
        print(stage, 'passed' if passed else 'not available', flush=True)
    report['passed'] = all(case['passed'] for case in report['cases'].values())
finally:
    cleanup = subprocess.run([*base, '--guest', '/bin/rm', '-f', prefix, prefix+'.so', prefix+'.json'],
                             capture_output=True, timeout=30)
    report['cleanup_exit'] = cleanup.returncode
    (a.output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
raise SystemExit(0 if report.get('passed') and report['cleanup_exit'] == 0 else 1)
