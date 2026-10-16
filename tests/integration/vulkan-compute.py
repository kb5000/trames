#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Original signed Linux Vulkan loader -> guest ICD -> actual native driver."""
import importlib.util
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import sys

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('m6_repository', ROOT / 'tests/integration/m6.py')
m6 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m6)
runner = os.getenv('TRAMES_RUNNER', str(ROOT / 'target/release/trames-run'))
fixtures = ROOT / 'tests/fixtures/bin'
reference = subprocess.run([str(fixtures / 'vulkan-compute-host'), str(fixtures / 'vulkan-compute.spv')], capture_output=True, timeout=60)
assert reference.returncode == 0 and reference.stdout.endswith(b'vulkan-compute-ok\n'), reference
coherent_reference = subprocess.run([str(fixtures / 'vulkan-compute-host'), str(fixtures / 'vulkan-compute.spv'), 'coherent'], capture_output=True, timeout=60)
assert coherent_reference.returncode == 0 and b'coherent persistent mappings without flush/invalidate' in coherent_reference.stdout, coherent_reference
print(reference.stderr.decode(), end='', flush=True)
with tempfile.TemporaryDirectory(prefix='trames-vulkan-compute-') as directory:
    tmp = Path(directory)
    m6.repository(tmp / 'repository', ('vulkan-loader',))
    packages = sorted((tmp / 'repository').rglob('*.apk'))
    for cpus in ('1', '4'):
        base = [runner, '--storage', str(tmp / ('store-' + cpus)), '--cpus', cpus,
            '--vulkan', '--metrics', '--max-instructions', '10000000000']
        imported = []
        for package in packages:
            imported += ['--file', '/tmp/' + package.name + '=' + str(package)]
        p = subprocess.run([*base, '--rootfs', str(m6.CACHE / 'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
            *imported, '--guest', '/sbin/apk', '--repositories-file', '/dev/null', '--no-progress',
            'add', *['/tmp/' + package.name for package in packages]], capture_output=True, timeout=180)
        assert p.returncode == 0 and b'ERROR:' not in p.stdout + p.stderr, p
        p = subprocess.run([*base,
            '--env', 'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json',
            '--file', '/usr/lib/libtrames_vulkan_icd.so=' + str(ROOT / 'target/vulkan-guest/libtrames_vulkan_icd.so'),
            '--file', '/usr/share/vulkan/icd.d/trames.json=' + str(ROOT / 'target/vulkan-guest/trames.json'),
            '--file', '/tmp/compute.spv=' + str(fixtures / 'vulkan-compute.spv'),
            '--exec-file', '/tmp/vulkan-compute=' + str(fixtures / 'vulkan-compute'),
            '--guest', '/tmp/vulkan-compute', '/tmp/compute.spv', 'bridge-check'], capture_output=True, timeout=120)
        if p.returncode != 0 or p.stdout != reference.stdout:
            print(p.stdout.decode(errors='replace'), flush=True)
            print(p.stderr.decode(errors='replace'), flush=True)
            log = subprocess.run([*base, '--guest', '/bin/dmesg'], capture_output=True, timeout=15)
            print(log.stdout.decode(errors='replace'), flush=True)
        assert p.returncode == 0 and p.stdout == reference.stdout, p
        assert b'Vulkan driver:' in p.stderr and b'ERROR' not in p.stderr, p.stderr
        metrics=json.loads(next(line.removeprefix('trames metrics: ') for line in p.stderr.decode().splitlines() if line.startswith('trames metrics: ')))
        assert metrics['gpu_compiles']==2 and metrics['gpu_peer_instructions']>0,metrics
        assert metrics['gpu_upload_bytes']>0 and metrics['gpu_download_bytes']>0,metrics
        assert metrics['gpu_direct_bytes']==metrics['gpu_upload_bytes']+metrics['gpu_download_bytes'] and metrics['gpu_buffered_bytes']==0,metrics
        print(f"{cpus}-CPU asynchronous native compile: peer retirement summed over call wait windows={metrics['gpu_peer_instructions']}; pending callers peak={metrics['gpu_peak_pending']}; direct GPU transfers {metrics['gpu_direct_bytes']} bytes with zero full-range staging",flush=True)
        print(f'{cpus}-CPU guest standard Vulkan: default and specialized compute, binary semaphore chains/reuse, batch boundaries, empty submissions and rejection rollback match native driver', flush=True)
        print(p.stderr.decode(), end='', flush=True)
        coherent = subprocess.run([*base,
            '--env', 'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json',
            '--guest', '/tmp/vulkan-compute', '/tmp/compute.spv', 'coherent'], capture_output=True, timeout=120)
        assert coherent.returncode == 0 and coherent.stdout == coherent_reference.stdout, (coherent.stdout, coherent.stderr)
        print(f'{cpus}-CPU coherent Vulkan: unmap/remap and persistent mappings without explicit flush/invalidate match native driver; disjoint CPU/GPU writes preserved', flush=True)
        disabled = subprocess.run([runner, '--storage', str(tmp / ('store-' + cpus)), '--cpus', cpus,
            '--max-instructions', '1000000000', '--env', 'VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json',
            '--guest', '/tmp/vulkan-compute', '/tmp/compute.spv', 'disabled'], capture_output=True, timeout=30)
        assert (disabled.returncode, disabled.stdout) == (0, b'vulkan-disabled-ok\n'), disabled
        print(f'{cpus}-CPU host opt-out: ICD remains installed, original loader reports incompatible driver', flush=True)
    # The app uses this exact ABI/path, including automatic ICD installation.
    library=ROOT/'target/release'/('libtrames_ffi.dylib' if sys.platform=='darwin' else 'libtrames_ffi.so')
    api=ctypes.CDLL(str(library))
    api.trames_session_open.argtypes=[ctypes.c_char_p,ctypes.c_uint32];api.trames_session_open.restype=ctypes.c_uint64
    api.trames_session_input.argtypes=[ctypes.c_uint64,ctypes.c_char_p,ctypes.c_uint32]
    api.trames_session_transfer.argtypes=[ctypes.c_uint64,ctypes.c_char_p,ctypes.c_uint32]
    for name in ('trames_session_status','trames_session_output'):
        getattr(api,name).argtypes=[ctypes.c_uint64,ctypes.c_void_p,ctypes.c_uint32]
    api.trames_session_close.argtypes=[ctypes.c_uint64]
    options={'store':str(tmp/'ffi-store'),'rootfs':str(m6.CACHE/'alpine-minirootfs-3.22.6-aarch64.tar.gz'),
        'volume_total':16<<30,'volume_available':16<<30,'network':'off','vulkan_accelerator':True,
        'vulkan_icd':str(ROOT/'target/vulkan-guest/libtrames_vulkan_icd.so')}
    data=json.dumps(options).encode();handle=api.trames_session_open(data,len(data));assert handle
    try:
        began=time.monotonic();output=bytearray();buffer=ctypes.create_string_buffer(65536)
        while True:
            n=api.trames_session_status(handle,buffer,len(buffer));assert n>0
            status=json.loads(buffer.raw[:n])
            if status['phase']=='running':break
            assert status['phase']=='starting' and time.monotonic()-began<30,status
            time.sleep(.01)
        for guest,host in [*[("/tmp/"+p.name,p) for p in packages],
            ('/tmp/vulkan-compute',fixtures/'vulkan-compute'),('/tmp/compute.spv',fixtures/'vulkan-compute.spv')]:
            payload=json.dumps({'guest':guest,'host':str(host),'export':False}).encode()
            assert api.trames_session_transfer(handle,payload,len(payload))==0
        command=('apk --repositories-file /dev/null --no-progress add '+ ' '.join('/tmp/'+p.name for p in packages)
            +' && chmod 755 /tmp/vulkan-compute && VK_DRIVER_FILES=/usr/share/vulkan/icd.d/trames.json /tmp/vulkan-compute /tmp/compute.spv; printf "\\nVULKAN_RESULT:%d\\n" $?\n').encode()
        assert api.trames_session_input(handle,command,len(command))==len(command)
        while b'\r\nVULKAN_RESULT:0\r\n' not in output:
            n=api.trames_session_output(handle,buffer,len(buffer));assert n>=0
            output.extend(buffer.raw[:n]);assert time.monotonic()-began<120,output[-4000:]
            assert b'VULKAN_RESULT:134' not in output and b'VULKAN_RESULT:1\r\n' not in output,output
            time.sleep(.01)
        assert b'compute round 3: 4096 uint32 values correct' in output and b'compute semaphore chain 2: 4096 specialized uint32 values correct' in output and b'binary semaphore reuse and empty submissions: ok' in output and b'partial mapping, offset flush/invalidate and remap persistence: ok' in output and b'vulkan-compute-ok' in output,output
        print('Real embedding ABI: automatic ICD installation, signed loader, compute and teardown passed',flush=True)
    finally:
        api.trames_session_close(handle)
print(reference.stdout.decode(), end='')
