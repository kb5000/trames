#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Publish only the verified 1.1.0 candidate after PR30 merges; retain old assets."""
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
import tomllib
import zipfile

REPO = 'kb5000/trames'
VERSION = '1.1.0'
BUILD = '32'


def gh(*args):
    return subprocess.check_output(['gh', *map(str, args)])


def api(path):
    return json.loads(gh('api', path))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked_run(run_id, commit, workflow, job_name):
    run = api(f'repos/{REPO}/actions/runs/{run_id}')
    assert run['head_sha'] == commit and run['conclusion'] == 'success'
    assert run['head_repository']['full_name'] == REPO and run['path'] == '.github/workflows/' + workflow
    jobs = api(f'repos/{REPO}/actions/runs/{run_id}/jobs')['jobs']
    job = next(j for j in jobs if j['name'] == job_name and j['conclusion'] == 'success')
    return run, {s['name'] for s in job['steps'] if s['conclusion'] == 'success'}


def artifact(run_id, name):
    entries = api(f'repos/{REPO}/actions/runs/{run_id}/artifacts')['artifacts']
    entry = next(a for a in entries if a['name'] == name and not a['expired'])
    raw = gh('api', f"repos/{REPO}/actions/artifacts/{entry['id']}/zip")
    assert sha(raw) == entry['digest'].removeprefix('sha256:')
    archive = zipfile.ZipFile(io.BytesIO(raw))
    assert archive.testzip() is None
    return archive, {k: entry[k] for k in ('id', 'name', 'digest', 'expires_at')}


def verify_bundle(app, root):
    info = plistlib.loads(app.read(root + 'Info.plist'))
    assert (info['CFBundleShortVersionString'], info['CFBundleVersion']) == (VERSION, BUILD)
    assert info['CFBundleSupportedPlatforms'] == ['iPhoneOS']
    name = root + info['CFBundleExecutable']
    assert app.getinfo(name).external_attr >> 16 & stat.S_IXUSR
    data = app.read(name)
    magic, cpu, _, kind, count, size, _, _ = struct.unpack_from('<8I', data)
    assert (magic, cpu, kind) == (0xFEEDFACF, 0x0100000C, 2)
    offset, platforms = 32, []
    for _ in range(count):
        command, length = struct.unpack_from('<2I', data, offset)
        assert length >= 8 and offset + length <= 32 + size
        assert command != 0x1D, 'Unsigned executable must not retain LC_CODE_SIGNATURE'
        if command == 0x32:
            platforms.append(struct.unpack_from('<I', data, offset + 8)[0])
        offset += length
    assert platforms == [2]
    return info, sha(data)


def main():
    if tomllib.loads(Path('Cargo.toml').read_text())['workspace']['package']['version'] != VERSION:
        print('Other release version; nothing to publish.')
        return
    config_path = Path('docs/validation/release-1.1.json')
    if not config_path.exists():
        print('No verified release manifest yet; nothing to publish.')
        return
    config = json.loads(config_path.read_text())
    assert config['status'] == 'verified' and config['version'] == VERSION and str(config['build']) == BUILD
    source = config['source_commit']
    assert re.fullmatch('[0-9a-f]{40}', source)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    subprocess.run(['git', 'merge-base', '--is-ancestor', source, head], check=True)
    changed = subprocess.check_output(['git', 'diff', '--name-only', source, head], text=True).splitlines()
    allowed = {'.github/ios-validation-plan.json', 'scripts/ios-ci-scope.py', '.github/workflows/ios.yml', '.github/workflows/linux.yml', 'scripts/publish-ios-release-1.1.py'}
    assert all(p.startswith('docs/') or p in allowed or p == 'README.md' for p in changed), 'Tag must retain the tested application/build/publisher sources'
    pr = api(f'repos/{REPO}/pulls/30')
    assert pr['merged'] and pr['base']['ref'] == 'main' and pr['head']['repo']['full_name'] == REPO
    apple, steps = checked_run(config['apple_run'], source, 'ios.yml', 'apple-build')
    assert {'Validate affected native modules or the complete Apple core', 'Link unsigned iPad device app', 'Package unsigned device IPA for sideload signing'} <= steps
    linux, steps = checked_run(config['linux_run'], config['linux_source_commit'], 'linux.yml', 'release-core')
    assert 'Complete release iOS cross compile' in steps
    assert not subprocess.check_output(['git', 'diff', '--name-only', source, config['linux_source_commit'], '--', 'crates', 'Cargo.toml', 'Cargo.lock', 'vendor', '.cargo', 'rust-toolchain.toml']).strip()
    workspace = api(f"repos/{REPO}/actions/runs/{config['linux_workspace_run']}")
    assert workspace['head_sha'] == source and workspace['path'] == '.github/workflows/linux.yml'
    assert workspace['head_repository']['full_name'] == REPO
    workspace_job = next(j for j in api(f"repos/{REPO}/actions/runs/{workspace['id']}/jobs")['jobs'] if j['name'] == 'release-core')
    log = gh('api', f"repos/{REPO}/actions/jobs/{workspace_job['id']}/logs").decode(errors='replace')
    passed = sum(int(n) for n in re.findall(r'test result: ok\. (\d+) passed;', log))
    assert passed == config['linux_workspace_passed'] >= 477
    assert not re.search(r'test result: FAILED', log)
    assert 'native parser archiver: Os { code: 2' in log
    # The original -e shell reached the iOS cross check only after fmt, strict
    # clippy and the full workspace tests passed. Retry only that missing tool.

    tag = 'v' + VERSION
    present = subprocess.run(['gh', 'release', 'view', tag, '--repo', REPO, '--json', 'url,isDraft,targetCommitish,assets'], capture_output=True)
    release = json.loads(present.stdout) if present.returncode == 0 else None
    if release and not release['isDraft']:
        print(release['url']); print('Published release is immutable; retained without changes.')
        return
    if release:
        assert release['targetCommitish'] == head
    refs = api(f'repos/{REPO}/git/matching-refs/tags/{tag}')
    for ref in refs:
        if ref['ref'] == 'refs/tags/' + tag:
            target = ref['object']
            if target['type'] == 'tag':
                target = api(f"repos/{REPO}/git/tags/{target['sha']}")['object']
            assert target['sha'] == head, 'Never move an existing tag'
    outer, outer_meta = artifact(config['apple_run'], 'Trames-unsigned-sideload')
    built, built_meta = artifact(config['apple_run'], 'unsigned-ipad-build-and-project')
    musl, musl_meta = artifact(config['guest_run'], 'full-musl-library-and-source')
    vulkan, vulkan_meta = artifact(config['guest_run'], 'vulkan-compute-driver-and-native-source')
    name = next(n for n in outer.namelist() if n.endswith('.ipa'))
    data = outer.read(name)
    assert outer.read(name + '.sha256').decode().split()[0] == sha(data) == config['ipa_sha256']
    app = zipfile.ZipFile(io.BytesIO(data)); assert app.testzip() is None
    root = 'Payload/Trames.app/'
    info, executable_sha = verify_bundle(app, root)
    assert info['CFBundleIdentifier'] == 'org.trames.Trames' and info['MinimumOSVersion'] == '17.0'
    providers = [n.removesuffix('Info.plist') for n in app.namelist() if '/PlugIns/' in n and n.endswith('.appex/Info.plist')]
    assert len(providers) == 1
    provider, provider_sha = verify_bundle(app, providers[0])
    assert provider['CFBundleIdentifier'] == 'org.trames.Trames.FileProvider'
    assert provider['TramesAppGroup'] == info['TramesAppGroup'] == 'group.org.trames.Trames'
    assert provider['NSExtension']['NSExtensionFileProviderDocumentGroup'] == info['TramesAppGroup']
    assert not any('/_CodeSignature/' in n or n.endswith('embedded.mobileprovision') for n in app.namelist())
    files = {n.removeprefix(root): app.read(n) for n in app.namelist() if n.startswith(root) and not n.endswith('/')}
    assert not any('coremark' in n.lower() and not n.endswith('CoreMark-LICENSE.txt') for n in files)
    prefix = 'build/device/Build/Products/Release-iphoneos/Trames.app/'
    compiled = {n.removeprefix(prefix): built.read(n) for n in built.namelist() if n.startswith(prefix) and not n.endswith('/') and not n.removeprefix(prefix).startswith('GuestVulkan/wayland-acceptance/')}
    assert files == compiled, 'Every packaged file must equal the tested Release device app'
    assert not any(n.startswith('GuestVulkan/wayland-acceptance/') for n in files)
    for archive, directory, library, key in [(musl, 'GuestMusl', 'libc.so', 'libc_sha256'), (vulkan, 'GuestVulkan', 'libtrames_vulkan_icd.so', 'icd_sha256')]:
        manifest = json.loads(archive.read('manifest.json'))
        elf = archive.read(library)
        assert elf[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', elf, 18)[0] == 183
        assert sha(elf) == manifest[key] and files[directory + '/' + library] == elf
        assert files[directory + '/manifest.json'] == archive.read('manifest.json')
        assert b'Version 3' in files[directory + '/GPL-3.0.txt']
    with tarfile.open(fileobj=io.BytesIO(musl.read('musl-hybrid-1.2.5-source.tar.gz'))) as archive:
        patch = json.load(archive.extractfile('./trames-patch.json'))
        for path, digest in patch['inputs_sha256'].items():
            current = subprocess.check_output(['git', 'show', source + ':' + path])
            assert sha(current) == digest == sha(archive.extractfile('./source/' + path).read()), path
    with tarfile.open(fileobj=io.BytesIO(vulkan.read('vulkan-native-upstream-source.tar.gz'))) as archive:
        pinned = json.load(archive.extractfile('vulkan-upstream.json'))
        assert pinned == json.loads(Path('scripts/vulkan-upstream.json').read_text())
        for name, record in pinned['archives'].items():
            if name != 'moltenvk-all.tar':
                assert sha(archive.extractfile('upstream/' + name).read()) == record['sha256']
        assert archive.extractfile('TRAMES-GPL-3.0.txt').read() == Path('LICENSE').read_bytes()
    with tempfile.TemporaryDirectory(prefix='trames-release-1.1-') as temporary:
        directory = Path(temporary)
        ipa = directory / f'Trames-{VERSION}-build{BUILD}-unsigned.ipa'; ipa.write_bytes(data)
        checksum = ipa.with_suffix('.ipa.sha256'); checksum.write_text(f'{sha(data)}  {ipa.name}\n')
        proof = directory / f'Trames-{VERSION}-verification.json'
        report = {**config, 'tag_commit': head, 'all_packaged_files_match_device_build': len(files), 'main_binary_sha256': executable_sha, 'file_provider_binary_sha256': provider_sha, 'licenses_and_paired_sources_verified': True, 'artifacts': [outer_meta, built_meta, musl_meta, vulkan_meta], 'ci': [apple['html_url'], workspace['html_url'], linux['html_url']]}
        proof.write_text(json.dumps(report, indent=2) + '\n')
        assets = [ipa, checksum, proof]
        for archive, name in [(musl, 'musl-hybrid-1.2.5-source.tar.gz'), (vulkan, 'vulkan-native-upstream-source.tar.gz')]:
            path = directory / name; path.write_bytes(archive.read(name)); assets.append(path)
        source_archive = directory / f'Trames-{VERSION}-source.tar.gz'
        source_archive.write_bytes(subprocess.check_output(['git', 'archive', '--format=tar.gz', '--prefix=Trames-' + VERSION + '/', head])); assets.append(source_archive)
        notes = directory / 'notes.md'; notes.write_text(Path('docs/validation/release-1.1-notes.md').read_text() + f'\n\nIPA SHA256: `{sha(data)}`\n\n源码：`{source}`；正式tag：`{head}`。\n\nApple：{apple["html_url"]}\nLinux：{linux["html_url"]}\n')
        if release:
            existing = {a['name'] for a in release['assets']}
            for path in assets:
                if path.name in existing:
                    target = directory / ('existing-' + path.name)
                    gh('release', 'download', tag, '--repo', REPO, '--pattern', path.name, '--output', target)
                    assert sha(target.read_bytes()) == sha(path.read_bytes())
                else:
                    gh('release', 'upload', tag, path, '--repo', REPO)
        else:
            gh('release', 'create', tag, *assets, '--repo', REPO, '--draft', '--target', head, '--title', f'Trames {VERSION} (build {BUILD})', '--notes-file', notes)
        gh('release', 'edit', tag, '--repo', REPO, '--draft=false', '--latest')
        for path in assets:
            downloaded = directory / ('published-' + path.name)
            gh('release', 'download', tag, '--repo', REPO, '--pattern', path.name, '--output', downloaded)
            assert sha(downloaded.read_bytes()) == sha(path.read_bytes()), path.name
        print(f'https://github.com/{REPO}/releases/tag/{tag}')


if __name__ == '__main__':
    main()
