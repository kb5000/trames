#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build pinned original MoltenVK with verified minimal image/query corrections.

Run prepare-vulkan.py --apple first. Never alter verified upstream archives or
silently fall back to the unpatched driver. stdout is the static-library folder;
full Xcode output and source/toolchain/library hashes remain in the build folder.
"""
import argparse
import difflib
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT/'patches/moltenvk/0001-use-source-mip-depth-for-volume-blits.patch'
TIMESTAMP_PATCH = ROOT/'patches/moltenvk/0002-require-native-gpu-timestamp-counters.patch'
MANIFEST = ROOT/'scripts/vulkan-upstream.json'
CACHE = ROOT/'target/vulkan-upstream'
TRANSFER = Path('MoltenVK/MoltenVK/Commands/MVKCmdTransfer.mm')
OLD = 'VkExtent3D srcExtent = _srcImage->getExtent3D(srcPlaneIndex, mvkIBR.region.dstSubresource.mipLevel);'
NEW = OLD.replace('dstSubresource', 'srcSubresource')
DEVICE = Path('MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm')
TF_PATCH = ROOT/'patches/moltenvk/0003-native-gpu-transform-feedback.patch'
CROSS_PATCH = ROOT/'patches/moltenvk/0004-allow-native-tf-entry-arguments.patch'
NATIVE_TF = ROOT/'crates/trames-vulkan/native/transform-feedback'


def patch_paths(patch):
    return [Path(line[6:]) for line in patch.read_text().splitlines() if line.startswith('+++ b/')]


def apply_tf_patch(source):
    paths = patch_paths(TF_PATCH) + patch_paths(CROSS_PATCH)
    before = {str(p): digest(source/p) for p in paths}
    for patch in (TF_PATCH, CROSS_PATCH):
        subprocess.run(['patch', '-s', '-p1', '-i', str(patch)], cwd=source, input=b'', check=True)
    after = {str(p): digest(source/p) for p in paths}
    target = source/'TramesTF'
    shutil.copytree(NATIVE_TF, target)
    shader = (target/'assemble.metal').read_text()
    assert ')TRAMESTF"' not in shader
    (target/'tf-utility-source.inc').write_text('static NSString* _TramesTFCommandShaderSource = @R"TRAMESTF(\n'+shader+'\n)TRAMESTF";\n')
    return dict(before=before, after=after)


def apply_timestamp_patch(source):
    path = source/DEVICE
    original = path.read_text()
    old = 'qfProps.timestampValidBits = 64;'
    new = 'qfProps.timestampValidBits = _timestampMTLCounterSet ? 64 : 0;'
    assert original.count(old) == 1 and new not in original
    expected = original.replace(old, new)
    start = expected.index('void MVKPhysicalDevice::initCounterSets() {')
    end = expected.index('\n}\n', start)
    expected = expected[:end] + '\n\t// Completion-time CPU timestamps cannot feed GPU query copies.\n\t_properties.limits.timestampComputeAndGraphics = (_timestampMTLCounterSet != nil);' + expected[end:]
    diff = ''.join(difflib.unified_diff(original.splitlines(True), expected.splitlines(True),
        fromfile='a/'+str(DEVICE), tofile='b/'+str(DEVICE)))
    assert TIMESTAMP_PATCH.read_text().endswith(diff), 'Unexpected timestamp capability patch'
    subprocess.run(['patch', '-s', '-p1', '-i', str(TIMESTAMP_PATCH)], cwd=source, input=b'', check=True)
    assert path.read_text() == expected
    return hashlib.sha256(original.encode()).hexdigest(), digest(path)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def apply_patch(source):
    path = source/TRANSFER
    original = path.read_text()
    assert original.count(OLD) == 1 and NEW not in original, 'Unexpected pinned MoltenVK source'
    expected = original.replace(OLD, NEW)
    diff = ''.join(difflib.unified_diff(original.splitlines(True), expected.splitlines(True),
        fromfile='a/'+str(TRANSFER), tofile='b/'+str(TRANSFER)))
    assert PATCH.read_text().endswith(diff), 'Patch must contain exactly the source-mip correction'
    subprocess.run(['patch', '-s', '-p1', '-i', str(PATCH)], cwd=source, input=b'', check=True)
    assert path.read_text() == expected
    return hashlib.sha256(original.encode()).hexdigest(), digest(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--platform', choices=('macos', 'ios', 'ios-simulator'), default='macos')
    p.add_argument('--arch', choices=('arm64', 'x86_64'), default='arm64')
    p.add_argument('--verify-source', type=Path, help='verify the pinned archive and exact patch without Apple SDK')
    p.add_argument('--verify-cross-source', type=Path, help='pinned SPIRV-Cross archive for source-only verification')
    a = p.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    if a.verify_source:
        assert digest(a.verify_source) == manifest['archives']['moltenvk-source.tar.gz']['sha256']
        with tempfile.TemporaryDirectory(prefix='trames-mvk-patch-') as tmp, tarfile.open(a.verify_source) as archive:
            member = next(v for v in archive if v.name.endswith('/'+str(TRANSFER)))
            path = Path(tmp)/TRANSFER;path.parent.mkdir(parents=True)
            path.write_bytes(archive.extractfile(member).read())
            before, after = apply_patch(Path(tmp))
            member = next(v for v in archive if v.name.endswith('/'+str(DEVICE)))
            path = Path(tmp)/DEVICE;path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(archive.extractfile(member).read())
            device_before, device_after = apply_timestamp_patch(Path(tmp))
            for relative in patch_paths(TF_PATCH):
                if (Path(tmp)/relative).exists():
                    continue
                member = next(v for v in archive if v.name.endswith('/'+str(relative)))
                path = Path(tmp)/relative;path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(archive.extractfile(member).read())
            cross = a.verify_cross_source or CACHE/'moltenvk-dependency-SPIRV-Cross.tar.gz'
            assert digest(cross) == manifest['archives']['moltenvk-dependency-SPIRV-Cross.tar.gz']['sha256']
            with tarfile.open(cross) as cross_files:
                member = next(v for v in cross_files if v.name.endswith('/spirv_msl.hpp'))
                path = Path(tmp)/'External/SPIRV-Cross/spirv_msl.hpp';path.parent.mkdir(parents=True)
                path.write_bytes(cross_files.extractfile(member).read())
            native_tf = apply_tf_patch(Path(tmp))
            archive_name = next(v for v in archive.getnames() if v.endswith('/Templates/spirv-tools/build.zip'))
            with zipfile.ZipFile(io.BytesIO(archive.extractfile(archive_name).read())) as headers:
                assert headers.testzip() is None and 'build/OpenCLDebugInfo100.h' in headers.namelist()
            print(json.dumps(dict(original_sha256=before, patched_sha256=after, patch_sha256=digest(PATCH),
                original_device_sha256=device_before, patched_device_sha256=device_after, timestamp_patch_sha256=digest(TIMESTAMP_PATCH), native_tf=native_tf)))
        return
    assert platform.system() == 'Darwin', 'An Apple SDK host is required to build native MoltenVK'
    assert a.platform != 'ios' or a.arch == 'arm64', 'iOS device build requires arm64'
    sdk, destination, os_name = {
        'macos': ('macosx', 'macOS', 'macOS'), 'ios': ('iphoneos', 'iOS', 'iOS'),
        'ios-simulator': ('iphonesimulator', 'iOS Simulator', 'iOS'),
    }[a.platform]
    toolchain = subprocess.check_output(['xcodebuild', '-version'], text=True)
    sdk_version = subprocess.check_output(['xcrun', '--sdk', sdk, '--show-sdk-version'], text=True).strip()
    inputs = dict(platform=a.platform, arch=a.arch, xcode=toolchain, sdk=sdk_version,
                  patch_sha256=digest(PATCH), recipe_sha256=digest(Path(__file__)),
                  timestamp_patch_sha256=digest(TIMESTAMP_PATCH),
                  transform_feedback_patch_sha256=digest(TF_PATCH), cross_dispatch_patch_sha256=digest(CROSS_PATCH),
                  transform_feedback_sources={p.name:digest(p) for p in sorted(NATIVE_TF.iterdir()) if p.is_file()},
                  manifest_sha256=digest(MANIFEST), upstream_commit=manifest['moltenvk_commit'])
    key = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()[:20]
    out = ROOT/'target/moltenvk-patched'/(a.platform+'-'+a.arch+'-'+key)
    library = out/'libMoltenVK.a';record = out/'build.json'
    if library.is_file() and record.is_file():
        cached = json.loads(record.read_text())
        assert cached['inputs'] == inputs and cached['library_sha256'] == digest(library)
        print(out);return
    out.mkdir(parents=True, exist_ok=True)
    source = out/'source'
    if source.exists():
        shutil.rmtree(source)
    # Source archives and every dependency are exactly those already retained
    # for GPL distribution. Keep the original archives and extracted trees intact.
    projects = {}
    for name, item in manifest['archives'].items():
        if name in ('headers.tar.gz', 'moltenvk-all.tar'):
            continue
        archive = CACHE/name
        assert archive.is_file() and digest(archive) == item['sha256'], 'Run prepare-vulkan.py --apple: '+name
        extracted = CACHE/name.split('.')[0]
        projects[name] = next(path for path in extracted.iterdir() if path.is_dir())
    shutil.copytree(projects['moltenvk-source.tar.gz'], source, symlinks=True)
    for name, item in manifest['archives'].items():
        if 'destination' not in item:
            continue
        path = source/item['destination'];path.parent.mkdir(parents=True, exist_ok=True)
        if name in ('moltenvk-dependency-SPIRV-Tools.tar.gz', 'moltenvk-dependency-SPIRV-Cross.tar.gz'):
            shutil.copytree(projects[name], path, symlinks=True)
        else:
            path.symlink_to(projects[name], target_is_directory=True)
    # Match upstream fetchDependencies' default setup, without cloning or
    # regenerating tables from a different version. These headers are shipped
    # inside the verified original MoltenVK source archive.
    tools = source/'External/SPIRV-Tools'
    spirv_headers = tools/'external/spirv-headers'
    spirv_headers.symlink_to(projects['moltenvk-dependency-SPIRV-Headers.tar.gz'], target_is_directory=True)
    with zipfile.ZipFile(source/'Templates/spirv-tools/build.zip') as headers:
        assert headers.testzip() is None
        headers.extractall(tools)
    assert (tools/'build/OpenCLDebugInfo100.h').is_file()
    before, after = apply_patch(source)
    device_before, device_after = apply_timestamp_patch(source)
    native_tf = apply_tf_patch(source)
    common = ['-configuration', 'Release', '-sdk', sdk, 'ARCHS='+a.arch,
              'ONLY_ACTIVE_ARCH=YES', 'CODE_SIGNING_ALLOWED=NO',
              'SYMROOT='+str(out/'products'), 'OBJROOT='+str(out/'intermediates')]
    common.append('HEADER_SEARCH_PATHS=$(inherited) '+str(source/'TramesTF'))
    commands = [
        ['xcodebuild', '-jobs', '4', '-project', 'ExternalDependencies.xcodeproj', '-scheme', 'ExternalDependencies-'+os_name,
         '-destination', 'generic/platform='+destination, *common, 'build'],
        ['xcodebuild', '-jobs', '4', '-project', 'MoltenVK/MoltenVK.xcodeproj', '-target', 'MoltenVK-'+os_name+'-static', *common, 'build'],
    ]
    with (out/'xcodebuild.log').open('wb') as log:
        for command in commands:
            print('Building patched MoltenVK '+a.platform+' '+a.arch, file=sys.stderr, flush=True)
            result = subprocess.run(command, cwd=source, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                failed_log = (out/'xcodebuild.log').read_text(errors='replace')
                lines = failed_log.splitlines()
                for i, line in enumerate(lines):
                    if 'error:' in line:
                        print('\n'.join(lines[max(0, i-2):i+5]), file=sys.stderr)
                print(failed_log[-14000:], file=sys.stderr)
                raise SystemExit(result.returncode)
    # Xcode also copies the library under XCFrameworkStaging; select only
    # the SDK's real configuration product (Release[-iphoneos/-iphonesimulator]).
    products = list((out/'products').glob('Release*/libMoltenVK.a'))
    assert len(products) == 1, 'Expected one native static MoltenVK product: '+str(products)
    shutil.copyfile(products[0], library)
    actual = subprocess.check_output(['xcrun', 'lipo', '-archs', str(library)], text=True).split()
    assert actual == [a.arch], 'Native artifact architecture mismatch'
    record.write_text(json.dumps(dict(inputs=inputs, commands=commands, original_transfer_sha256=before,
        patched_transfer_sha256=after, original_device_sha256=device_before,
        patched_device_sha256=device_after, library_sha256=digest(library)), indent=2)+'\n')
    data = json.loads(record.read_text());data['native_transform_feedback']=native_tf
    record.write_text(json.dumps(data, indent=2)+'\n')
    print(out)


if __name__ == '__main__':
    main()
