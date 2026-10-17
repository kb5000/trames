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
    deadline = time.monotonic() + 95 * 60
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
    assert info['CFBundleVersion'] == '26' and version == '1.0.0'
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
    built_prefix = 'build/device/Build/Products/Release-iphoneos/Trames.app/'
    built = {n.removeprefix(built_prefix): build.read(n) for n in build.namelist()
             if n.startswith(built_prefix) and not n.endswith('/')}
    debug_prefix = 'GuestVulkan/wayland-acceptance/'
    excluded = {name for name in built if name.startswith(debug_prefix)}
    assert excluded, 'The original DEBUG acceptance inputs must exist in the device build'
    assert not any(name.startswith(debug_prefix) for name in files)
    assert files == {name: data for name, data in built.items() if name not in excluded}
    wayland_notices = source / 'apps/ios/Trames/Notices/Wayland-native-dependencies-NOTICES.txt'
    notice_files = [data for name, data in files.items() if name.split('/')[-1] == wayland_notices.name]
    assert notice_files == [wayland_notices.read_bytes()], 'The exact original Wayland notices must ship'
    manifest = json.loads(musl.read('manifest.json'))
    libc = musl.read('libc.so')
    assert sha(libc) == manifest['libc_sha256'] and files['GuestMusl/libc.so'] == libc
    assert libc[:5]==b'\x7fELF\x02' and struct.unpack_from('<H',libc,18)[0]==183
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
    assert not summaries, 'This release uses one affected workspace acceptance, not a repeated full simulator run'
    summary = dict(passedTests=0, failedTests=0, skippedTests=0, reused_baselines=['ipad-0.18-build23.json','ipad-0.18-build24.json','ipad-0.18-build25.json'])
    workspace = json.loads(archives['workspace'].read('workspace-interaction-summary.json'))
    assert workspace['passedTests'] == 1 and workspace['failedTests'] == 0 and workspace['skippedTests'] == 0
    log=archives['workspace'].read('workspace-interaction.log').decode(errors='replace')
    assert re.search(r'Test Case.*testMultipleAlpineTerminalsShareGuestAndSurviveClosingFirstWindow.*passed',log)
    assert b'NativeCoreMarkRunner' not in binary and b'NIOSSHHandler' not in binary
    def device_bundle(root):
        meta=plistlib.loads(app.read(root+'Info.plist'))
        assert meta['CFBundleShortVersionString']=='1.0.0' and meta['CFBundleVersion']=='26'
        assert meta['CFBundleSupportedPlatforms']==['iPhoneOS']
        assert (app.getinfo(root+meta['CFBundleExecutable']).external_attr >> 16) & stat.S_IXUSR
        image=app.read(root+meta['CFBundleExecutable']);magic,cpu,_,kind,count,size,_,_=struct.unpack_from('<8I',image)
        assert (magic,cpu,kind)==(0xfeedfacf,0x0100000c,2)
        offset=32;platforms=[]
        for _ in range(count):
            command,length=struct.unpack_from('<2I',image,offset);assert length>=8 and offset+length<=32+size
            assert command!=0x1d, 'Unsigned app/extension must not retain a code signature'
            if command==0x32:platforms.append(struct.unpack_from('<I',image,offset+8)[0])
            offset+=length
        assert platforms==[2]
        return meta
    device_bundle(prefix)
    providers=[n.removesuffix('Info.plist') for n in app.namelist() if n.startswith(prefix+'PlugIns/') and n.endswith('.appex/Info.plist')]
    assert len(providers)==1
    provider=device_bundle(providers[0]);ext=provider['NSExtension']
    assert provider['TramesAppGroup']==info['TramesAppGroup']=='group.org.trames.Trames'
    assert ext['NSExtensionPointIdentifier']=='com.apple.fileprovider-nonui'
    assert ext['NSExtensionPrincipalClass']=='RootfsFileProvider'
    assert ext['NSExtensionFileProviderDocumentGroup']==info['TramesAppGroup']
    assert not any('/_CodeSignature/' in name for name in app.namelist())
    sim = 'build/simulator/Build/Products/Debug-iphonesimulator/Trames.app/GuestMusl/libc.so'
    assert build.read(sim) == libc
    vulkan = archives['vulkan']
    gpu = json.loads(vulkan.read('manifest.json'))
    icd = vulkan.read('libtrames_vulkan_icd.so')
    assert files['GuestVulkan/libtrames_vulkan_icd.so'] == icd
    assert icd[:5]==b'\x7fELF\x02' and struct.unpack_from('<H',icd,18)[0]==183
    assert build.read('build/simulator/Build/Products/Debug-iphonesimulator/Trames.app/GuestVulkan/libtrames_vulkan_icd.so') == icd
    assert files['GuestVulkan/manifest.json'] == vulkan.read('manifest.json')
    assert sha(icd) == gpu['icd_sha256'] and gpu['profile'] == 65542
    assert gpu['wire_revision'] == 4 and gpu['default_enabled'] is False
    assert gpu['native_moltenvk'] == '1.4.2'
    assert len(re.findall(r'if\(!strcmp\(name,', (source / 'accelerator/vulkan/entrypoints.inc').read_text())) == 113
    for name in ['vulkan-texture', 'vulkan-texture.vert.spv', 'vulkan-texture.frag.spv', 'vulkan-present']:
        assert files['GuestVulkan/acceptance/' + name] == vulkan.read('acceptance/' + name)
    notices = [n for n in vulkan.namelist() if n.startswith('Notices/') and not n.endswith('/')]
    required_notices = {
        'Notices/Trames-Vulkan-GPL-3.0.txt',
        'Notices/moltenvk-source-LICENSE.txt',
        'Notices/moltenvk-dependency-SPIRV-Cross-LICENSE.txt',
        'Notices/moltenvk-dependency-SPIRV-Headers-LICENSE.txt',
        'Notices/moltenvk-dependency-SPIRV-Tools-LICENSE.txt',
        'Notices/moltenvk-dependency-Volk-LICENSE.md.txt',
        'Notices/moltenvk-dependency-Vulkan-Headers-LICENSE.md.txt',
        'Notices/moltenvk-dependency-Vulkan-Tools-LICENSE.txt.txt',
        'Notices/moltenvk-dependency-cereal-LICENSE.txt',
    }
    assert required_notices <= set(notices)
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
    report = dict(source_commit=source_commit, version=version, build='26',
        ipa_sha256=sha(data), ipa_bytes=len(data), bundle_id=info['CFBundleIdentifier'],
        minimum_os=info['MinimumOSVersion'], device_families=info['UIDeviceFamily'],
        device_architecture='arm64', mach_o_platform=2,
        all_packaged_files_match_device_build=len(files), executable_permission_verified=True,
        zip_crc_and_sha256_sidecar_verified=True, libc_sha256=sha(libc),
        musl_native_functions=19, source_inputs_verified=len(patch['inputs_sha256']),
        license_notices_verified=True, vulkan_icd_sha256=sha(icd), vulkan_profile=gpu['profile'],
        vulkan_public_entrypoints=113, moltenvk_version='1.4.2', vulkan_default_enabled=False,
        vulkan_source_and_notices_verified=True, wayland_notices_sha256=sha(wayland_notices.read_bytes()),
        debug_wayland_files_excluded=len(excluded), workspace_passed=workspace['passedTests'],
        simulator_remaining_passed=summary['passedTests'],
        simulator_passed=summary['passedTests'] + workspace['passedTests'], simulator_failed=0, file_provider_metadata_verified=True, unsigned_app_and_extension_verified=True)
    return data, report, summary, workspace


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository',required=True)
    args=parser.parse_args()
    assert args.repository=='kb5000/trames'
    if tomllib.loads(Path('Cargo.toml').read_text())['workspace']['package']['version'] != '1.0.0':
        print('This publisher only handles 1.0.0; nothing to publish.');return
    manifest=Path('docs/validation/release-1.0.json')
    if not manifest.exists():
        print('No verified 1.0.0 publication manifest; nothing to publish.')
        return
    config=json.loads(manifest.read_text())
    assert config['version']=='1.0.0' and config['build']==26 and config['status']=='verified'
    source_commit=config['source_commit'];assert re.fullmatch('[0-9a-f]{40}',source_commit)
    tag='v1.0.0'
    tag_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
    subprocess.run(['git','merge-base','--is-ancestor',source_commit,tag_commit],check=True)
    changed=subprocess.check_output(['git','diff','--name-only',source_commit,tag_commit],text=True).splitlines()
    allowed={'README.md','.github/ios-validation-plan.json','scripts/ios-ci-scope.py',
             'scripts/publish-ios-release-1.0.py','.github/workflows/release-1.0.yml','tests/integration/multiple-terminals.py','apps/ios/TramesUITests/LaunchTests.swift','apps/ios/TramesTests/WorkspaceInteractionTests.swift'}
    assert all(p.startswith('docs/') or p in allowed for p in changed), 'Application, guest and build sources must match the tested source'
    pr=api(f'repos/{args.repository}/pulls/28')
    assert pr['merged'] and pr['base']['ref']=='main' and pr['head']['repo']['full_name']==args.repository
    apple=wait_run(args.repository,config['apple_run']);linux=wait_run(args.repository,config['linux_run'])
    for run,path in [(apple,'.github/workflows/ios.yml'),(linux,'.github/workflows/linux.yml')]:
        expected_source=config.get('linux_source_commit',source_commit) if run is linux else source_commit
        assert run['head_sha']==expected_source and run['path']==path
        assert all(p.startswith('docs/') or p in allowed for p in subprocess.check_output(['git','diff','--name-only',source_commit,expected_source],text=True).splitlines()), 'Validation-only fixes must preserve every production source'
        assert run['head_repository']['full_name']==args.repository and run['event']=='pull_request'
    for run,job_name,required in [(apple,'apple-build',{'Exercise peer tabs and floating window gestures first','Export simulator test summary','Link unsigned iPad device app','Package unsigned device IPA for sideload signing'}),
                                  (linux,'targeted-runtime',{'Exercise multiple independent terminals in one original Alpine guest'})]:
        jobs=api(f"repos/{args.repository}/actions/runs/{run['id']}/jobs")['jobs']
        assert any(j['name']==job_name and j['conclusion']=='success' and required<={s['name'] for s in j['steps'] if s['conclusion']=='success'} for j in jobs)
    native_linux=api(f"repos/{args.repository}/actions/runs/{config['native_linux_run']}")
    assert native_linux['path']=='.github/workflows/linux.yml'
    native_apple=api(f"repos/{args.repository}/actions/runs/{config['native_apple_run']}")
    assert native_apple['path']=='.github/workflows/ios.yml'
    for native in (native_apple,native_linux):
        assert native['head_repository']['full_name']==args.repository
        diff=subprocess.check_output(['git','diff','--name-only',source_commit,native['head_sha']],text=True).splitlines()
        assert all(p.startswith('docs/') or p in allowed for p in diff), 'Reused native checks require identical production sources'
    apple_modules=api(f"repos/{args.repository}/actions/runs/{config['native_apple_run']}/jobs")['jobs']
    assert any(s['name']=='Validate affected native modules or the complete Apple core' and s['conclusion']=='success' for j in apple_modules if j['name']=='apple-build' for s in j['steps'])
    modules=api(f"repos/{args.repository}/actions/runs/{config['native_linux_run']}/jobs")['jobs']
    assert any(s['name']=='Check affected modules' and s['conclusion']=='success' for j in modules if j['name']=='targeted-runtime' for s in j['steps'])
    names={f'Trames-1.0.0-build26-unsigned.ipa',f'Trames-1.0.0-build26-unsigned.ipa.sha256',
           'Trames-1.0.0-verification.json','Trames-1.0.0-simulator.json',
           'musl-hybrid-1.2.5-source.tar.gz','vulkan-native-upstream-source.tar.gz'}
    existing=subprocess.run(['gh','release','view',tag,'--repo',args.repository,'--json','url,isDraft,targetCommitish,assets'],capture_output=True)
    release=json.loads(existing.stdout) if existing.returncode==0 else None
    if release and not release['isDraft']:
        assert names<={a['name'] for a in release['assets']}, 'Existing published release is incomplete'
        print(release['url']);print('Published release/tag/assets retained without modification.');return
    if release:assert release['targetCommitish']==tag_commit, 'Draft must belong to this exact release commit'
    refs=subprocess.check_output(['git','ls-remote','--tags',f'https://github.com/{args.repository}.git',f'refs/tags/{tag}',f'refs/tags/{tag}^{{}}'],text=True).splitlines()
    if refs:
        refs={ref:digest for digest,ref in (line.split() for line in refs)}
        assert refs.get(f'refs/tags/{tag}^{{}}',refs.get(f'refs/tags/{tag}'))==tag_commit, 'Never retarget an existing tag'
    with tempfile.TemporaryDirectory(prefix='trames-release-1.0-') as folder:
        directory=Path(folder);source=directory/'source'
        subprocess.run(['git','clone','--quiet','--no-checkout','--filter=blob:none',f'https://github.com/{args.repository}.git',str(source)],check=True)
        subprocess.run(['git','-C',str(source),'checkout','--quiet','--detach',source_commit],check=True)
        expected={'sideload':(config['apple_run'],'Trames-unsigned-sideload'),
                  'build':(config['apple_run'],'unsigned-ipad-build-and-project'),
                  'workspace':(config['apple_run'],'workspace-gesture-diagnostics'),
                  'musl':(config['reuse_guest_run'],'full-musl-library-and-source'),
                  'vulkan':(config['reuse_guest_run'],'vulkan-compute-driver-and-native-source')}
        archives={};metadata={}
        for kind,(run_id,name) in expected.items():
            artifacts=api(f'repos/{args.repository}/actions/runs/{run_id}/artifacts')['artifacts']
            artifact=next(a for a in artifacts if a['name']==name and not a['expired'])
            raw=gh('api',f"repos/{args.repository}/actions/artifacts/{artifact['id']}/zip")
            assert sha(raw)==artifact['digest'].removeprefix('sha256:')
            archive=zipfile.ZipFile(io.BytesIO(raw));assert archive.testzip() is None
            archives[kind]=archive;metadata[kind]={k:artifact[k] for k in ('id','name','digest','expires_at')}
        data,report,summary,workspace=verify(archives,source,source_commit)
        assert sha(data)==config['artifact']['sha256']
        report.update(tag_commit=tag_commit,publisher_source_commit=tag_commit,application_identical_at_tag=True,
                      publication_only_changed_paths=changed,artifacts=metadata,
                      ci={r['name']:{'id':r['id'],'url':r['html_url'],'head_sha':r['head_sha'],'conclusion':r['conclusion']} for r in (apple,linux)},
                      reuse_guest_run=config['reuse_guest_run'],local_checks=config['local_checks'],native_checks=config['native_checks'],
                      native_linux_run=config['native_linux_run'],native_apple_run=config['native_apple_run'],validation_source_commit=config.get('linux_source_commit',source_commit),
                      source_archives_sha256={name:sha(archives[kind].read(name)) for kind,name in [('musl','musl-hybrid-1.2.5-source.tar.gz'),('vulkan','vulkan-native-upstream-source.tar.gz')]})
        ipa=directory/'Trames-1.0.0-build26-unsigned.ipa';ipa.write_bytes(data)
        checksum=ipa.with_suffix('.ipa.sha256');checksum.write_text(f'{sha(data)}  {ipa.name}\n')
        proof=directory/'Trames-1.0.0-verification.json';proof.write_text(json.dumps(report,indent=2)+'\n')
        simulator=directory/'Trames-1.0.0-simulator.json';simulator.write_text(json.dumps({'affected':workspace,'unchanged_baselines':summary['reused_baselines']},indent=2)+'\n')
        musl=directory/'musl-hybrid-1.2.5-source.tar.gz';musl.write_bytes(archives['musl'].read(musl.name))
        vulkan=directory/'vulkan-native-upstream-source.tar.gz';vulkan.write_bytes(archives['vulkan'].read(vulkan.name))
        notes=directory/'release-notes.md';notes.write_text((source/'docs/validation/release-1.0-notes.md').read_text()+f'\n\n新增多终端 iPad 模拟器验收 {workspace["passedTests"]} passed / 0 failed / 0 skipped；Linux 原版 Alpine 多终端及受影响 native 检查通过。此前不受影响的成功检查按源码复用。\n\nApplication source: `{source_commit}`\nRelease tag: `{tag_commit}`（应用、guest 与构建源码相同）\nIPA SHA-256: `{sha(data)}`\nApple CI: {apple["html_url"]}\nLinux CI: {linux["html_url"]}\n')
        assets=[ipa,checksum,proof,simulator,musl,vulkan]
        if release:
            present={a['name'] for a in release['assets']}
            for asset in assets:
                if asset.name in present:
                    downloaded=directory/('existing-'+asset.name)
                    gh('release','download',tag,'--repo',args.repository,'--pattern',asset.name,'--output',downloaded)
                    assert sha(downloaded.read_bytes())==sha(asset.read_bytes()), asset.name
                else:gh('release','upload',tag,asset,'--repo',args.repository)
        else:
            gh('release','create',tag,*assets,'--repo',args.repository,'--draft','--target',tag_commit,'--title','Trames 1.0.0 (build 26)','--notes-file',notes)
        gh('release','edit',tag,'--repo',args.repository,'--draft=false')
        print(f'https://github.com/{args.repository}/releases/tag/{tag}')


if __name__=='__main__':
    main()

