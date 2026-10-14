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
    assert info['CFBundleVersion'] == '15' and version == '0.14.0'
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
    assert summary['passedTests'] > 0 and summary['failedTests'] == 0
    sim = 'build/simulator/Build/Products/Debug-iphonesimulator/Trames.app/GuestMusl/libc.so'
    assert build.read(sim) == libc
    report = dict(source_commit=source_commit, version=version, build='15',
        ipa_sha256=sha(data), ipa_bytes=len(data), bundle_id=info['CFBundleIdentifier'],
        minimum_os=info['MinimumOSVersion'], device_families=info['UIDeviceFamily'],
        device_architecture='arm64', mach_o_platform=2,
        all_packaged_files_match_device_build=len(files), executable_permission_verified=True,
        zip_crc_and_sha256_sidecar_verified=True, libc_sha256=sha(libc),
        musl_native_functions=19, source_inputs_verified=len(patch['inputs_sha256']),
        license_notices_verified=True, simulator_passed=summary['passedTests'], simulator_failed=0)
    return data, report, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True)
    parser.add_argument('--apple-run', type=int, required=True)
    parser.add_argument('--allow-version', choices=['0.14.0'], required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'[\w.-]+/[\w.-]+', args.repository)
    apple = wait_run(args.repository, args.apple_run)
    assert apple['path'] == '.github/workflows/ios.yml' and apple['head_branch'] == 'main'
    assert apple['event'] == 'push' and apple['head_repository']['full_name'] == args.repository
    commit = apple['head_sha']
    assert re.fullmatch(r'[0-9a-f]{40}', commit)
    runs = api(f'repos/{args.repository}/actions/runs?head_sha={commit}')['workflow_runs']
    linux = next(r for r in runs if r['path'] == '.github/workflows/linux.yml')
    linux = wait_run(args.repository, linux['id'])
    assert linux['head_sha'] == commit and linux['event'] == 'push'
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
        tag = 'v' + version
        existing = subprocess.run(['gh', 'release', 'view', tag, '--repo', args.repository,
                                   '--json', 'url,isDraft,targetCommitish,assets'], capture_output=True)
        release = json.loads(existing.stdout) if existing.returncode == 0 else None
        asset_names = {f'Trames-{version}-build15-unsigned.ipa',
                       f'Trames-{version}-build15-unsigned.ipa.sha256',
                       f'Trames-{version}-verification.json', f'Trames-{version}-simulator.json',
                       'musl-hybrid-1.2.5-source.tar.gz'}
        if release and not release['isDraft']:
            assert asset_names <= {a['name'] for a in release['assets']}, 'Published release is incomplete'
            print(release['url'])
            print('Existing release is immutable; assets and tag retained.')
            return
        if release:
            assert release['targetCommitish'] == commit, 'Draft belongs to another source commit'
        expected = {'sideload': 'Trames-unsigned-sideload', 'build': 'unsigned-ipad-build-and-project',
                    'musl': 'full-musl-library-and-source'}
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
        ipa = directory / f'Trames-{version}-build15-unsigned.ipa'
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
        notes.write_text(f'''0.14.0/build 15 合并原生工作线程并行、19 项纯函数、64 字节区域访存与稀疏脏页提交。未使用 JIT，实验性原生 malloc/free 默认关闭。

Windows 下载 `Trames-{version}-build15-unsigned.ipa`，用 Sideloadly 和原 Apple ID/实际 Bundle ID 覆盖安装，无需清数据。最低 iPadOS 17，Bundle ID 为 `org.trames.Trames`。

四核分配负载本次中位数耗时降低约 14%～18%；其他负载有回退和宿主波动，不能视作整体或物理 iPad 提速。完整证据见仓库 `docs/validation/checkpoint-cost.md`。

Linux 与 Apple 工作流均通过，实际模拟器 {summary['passedTests']} passed / 0 failed，IPA 文件与 ARM64 设备构建逐字节一致。已核对 ZIP CRC、SHA-256、可执行权限、设备平台、19 项原生功能、原始源码及许可证。

GPL-3.0-or-later：完整仓库源码对应此 Release 的固定 tag；`musl-hybrid-1.2.5-source.tar.gz` 提供 guest libc 与配套源码。基于固定 NIOSSH 源码修复关闭期间的窗口调整崩溃；确定性复现与原始测试保留，真实 OpenSSH PTY/SFTP 检查通过。物理 iPad 验收继续在 Issue #8 跟踪。

Source commit: `{commit}`
IPA SHA-256: `{sha(data)}`
Apple CI: {apple['html_url']}
Linux CI: {linux['html_url']}
''')
        assets = [ipa, checksum, proof, simulator, musl]
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
               '--target', commit, '--title', f'Trames {version} (build 15)', '--notes-file', notes)
        # The public release appears only after every verified asset uploaded.
        gh('release', 'edit', tag, '--repo', args.repository, '--draft=false')
        print(f'https://github.com/{args.repository}/releases/tag/{tag}')


if __name__ == '__main__':
    main()
