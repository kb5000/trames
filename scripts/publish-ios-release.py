#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Publish one verified iPad build and paired source, retaining immutable tags."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import plistlib
import re
import stat
import struct
import subprocess
import tarfile
import tempfile
import time
import tomllib
import zipfile


def gh(*arguments):
    return subprocess.check_output(['gh', *map(str, arguments)])


def api(path):
    return json.loads(gh('api', path))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def wait_run(repository, run_id):
    deadline = time.monotonic() + 45 * 60
    while True:
        run = api(f'repos/{repository}/actions/runs/{run_id}')
        if run['status'] == 'completed':
            if run['conclusion'] != 'success':
                raise RuntimeError(f"Workflow {run_id} concluded {run['conclusion']}")
            return run
        if time.monotonic() >= deadline:
            raise TimeoutError(f'Workflow {run_id} did not finish')
        print(f"Waiting for {run['name']} ({run_id}): {run['status']}", flush=True)
        time.sleep(20)


def verify(archives, source, source_commit):
    outer, build, musl = [archives[k] for k in ('sideload', 'build', 'musl')]
    names = [n for n in outer.namelist() if n.endswith('.ipa')]
    assert len(names) == 1
    data = outer.read(names[0])
    assert outer.read(names[0] + '.sha256').decode().split()[0] == sha(data)
    app = zipfile.ZipFile(io.BytesIO(data))
    assert app.testzip() is None
    prefix = 'Payload/Trames.app/'
    info = plistlib.loads(app.read(prefix + 'Info.plist'))
    version = tomllib.loads((source / 'Cargo.toml').read_text())['workspace']['package']['version']
    assert info['CFBundleShortVersionString'] == version
    assert info['CFBundleVersion'] == '16' and version == '0.15.0'
    assert info['CFBundleIdentifier'] == 'org.trames.Trames'
    assert info['CFBundleSupportedPlatforms'] == ['iPhoneOS']
    assert info['MinimumOSVersion'] == '17.0' and 2 in info['UIDeviceFamily']
    executable = prefix + info['CFBundleExecutable']
    binary = app.read(executable)
    magic, cpu, _, filetype, count, size, _, _ = struct.unpack_from('<8I', binary)
    assert (magic, cpu, filetype) == (0xfeedfacf, 0x0100000c, 2)
    offset, platforms = 32, []
    for _ in range(count):
        command, length = struct.unpack_from('<2I', binary, offset)
        assert length >= 8 and offset + length <= 32 + size
        if command == 0x32:
            assert length >= 24
            platforms.append(struct.unpack_from('<I', binary, offset + 8)[0])
        offset += length
    assert platforms == [2]
    assert (app.getinfo(executable).external_attr >> 16) & stat.S_IXUSR
    files = {n.removeprefix(prefix): app.read(n) for n in app.namelist()
             if n.startswith(prefix) and not n.endswith('/')}
    built_prefix = 'build/device/Build/Products/Debug-iphoneos/Trames.app/'
    built = {n.removeprefix(built_prefix): build.read(n) for n in build.namelist()
             if n.startswith(built_prefix) and not n.endswith('/')}
    assert files == built
    manifest = json.loads(musl.read('manifest.json'))
    libc = musl.read('libc.so')
    assert sha(libc) == manifest['libc_sha256'] and files['GuestMusl/libc.so'] == libc
    assert files['GuestMusl/manifest.json'] == musl.read('manifest.json')
    assert manifest['native_features'] == 0x7ffff and len(manifest['native_functions']) == 19
    assert b'Version 3' in files['GuestMusl/GPL-3.0.txt']
    assert b'Permission is hereby granted' in files['GuestMusl/musl-COPYRIGHT.txt']
    with tarfile.open(fileobj=io.BytesIO(musl.read('musl-hybrid-1.2.5-source.tar.gz'))) as archive:
        patch = json.load(archive.extractfile('./trames-patch.json'))
        for path, digest in patch['inputs_sha256'].items():
            assert sha((source / path).read_bytes()) == digest, path
            assert sha(archive.extractfile('./source/' + path).read()) == digest, path
        assert sha(archive.extractfile('./libc.so').read()) == sha(libc)
        upstream = './source/target/busybox-build/downloads/musl-1.2.5.tar.gz'
        assert sha(archive.extractfile(upstream).read()) == manifest['upstream_archive_sha256']
    summaries = [n for n in build.namelist() if n.endswith('ipad-smoke-summary.json')]
    assert len(summaries) == 1
    summary = json.loads(build.read(summaries[0]))
    assert summary['passedTests'] >= 14 and summary['failedTests'] == 0 and summary['skippedTests'] == 0
    sim = 'build/simulator/Build/Products/Debug-iphonesimulator/Trames.app/GuestMusl/libc.so'
    assert build.read(sim) == libc
    vulkan = archives['vulkan']
    gpu = json.loads(vulkan.read('manifest.json'))
    icd = vulkan.read('libtrames_vulkan_icd.so')
    assert files['GuestVulkan/libtrames_vulkan_icd.so'] == icd
    assert files['GuestVulkan/manifest.json'] == vulkan.read('manifest.json')
    assert sha(icd) == gpu['icd_sha256'] and gpu['profile'] == 65542
    assert gpu['wire_revision'] == 3 and gpu['default_enabled'] is False
    assert gpu['native_moltenvk'] == '1.4.2'
    assert len(re.findall(r'if\(!strcmp\(name,', (source / 'accelerator/vulkan/entrypoints.inc').read_text())) == 108
    for name in ['vulkan-texture', 'vulkan-texture.vert.spv', 'vulkan-texture.frag.spv', 'vulkan-present']:
        assert files['GuestVulkan/acceptance/' + name] == vulkan.read('acceptance/' + name)
    notices = [n for n in vulkan.namelist() if n.startswith('Notices/') and not n.endswith('/')]
    assert len(notices) >= 10
    for name in notices:
        assert files['GuestVulkan/' + name] == vulkan.read(name)
    with tarfile.open(fileobj=io.BytesIO(vulkan.read('vulkan-native-upstream-source.tar.gz'))) as archive:
        upstream = json.load(archive.extractfile('vulkan-upstream.json'))
        assert upstream == json.loads((source / 'scripts/vulkan-upstream.json').read_text())
        for name, record in upstream['archives'].items():
            if name != 'moltenvk-all.tar':
                assert sha(archive.extractfile('upstream/' + name).read()) == record['sha256'], name
        assert archive.extractfile('prepare-vulkan.py').read() == (source / 'scripts/prepare-vulkan.py').read_bytes()
        assert archive.extractfile('TRAMES-GPL-3.0.txt').read() == (source / 'LICENSE').read_bytes()
    report = dict(source_commit=source_commit, version=version, build='16',
        ipa_sha256=sha(data), ipa_bytes=len(data), bundle_id=info['CFBundleIdentifier'],
        minimum_os=info['MinimumOSVersion'], device_families=info['UIDeviceFamily'],
        device_architecture='arm64', mach_o_platform=2,
        all_packaged_files_match_device_build=len(files), executable_permission_verified=True,
        zip_crc_and_sha256_sidecar_verified=True, libc_sha256=sha(libc),
        musl_native_functions=19, source_inputs_verified=len(patch['inputs_sha256']),
        license_notices_verified=True, vulkan_icd_sha256=sha(icd), vulkan_profile=gpu['profile'],
        vulkan_public_entrypoints=108, moltenvk_version='1.4.2', vulkan_default_enabled=False,
        vulkan_source_and_notices_verified=True, simulator_passed=summary['passedTests'], simulator_failed=0)
    return data, report, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--apple-run', type=int, required=True)
    parser.add_argument('--allow-version', choices=['0.15.0'], required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'[\w.-]+/[\w.-]+', args.repository)
    apple = wait_run(args.repository, args.apple_run)
    assert apple['path'] == '.github/workflows/ios.yml' and apple['head_branch'] == 'main'
    assert apple['event'] == 'push' and apple['head_repository']['full_name'] == args.repository
    commit = apple['head_sha']
    assert re.fullmatch(r'[0-9a-f]{40}', commit)
    with tempfile.TemporaryDirectory(prefix='trames-release-') as folder:
        directory = Path(folder)
        source = directory / 'source'
        subprocess.run(['git', 'clone', '--quiet', '--no-checkout', '--filter=blob:none',
                        f'https://github.com/{args.repository}.git', str(source)], check=True)
        subprocess.run(['git', '-C', str(source), 'checkout', '--quiet', '--detach', commit], check=True)
        version = tomllib.loads((source / 'Cargo.toml').read_text())['workspace']['package']['version']
        if version != args.allow_version:
            print(f'Skipping {version}; only {args.allow_version} is authorized by this release job.')
            return
        # A newer authorized source can arrive while an older build is running.
        # Never let that older run claim this release version first.
        if api(f'repos/{args.repository}/git/ref/heads/main')['object']['sha'] != commit:
            print('Skipping superseded source; wait for the current main build.')
            return
        runs = api(f'repos/{args.repository}/actions/runs?head_sha={commit}')['workflow_runs']
        linux = next(r for r in runs if r['path'] == '.github/workflows/linux.yml')
        linux = wait_run(args.repository, linux['id'])
        assert linux['head_sha'] == commit and linux['event'] == 'push'
        tag = 'v' + version
        existing = subprocess.run(['gh', 'release', 'view', tag, '--repo', args.repository,
                                   '--json', 'url,isDraft,targetCommitish,assets'], capture_output=True)
        release = json.loads(existing.stdout) if existing.returncode == 0 else None
        asset_names = {f'Trames-{version}-build16-unsigned.ipa',
                       f'Trames-{version}-build16-unsigned.ipa.sha256',
                       f'Trames-{version}-verification.json', f'Trames-{version}-simulator.json',
                       'musl-hybrid-1.2.5-source.tar.gz', 'vulkan-native-upstream-source.tar.gz'}
        if release and not release['isDraft']:
            assert asset_names <= {a['name'] for a in release['assets']}, 'Published release is incomplete'
            print(release['url'])
            print('Existing release is immutable; assets and tag retained.')
            return
        if release:
            assert release['targetCommitish'] == commit, 'Draft belongs to another source commit'
        expected = {'sideload': 'Trames-unsigned-sideload', 'build': 'unsigned-ipad-build-and-project',
                    'musl': 'full-musl-library-and-source',
                    'vulkan': 'vulkan-compute-driver-and-native-source'}
        artifacts = api(f'repos/{args.repository}/actions/runs/{args.apple_run}/artifacts')['artifacts']
        archives, metadata = {}, {}
        for kind, name in expected.items():
            artifact = next(a for a in artifacts if a['name'] == name and not a['expired'])
            raw = gh('api', f"repos/{args.repository}/actions/artifacts/{artifact['id']}/zip")
            assert sha(raw) == artifact['digest'].removeprefix('sha256:')
            archive = zipfile.ZipFile(io.BytesIO(raw))
            assert archive.testzip() is None
            archives[kind] = archive
            metadata[kind] = {k: artifact[k] for k in ('id', 'name', 'digest', 'expires_at')}
        data, report, summary = verify(archives, source, commit)
        report['artifacts'] = metadata
        report['ci'] = {r['name']: {'id': r['id'], 'attempt': r['run_attempt'],
                        'url': r['html_url'], 'head_sha': r['head_sha'], 'conclusion': r['conclusion']}
                        for r in (apple, linux)}
        ipa = directory / f'Trames-{version}-build16-unsigned.ipa'
        ipa.write_bytes(data)
        checksum = ipa.with_suffix('.ipa.sha256')
        checksum.write_text(f'{sha(data)}  {ipa.name}\n')
        proof = directory / f'Trames-{version}-verification.json'
        proof.write_text(json.dumps(report, indent=2) + '\n')
        simulator = directory / f'Trames-{version}-simulator.json'
        simulator.write_text(json.dumps(summary, indent=2) + '\n')
        musl = directory / 'musl-hybrid-1.2.5-source.tar.gz'
        musl.write_bytes(archives['musl'].read(musl.name))
        notes = directory / 'release-notes.md'
        native_source = directory / 'vulkan-native-upstream-source.tar.gz'
        native_source.write_bytes(archives['vulkan'].read(native_source.name))
        notes.write_text((source / 'docs/validation/release-0.15-notes.md').read_text() + f"""

实际模拟器 {summary['passedTests']} passed / 0 failed / 0 skipped。IPA 与 ARM64 设备构建逐字节一致；ZIP、SHA-256、执行权限、平台、原始源码及许可证均通过核验。

Source commit: `{commit}`
IPA SHA-256: `{sha(data)}`
Apple CI: {apple['html_url']}
Linux CI: {linux['html_url']}
""")
        assets = [ipa, checksum, proof, simulator, musl, native_source]
        if api(f'repos/{args.repository}/git/ref/heads/main')['object']['sha'] != commit:
            print('Source superseded during verification; no release published.')
            return
        if release:
            present = {a['name'] for a in release['assets']}
            for asset in assets:
                if asset.name in present:
                    downloaded = directory / ('existing-' + asset.name)
                    gh('release', 'download', tag, '--repo', args.repository,
                       '--pattern', asset.name, '--output', downloaded)
                    assert sha(downloaded.read_bytes()) == sha(asset.read_bytes()), asset.name
                else:
                    gh('release', 'upload', tag, asset, '--repo', args.repository)
        else:
            gh('release', 'create', tag, *assets, '--repo', args.repository, '--draft',
               '--target', commit, '--title', f'Trames {version} (build 16)', '--notes-file', notes)
        # The public release appears only after every verified asset uploaded.
        gh('release', 'edit', tag, '--repo', args.repository, '--draft=false')
        print(f'https://github.com/{args.repository}/releases/tag/{tag}')


if __name__ == '__main__':
    main()
