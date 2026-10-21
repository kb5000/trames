#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Publish the existing verified iPad build and paired sources as release 1.3.0."""
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import subprocess
import tarfile
import tempfile
import zipfile

spec = importlib.util.spec_from_file_location('release_base', Path(__file__).with_name('publish-ios-release-1.1.py'))
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
base.VERSION, base.BUILD = '1.3.0', '35'
gh, api, sha, artifact = base.gh, base.api, base.sha, base.artifact
REPO, VERSION, BUILD = base.REPO, base.VERSION, base.BUILD
TAG = 'v1.3.0'


def paired_sources(raw, names):
    with tarfile.open(fileobj=io.BytesIO(raw)) as source:
        for archived, local in names:
            assert source.extractfile(archived).read() == Path(local).read_bytes(), archived


def main():
    config = json.loads(Path('docs/validation/ipad-1.3-build35.json').read_text())
    assert config['status'] == 'verified' and (config['version'], str(config['build'])) == (VERSION, BUILD)
    source = config['source_commit']
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    subprocess.run(['git', 'merge-base', '--is-ancestor', source, head], check=True)
    delta = subprocess.check_output(['git', 'diff', '--name-only', source, head], text=True).splitlines()
    allowed = {'.github/ios-validation-plan.json', 'scripts/ios-ci-scope.py', '.github/workflows/linux.yml',
               '.github/workflows/publish-ios-build35.yml', 'scripts/publish-ios-build35.py'}
    assert all(p in allowed or p.startswith('docs/') for p in delta), 'Device build inputs changed'
    apple, steps = base.checked_run(config['apple_run'], source, 'ios.yml', 'apple-build')
    required = {'Install exact Metal-validated EGL and ICD with paired sources',
                'Link unsigned iPad simulator app', 'Exercise peer tabs and floating window gestures first',
                'Test affected frontend paths or the full simulator suite', 'Export simulator test summary',
                'Link unsigned iPad device app', 'Package unsigned device IPA for sideload signing'}
    assert required <= steps
    base.checked_run(config['apple_run'], source, 'ios.yml', 'device-xwayland')
    plan = json.loads(subprocess.check_output(['git', 'show', source + ':.github/ios-validation-plan.json']))['device_build']
    for record, names in [(plan['layout_runtime'], ['firefox-layout', 'firefox-layout-apple']),
                          (plan['zink'], ['zink-wayland-linux', 'zink-wayland-apple'])]:
        for name in names: base.checked_run(record['run'], record['sha'], 'linux.yml', name)
    base.checked_run(plan['guest']['run'], plan['guest']['sha'], 'ios.yml', 'guest-musl')
    outer, outer_meta = artifact(config['apple_run'], 'Trames-unsigned-sideload')
    built, built_meta = artifact(config['apple_run'], 'unsigned-ipad-build-and-project')
    native, native_meta = artifact(config['apple_run'], 'vulkan-compute-driver-and-native-source')
    musl, musl_meta = artifact(plan['guest']['run'], 'full-musl-library-and-source')
    name = next(n for n in outer.namelist() if n.endswith('.ipa'))
    data = outer.read(name)
    assert sha(data) == config['ipa_sha256'] == outer.read(name + '.sha256').decode().split()[0]
    app = zipfile.ZipFile(io.BytesIO(data)); assert app.testzip() is None
    root = 'Payload/Trames.app/'
    info, executable_sha = base.verify_bundle(app, root)
    assert info['CFBundleIdentifier'] == 'org.trames.Trames' and info['MinimumOSVersion'] == '17.0'
    provider_root = next(n.removesuffix('Info.plist') for n in app.namelist() if n.endswith('.appex/Info.plist'))
    provider, provider_sha = base.verify_bundle(app, provider_root)
    assert provider['CFBundleIdentifier'] == 'org.trames.Trames.FileProvider'
    assert provider['TramesAppGroup'] == info['TramesAppGroup'] == 'group.org.trames.Trames'
    assert provider['NSExtension']['NSExtensionFileProviderDocumentGroup'] == info['TramesAppGroup']
    assert not any('/_CodeSignature/' in n or n.endswith('embedded.mobileprovision') for n in app.namelist())
    files = {n.removeprefix(root): app.read(n) for n in app.namelist() if n.startswith(root) and not n.endswith('/')}
    prefix = 'build/device/Build/Products/Release-iphoneos/Trames.app/'
    compiled = {n.removeprefix(prefix): built.read(n) for n in built.namelist() if n.startswith(prefix) and not n.endswith('/') and not n.removeprefix(prefix).startswith('GuestVulkan/wayland-acceptance/')}
    assert files == compiled, 'Packaged files differ from the device build'
    assert not any(n.startswith('GuestVulkan/wayland-acceptance/') for n in files)
    assert not any('coremark' in n.lower() and not n.endswith('CoreMark-LICENSE.txt') for n in files)
    for marker in (b'TRAMES_FIREFOX_FRAME_DUMP', b'FFGPU_CAPTURE_OK', b'FFMMAPA'):
        assert marker not in files['Trames'], 'Private Firefox instrumentation must not ship'
    reuse = json.loads(native.read('ipa-guest-reuse.json'))
    assert reuse['source_commit'] == source and reuse['graphics'] == plan['zink'] and reuse['guest'] == plan['guest']
    for key, filename, archive in [('icd_sha256', 'libtrames_vulkan_icd.so', native),
                                   ('egl_sha256', 'libEGL.so.1', native), ('musl_sha256', 'libc.so', musl)]:
        folder = 'GuestMusl' if archive is musl else 'GuestVulkan'
        assert sha(archive.read(filename)) == config['guest_library_sha256'][filename] == sha(files[folder + '/' + filename])
        assert reuse[key] == sha(archive.read(filename))
    for filename, key in [('Xwayland-native', 'executable_sha256'), ('trames-xwayland', 'sha256')]:
        manifest_name = 'xwayland-native-manifest.json' if filename == 'Xwayland-native' else 'launcher-manifest.json'
        manifest = json.loads(native.read(manifest_name))
        assert sha(native.read(filename)) == manifest[key] == sha(files['GuestVulkan/' + filename])
    native_names = [(p, p) for p in subprocess.check_output(['git', 'ls-files', 'patches/moltenvk',
                    'crates/trames-vulkan/native/transform-feedback'], text=True).splitlines()]
    native_names += [('prepare-vulkan.py', 'scripts/prepare-vulkan.py'),
                     ('scripts/build-moltenvk.py', 'scripts/build-moltenvk.py'),
                     ('scripts/build-vulkan-native-tf.py', 'scripts/build-vulkan-native-tf.py')]
    paired_sources(native.read('vulkan-native-upstream-source.tar.gz'), native_names)
    with tarfile.open(fileobj=io.BytesIO(native.read('vulkan-native-upstream-source.tar.gz'))) as closure:
        pins = json.loads(closure.extractfile('vulkan-upstream.json').read())
        assert pins == json.loads(Path('scripts/vulkan-upstream.json').read_text())
        for n, pin in pins['archives'].items():
            if n != 'moltenvk-all.tar': assert sha(closure.extractfile('upstream/' + n).read()) == pin['sha256']
    paired_sources(native.read('xwayland-native-source.tar.gz'), [(p, p) for p in
        ('patches/xwayland/0001-native-wayland-egl-wsi.patch', 'accelerator/xwayland/native-wsi.c', 'accelerator/xwayland/native-wsi.h')])
    paired_sources(native.read('xwayland-launcher-source.tar.gz'), [(p, p) for p in
        ('accelerator/xwayland/launcher.c', 'scripts/build-xwayland-launcher.sh', 'LICENSE')])
    paired_sources(native.read('mesa-egl-25.1.9-source.tar.gz'), [(p, p) for p in
        ('patches/mesa/0001-wayland-kopper-native-wsi.patch', 'scripts/build-zink-egl.py', 'LICENSE')])
    with tarfile.open(fileobj=io.BytesIO(musl.read('musl-hybrid-1.2.5-source.tar.gz'))) as closure:
        inputs = json.load(closure.extractfile('./trames-patch.json'))['inputs_sha256']
        for n, digest in inputs.items(): assert sha(closure.extractfile('./source/' + n).read()) == digest == sha(Path(n).read_bytes())
    proof = dict(source_commit=source, tag_commit=head, apple_run=config['apple_run'], ipa_sha256=sha(data),
                 main_binary_sha256=executable_sha, file_provider_binary_sha256=provider_sha,
                 all_packaged_files_match_build=len(files), paired_sources_verified=True,
                 guest_library_sha256=config['guest_library_sha256'], physical_ipad_tested=False,
                 artifacts=[outer_meta, built_meta, native_meta, musl_meta])
    with tempfile.TemporaryDirectory(prefix='trames-build35-') as directory:
        directory = Path(directory); assets = []
        def save(n, b):
            p = directory / n; p.write_bytes(b); assets.append(p); return p
        save(f'Trames-{VERSION}-build{BUILD}-unsigned.ipa', data)
        save('build35-verification.json', (json.dumps(proof, indent=2)+'\n').encode())
        for archive, n in [(musl, 'musl-hybrid-1.2.5-source.tar.gz'),
                           *[(native, n) for n in ('vulkan-native-upstream-source.tar.gz', 'mesa-egl-25.1.9-source.tar.gz',
                              'xwayland-native-source.tar.gz', 'xwayland-launcher-source.tar.gz')]]:
            save(n, archive.read(n))
        # Corresponding build source, without historical diagnostic datasets.
        paths = ['Cargo.toml', 'Cargo.lock', 'LICENSE', 'README.md', 'AGENTS.md', 'rust-toolchain.toml',
                 'crates', 'accelerator', 'vendor', 'patches', '.cargo', 'scripts', 'apps/ios',
                 'tests', '.github']
        save('Trames-1.3.0-build35-source.tar.gz', subprocess.check_output([
            'git', 'archive', '--format=tar.gz', '--prefix=Trames-1.3.0-build35/', head, '--', *paths]))
        save('SHA256SUMS', ''.join(sha(p.read_bytes())+'  '+p.name+'\n' for p in assets).encode())
        notes = directory / 'notes.md'; notes.write_text(Path('docs/validation/ipad-1.3-build35-notes.md').read_text()+
            f'\n\nIPA SHA-256: `{sha(data)}`\n\nApp source: `{source}`\n\nBuild: {apple["html_url"]}\n')
        existing = subprocess.run(['gh', 'release', 'view', TAG, '--repo', REPO, '--json', 'assets,isDraft,isPrerelease,targetCommitish,url'], capture_output=True)
        if existing.returncode == 0:
            release = json.loads(existing.stdout)
            assert not release['isPrerelease'] and release['targetCommitish'] == head
            if not release['isDraft']:
                print(release['url']); print('Published release retained without changes.'); return
            names = {a['name'] for a in release['assets']}
            for p in assets:
                if p.name in names:
                    old = directory / ('existing-' + p.name); gh('release', 'download', TAG, '--repo', REPO, '--pattern', p.name, '--output', old)
                    assert sha(old.read_bytes()) == sha(p.read_bytes()), 'Existing release asset differs'
                else: gh('release', 'upload', TAG, p, '--repo', REPO)
        else:
            refs = api(f'repos/{REPO}/git/matching-refs/tags/{TAG}')
            for ref in refs:
                if ref['ref'] == 'refs/tags/' + TAG:
                    target = ref['object']
                    if target['type'] == 'tag': target = api(f"repos/{REPO}/git/tags/{target['sha']}")['object']
                    assert target['sha'] == head, 'Never move an existing tag'
            gh('release', 'create', TAG, *assets, '--repo', REPO, '--draft', '--target', head,
               '--title', 'Trames 1.3.0 (build 35)', '--notes-file', notes)
        published = api(f'repos/{REPO}/releases/tags/{TAG}')
        uploaded = {a['name']: a for a in published['assets']}
        for p in assets:
            assert uploaded[p.name]['size'] == p.stat().st_size
            digest = uploaded[p.name].get('digest')
            if digest:
                assert digest == 'sha256:' + sha(p.read_bytes()), p.name
            else:
                downloaded = directory / ('published-' + p.name)
                gh('release', 'download', TAG, '--repo', REPO, '--pattern', p.name, '--output', downloaded)
                assert sha(downloaded.read_bytes()) == sha(p.read_bytes()), p.name
        gh('release', 'edit', TAG, '--repo', REPO, '--draft=false', '--latest')
        print(f'https://github.com/{REPO}/releases/tag/{TAG}')


if __name__ == '__main__':
    main()
