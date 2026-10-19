#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the exact Release app and paired sources before publishing 1.2.0."""
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import subprocess
import tarfile
import tempfile

spec = importlib.util.spec_from_file_location('previous_release', Path(__file__).with_name('publish-ios-release-1.1.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
base.VERSION, base.BUILD = '1.2.0', '33'
gh, api, sha, artifact = base.gh, base.api, base.sha, base.artifact
REPO, VERSION, BUILD = base.REPO, base.VERSION, base.BUILD


def main():
    config = json.loads(Path('docs/validation/release-1.2.json').read_text())
    assert config['status'] == 'verified' and config['version'] == VERSION and str(config['build']) == BUILD
    source = config['source_commit']
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    subprocess.run(['git', 'merge-base', '--is-ancestor', source, head], check=True)
    delta = subprocess.check_output(['git', 'diff', '--name-only', source, head], text=True).splitlines()
    allowed = {'.github/ios-validation-plan.json', '.github/workflows/release-1.2.yml', 'scripts/publish-ios-release-1.2.py', 'README.md'}
    assert all(p in allowed or p.startswith('docs/') for p in delta), 'Compiled/build inputs changed after the device build'
    apple, steps = base.checked_run(config['apple_run'], source, 'ios.yml', 'apple-build')
    assert {'Install exact Metal-validated EGL and ICD with paired sources', 'Link unsigned iPad device app', 'Package unsigned device IPA for sideload signing'} <= steps
    plan = json.loads(subprocess.check_output(['git', 'show', source + ':.github/ios-validation-plan.json']))['device_build']
    graphics = plan['zink']
    for name in ['zink-wayland-linux', 'zink-wayland-apple']:
        base.checked_run(graphics['run'], graphics['sha'], 'linux.yml', name)
    base.checked_run(plan['guest']['run'], plan['guest']['sha'], 'ios.yml', 'guest-musl')
    base.checked_run(plan['frontend']['run'], plan['frontend']['sha'], 'ios.yml', 'apple-build')
    outer, outer_meta = artifact(config['apple_run'], 'Trames-unsigned-sideload')
    built, built_meta = artifact(config['apple_run'], 'unsigned-ipad-build-and-project')
    vk, vk_meta = artifact(config['apple_run'], 'vulkan-compute-driver-and-native-source')
    musl, musl_meta = artifact(plan['guest']['run'], 'full-musl-library-and-source')
    name = next(n for n in outer.namelist() if n.endswith('.ipa'))
    data = outer.read(name)
    assert outer.read(name + '.sha256').decode().split()[0] == sha(data) == config['ipa_sha256']
    app = base.zipfile.ZipFile(io.BytesIO(data)); assert app.testzip() is None
    root = 'Payload/Trames.app/'
    info, executable_sha = base.verify_bundle(app, root)
    assert info['CFBundleIdentifier'] == 'org.trames.Trames' and info['MinimumOSVersion'] == '17.0'
    providers = [n.removesuffix('Info.plist') for n in app.namelist() if '/PlugIns/' in n and n.endswith('.appex/Info.plist')]
    assert len(providers) == 1
    provider, provider_sha = base.verify_bundle(app, providers[0])
    assert provider['CFBundleIdentifier'] == 'org.trames.Trames.FileProvider'
    assert provider['TramesAppGroup'] == info['TramesAppGroup'] == 'group.org.trames.Trames'
    assert provider['NSExtension']['NSExtensionFileProviderDocumentGroup'] == info['TramesAppGroup']
    assert not any('/_CodeSignature/' in n or n.endswith('embedded.mobileprovision') for n in app.namelist())
    files = {n.removeprefix(root): app.read(n) for n in app.namelist() if n.startswith(root) and not n.endswith('/')}
    assert not any('coremark' in n.lower() and not n.endswith('CoreMark-LICENSE.txt') for n in files)
    prefix = 'build/device/Build/Products/Release-iphoneos/Trames.app/'
    compiled = {n.removeprefix(prefix): built.read(n) for n in built.namelist() if n.startswith(prefix) and not n.endswith('/') and not n.removeprefix(prefix).startswith('GuestVulkan/wayland-acceptance/')}
    assert files == compiled, 'Packaged files differ from the compiled device app'
    reused = json.loads(vk.read('ipa-guest-reuse.json'))
    assert reused['source_commit'] == source and reused['graphics'] == graphics and reused['guest'] == plan['guest']
    assert reused['files_verified'] == 459 and reused['paired_musl_sources_verified'] and reused['original_moltenvk_pins_verified']
    for archive, directory, library, key in [(musl, 'GuestMusl', 'libc.so', 'libc_sha256'), (vk, 'GuestVulkan', 'libtrames_vulkan_icd.so', 'icd_sha256')]:
        manifest = json.loads(archive.read('manifest.json')); elf = archive.read(library)
        assert elf[:6] == b'\x7fELF\x02\x01' and int.from_bytes(elf[18:20], 'little') == 183
        assert sha(elf) == manifest[key] and files[directory + '/' + library] == elf
        assert files[directory + '/manifest.json'] == archive.read('manifest.json')
        assert b'Version 3' in files[directory + '/GPL-3.0.txt']
    egl = json.loads(vk.read('egl-manifest.json'))
    assert files['GuestVulkan/libEGL.so.1'] == vk.read('libEGL.so.1') and sha(vk.read('libEGL.so.1')) == egl['egl_sha256'] == reused['egl_sha256']
    assert files['GuestVulkan/egl-manifest.json'] == vk.read('egl-manifest.json')
    for path in vk.namelist():
        if path.startswith('Notices/Mesa-EGL/') and not path.endswith('/'):
            assert files['GuestVulkan/' + path] == vk.read(path)
    with tarfile.open(fileobj=io.BytesIO(musl.read('musl-hybrid-1.2.5-source.tar.gz'))) as archive:
        patch = json.load(archive.extractfile('./trames-patch.json'))
        for path, digest in patch['inputs_sha256'].items():
            assert sha(subprocess.check_output(['git', 'show', source + ':' + path])) == digest == sha(archive.extractfile('./source/' + path).read())
    with tarfile.open(fileobj=io.BytesIO(vk.read('vulkan-native-upstream-source.tar.gz'))) as archive:
        pinned = json.load(archive.extractfile('vulkan-upstream.json'))
        assert pinned == json.loads(Path('scripts/vulkan-upstream.json').read_text())
        for path, record in pinned['archives'].items():
            if path != 'moltenvk-all.tar': assert sha(archive.extractfile('upstream/' + path).read()) == record['sha256']
    with tarfile.open(fileobj=io.BytesIO(vk.read('mesa-egl-25.1.9-source.tar.gz'))) as archive:
        assert sha(archive.extractfile('upstream/mesa-25.1.9.tar.xz').read()) == egl['source_sha256']
        assert sha(archive.extractfile(egl['patch']).read()) == egl['patch_sha256']
        for path in [egl['patch'], 'scripts/build-zink-egl.py', 'scripts/zink-egl-requirements.txt', 'LICENSE']:
            assert archive.extractfile(path).read() == subprocess.check_output(['git', 'show', source + ':' + path])
    tag = 'v' + VERSION
    present = subprocess.run(['gh', 'release', 'view', tag, '--repo', REPO, '--json', 'url,isDraft,targetCommitish,assets'], capture_output=True)
    release = json.loads(present.stdout) if present.returncode == 0 else None
    if release and not release['isDraft']:
        assert any(a['name'] == f'Trames-{VERSION}-build{BUILD}-unsigned.ipa' and a.get('digest') == 'sha256:' + sha(data) for a in api(f'repos/{REPO}/releases/tags/{tag}')['assets'])
        print(release['url']); return
    if release: assert release['targetCommitish'] == head
    for ref in api(f'repos/{REPO}/git/matching-refs/tags/{tag}'):
        if ref['ref'] == 'refs/tags/' + tag:
            target = ref['object']
            if target['type'] == 'tag': target = api(f"repos/{REPO}/git/tags/{target['sha']}")['object']
            assert target['sha'] == head, 'Never move an existing release tag'
    with tempfile.TemporaryDirectory(prefix='trames-release-1.2-') as temporary:
        directory = Path(temporary)
        ipa = directory / f'Trames-{VERSION}-build{BUILD}-unsigned.ipa'; ipa.write_bytes(data)
        checksum = ipa.with_suffix('.ipa.sha256'); checksum.write_text(f'{sha(data)}  {ipa.name}\n')
        proof = directory / f'Trames-{VERSION}-verification.json'
        proof.write_text(json.dumps({**config, 'tag_commit': head, 'all_packaged_files_match_device_build': len(files), 'main_binary_sha256': executable_sha, 'file_provider_binary_sha256': provider_sha, 'guest_reuse': reused, 'artifacts': [outer_meta, built_meta, vk_meta, musl_meta], 'licenses_and_paired_sources_verified': True}, indent=2) + '\n')
        assets = [ipa, checksum, proof]
        for archive, name in [(musl, 'musl-hybrid-1.2.5-source.tar.gz'), (vk, 'vulkan-native-upstream-source.tar.gz'), (vk, 'mesa-egl-25.1.9-source.tar.gz')]:
            path = directory / name; path.write_bytes(archive.read(name)); assets.append(path)
        sources = directory / f'Trames-{VERSION}-source.tar.gz'
        sources.write_bytes(subprocess.check_output(['git', 'archive', '--format=tar.gz', '--prefix=Trames-' + VERSION + '/', head])); assets.append(sources)
        notes = directory / 'notes.md'; notes.write_text(Path('docs/validation/release-1.2-notes.md').read_text() + f'\n\nIPA SHA256: `{sha(data)}`\n\n应用源码：`{source}`；正式tag：`{head}`。\n\n构建：{apple["html_url"]}\n图形验收：https://github.com/{REPO}/actions/runs/{graphics["run"]}\n')
        if release:
            existing = {a['name'] for a in release['assets']}
            for path in assets:
                if path.name in existing:
                    old = directory / ('existing-' + path.name); gh('release', 'download', tag, '--repo', REPO, '--pattern', path.name, '--output', old)
                    assert sha(old.read_bytes()) == sha(path.read_bytes())
                else: gh('release', 'upload', tag, path, '--repo', REPO)
        else:
            gh('release', 'create', tag, *assets, '--repo', REPO, '--draft', '--target', head, '--title', f'Trames {VERSION} (build {BUILD})', '--notes-file', notes)
        gh('release', 'edit', tag, '--repo', REPO, '--draft=false', '--latest')
        for path in assets:
            downloaded = directory / ('published-' + path.name); gh('release', 'download', tag, '--repo', REPO, '--pattern', path.name, '--output', downloaded)
            assert sha(downloaded.read_bytes()) == sha(path.read_bytes()), path.name
        print(f'https://github.com/{REPO}/releases/tag/{tag}')


if __name__ == '__main__':
    main()
