#!/usr/bin/env python3
"""Reuse successful runtime/guest checks when only the iPad frontend changed."""
import json
import os
import subprocess
import urllib.request
import hashlib
from pathlib import Path


def frontend_path(path):
    return path.startswith(("apps/ios/", "docs/")) or path in {
        ".github/workflows/ios.yml", ".github/workflows/linux.yml",
        "scripts/ios-ci-scope.py", "AGENTS.md", ".github/ios-validation-plan.json",
        "scripts/prepare-ios-guest-artifacts.py", "tests/integration/ios-guest-artifacts.py",
        ".github/workflows/stabilization.yml",
    }


def paired_runtime_plan(plan_path):
    pin = Path('runtime-version.json')
    if not pin.exists():
        return True
    return json.loads(plan_path.read_text()).get('runtime_revision') == json.loads(pin.read_text())['revision']


def zink_metal_only_reuse(plan, repo, headers):
    """Reuse an exact guest/backend build while repairing only native MoltenVK."""
    reuse = plan.get('zink_metal_only_reuse')
    if not reuse:
        return None
    if plan['native_scope'] != 'zink-gles':
        raise RuntimeError('Metal-only repair requires GLES native acceptance')
    allowed = {'.github/ios-validation-plan.json', '.github/workflows/linux.yml',
               'scripts/ios-ci-scope.py', 'scripts/prepare-zink-native-bundle.py',
               'scripts/prepare-vulkan.py', 'scripts/build-moltenvk.py', 'scripts/build-ios.sh',
               'patches/moltenvk/0001-use-source-mip-depth-for-volume-blits.patch',
                               'patches/moltenvk/0002-require-native-gpu-timestamp-counters.patch', 'patches/moltenvk/0003-native-gpu-transform-feedback.patch', 'patches/moltenvk/0004-allow-native-tf-entry-arguments.patch', 'scripts/build-vulkan-native-tf.py', '.github/workflows/ios.yml', 'tests/integration/vulkan-native-tf-reference.py', 'tests/integration/vulkan-native-tf-runtime.py'}
    changed = subprocess.check_output(['git', 'diff', '--name-only', reuse['sha'], 'HEAD'], text=True).splitlines()
    if any(path not in allowed and not path.startswith(('docs/', 'crates/trames-vulkan/native/transform-feedback/', 'tests/fixtures/vulkan-native-tf')) for path in changed):
        raise RuntimeError('Metal-only repair changed the guest, runtime, GPU bridge, fixture or toolchain')

    def get(path):
        request = urllib.request.Request(f'https://api.github.com/repos/{repo}/{path}', headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    run = get(f"actions/runs/{reuse['run']}")
    if (run['head_sha'] != reuse['sha'] or run['head_repository']['full_name'] != repo
            or run['path'] != '.github/workflows/linux.yml'):
        raise RuntimeError('Metal repair reuse source/workflow mismatch')
    jobs = get(f"actions/runs/{reuse['run']}/jobs")['jobs']
    if next(j for j in jobs if j['name'] == 'zink-foundation')['conclusion'] != 'success':
        raise RuntimeError('Missing successful exact-source Linux GPU/Mesa acceptance')
    apple = next(j for j in jobs if j['name'] == 'zink-gles-apple')
    if not any(s['name'] == 'Verify exact guest inputs, Apple modules and iOS embedding'
               and s['conclusion'] == 'success' for s in apple['steps']):
        raise RuntimeError('Missing passed unchanged Apple module/iOS checks')
    if not any(a['name'] == 'zink-native-bundle' and not a['expired']
               for a in get(f"actions/runs/{reuse['run']}/artifacts")['artifacts']):
        raise RuntimeError('Missing source-bound original Mesa guest bundle')
    return reuse


def tf_cache_only_reuse(plan, repo, headers):
    reuse=plan.get('tf_cache_only_reuse')
    if not reuse:
        return False
    allowed={'.github/ios-validation-plan.json','.github/workflows/linux.yml','scripts/ios-ci-scope.py',
             'patches/moltenvk/0003-native-gpu-transform-feedback.patch',
             'tests/integration/vulkan-native-tf-runtime.py','tests/fixtures/vulkan-native-tf-cache.c'}
    changed=subprocess.check_output(['git','diff','--name-only',reuse['sha'],'HEAD'],text=True).splitlines()
    if any(p not in allowed and not p.startswith('docs/') for p in changed):
        raise RuntimeError('TF cache diagnostic repair changed runtime or GPU algorithms')
    path='patches/moltenvk/0003-native-gpu-transform-feedback.patch'
    old=subprocess.check_output(['git','show',reuse['sha']+':'+path],text=True)
    new=Path(path).read_text()
    # Canonical original-source positions ignore hunk merging/context, while
    # preserving every actual source edit. The pinned archive is checked again
    # when applying the patch during the native build.
    def edits(patch):
        import re
        result=[];name=None;position=0
        for line in patch.splitlines():
            if line.startswith('--- a/'):name=line[6:]
            elif line.startswith('+++ b/'):continue
            elif line.startswith('@@ '):position=int(re.match(r'@@ -(\d+)',line).group(1))
            elif name and line.startswith(('+','-')):
                result.append((name,position,line))
                if line.startswith('-'):position+=1
            elif name and line.startswith(' '):position+=1
        return result
    removed=[];keep=[]
    expected=set()
    for name,a,b in [('SPIRVToMSLConversionOptions',29,28),('SPIRVToMSLConversionConfiguration',109,108),('SPIRVToMSLConversionResultInfo',40,36)]:
        expected.add(('-',f'mvkValidateCerealArchiveSize<mvk::{name}>({a})'))
        expected.add(('+',f'mvkValidateCerealArchiveSize<mvk::{name}>({b})'))
    for edit in edits(new):
        matches=[key for key in expected if edit[2].startswith(key[0]) and key[1] in edit[2]]
        if matches:
            if edit[0]!='MoltenVK/MoltenVK/GPUObjects/MVKPipeline.mm':raise RuntimeError('Wrong cache diagnostic source')
            removed+=matches
        else:keep.append(edit)
    if set(removed)!=expected or len(removed)!=6 or keep!=edits(old):
        raise RuntimeError('Only three known Cereal padding constants may change')
    def get(path):
        request=urllib.request.Request(f'https://api.github.com/repos/{repo}/'+path,headers=headers)
        with urllib.request.urlopen(request,timeout=30) as response:return json.load(response)
    run=get(f"actions/runs/{reuse['run']}")
    if (run['head_sha']!=reuse['sha'] or run['path']!='.github/workflows/linux.yml'
            or run['head_repository']['full_name']!=repo or run['conclusion']!='success'):
        raise RuntimeError('TF reuse requires the exact successful source/workflow')
    job=next(j for j in get(f"actions/runs/{reuse['run']}/jobs")['jobs'] if j['name']=='zink-gles-apple')
    passed={step['name'] for step in job['steps'] if step['conclusion']=='success'}
    required={'Run original MoltenVK Metal resource and Mesa GLSL paths','Execute public native Vulkan TF and raster replay',
              'Execute ordinary guest Vulkan and original Mesa GLES3 TF','Compile corrected native iOS driver without packaging an app'}
    if not required<=passed:raise RuntimeError('Missing passed complete Apple TF/guest/Mesa/iOS acceptance')
    print('Only Cereal diagnostic padding constants changed. Reuse all passed GPU/guest/Mesa data; rebuild Mac/iOS libraries and check original constructor diagnostics, no repeated TF data tests.')
    return True


def compatibility_probe_outputs(plan, repo, headers):
    """Probe/documentation changes reuse an exactly unchanged passing runtime."""
    reuse = plan['runtime_reuse']
    source = reuse['sha']
    changed = subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD'], text=True).splitlines()
    allowed = {'.github/ios-validation-plan.json', '.github/workflows/ios.yml',
               '.github/workflows/linux.yml', 'scripts/ios-ci-scope.py',
               'tests/integration/firefox.py', 'tests/integration/firefox-prepare.py',
               'tests/fixtures/firefox-socket-probe.c',
               'scripts/publish-ios-build35.py', '.github/workflows/publish-ios-build35.yml'}
    if any(path not in allowed and not path.startswith('docs/') for path in changed):
        raise RuntimeError('Compatibility probe reuse requires unchanged runtime, build and existing tests')
    for platform, workflow in [('linux', 'linux.yml'), ('apple', 'ios.yml')]:
        request = urllib.request.Request(
            f"https://api.github.com/repos/{repo}/actions/runs/{reuse[platform]}", headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            run = json.load(response)
        if (run['head_sha'] != source or run['path'] != '.github/workflows/' + workflow
                or run['head_repository']['full_name'] != repo or run['conclusion'] != 'success'):
            raise RuntimeError('Compatibility baseline must be successful at the exact runtime source')
    print('Only new Firefox probes and evidence changed. Check probe syntax and host socket oracle; reuse passed unchanged Linux/Apple runtime, guest and UI results. No IPA.')
    return dict(frontend_only='false', targeted='true', native_scope='compatibility-probe',
                skip_native='true', skip_linux_native='true', skip_linux_modules='true')


def runtime_fastpaths_reuse(plan, repo, headers):
    reuse = plan.get('runtime_reuse')
    if not reuse:
        return False
    source = reuse['sha']
    changed = subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD'], text=True).splitlines()
    allowed = {'.github/ios-validation-plan.json', '.github/workflows/ios.yml',
               '.github/workflows/linux.yml', 'scripts/ios-ci-scope.py'}
    if any(p not in allowed and not p.startswith('docs/') for p in changed):
        raise RuntimeError('Runtime check reuse requires unchanged runtime, tests and build inputs')

    def get(path):
        request = urllib.request.Request(f'https://api.github.com/repos/{repo}/{path}', headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    for platform, workflow, name, required in [
        ('linux', 'linux.yml', 'runtime-fastpaths', None),
        ('apple', 'ios.yml', 'runtime-fastpaths-apple', 'Validate affected Apple VFS and scheduler modules'),
    ]:
        run_id = reuse[platform]
        run = get(f'actions/runs/{run_id}')
        if (run['head_sha'] != source or run['path'] != '.github/workflows/' + workflow
                or run['head_repository']['full_name'] != repo):
            raise RuntimeError('Runtime reuse evidence must match the exact repository/source/workflow')
        job = next(j for j in get(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == name)
        if required:
            if not any(s['name'] == required and s['conclusion'] == 'success' for s in job['steps']):
                raise RuntimeError('Missing successful Apple module checks')
        elif job['conclusion'] != 'success':
            raise RuntimeError('Missing successful Linux acceptance')
    if plan.get('epoch_services'):
        jobs = get(f"actions/runs/{reuse['apple']}/jobs")['jobs']
        if not any(j['name'] == 'epoch-service-fixture' and j['conclusion'] == 'success' for j in jobs):
            raise RuntimeError('Missing successful unchanged musl fixture build')
        artifacts = get(f"actions/runs/{reuse['apple']}/artifacts?per_page=100")['artifacts']
        if not any(a['name'] == 'epoch-service-fixture' and not a['expired'] for a in artifacts):
            raise RuntimeError('Validated epoch fixture artifact missing or expired')
        with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
            output.write(f"epoch_fixture_run={reuse['apple']}\n")
    print('Reuse exact passed Linux and Apple modules; complete only the unfinished Apple terminal check.')
    return True


def container_isolation_path(path):
    return path.startswith(('runtime/crates/trames-net/', 'runtime/crates/trames-vfs/',
                            'runtime/crates/trames-linux/', 'crates/trames-core/', 'docs/')) or path in {
        'crates/trames-cpu/src/extended.rs', 'crates/trames-cpu/src/compatibility.rs',
        'tests/fixtures/seccomp.c', 'tests/fixtures/network-namespaces.c',
        'scripts/build-container-isolation-fixtures.sh',
        'tests/integration/container-isolation.py', 'tests/integration/docker.py',
        '.github/workflows/ios.yml', '.github/workflows/linux.yml',
        'scripts/ios-ci-scope.py',
    }


def container_storage_path(path):
    return path.startswith(('runtime/crates/trames-vfs/', 'docs/')) or path in {
        '.github/workflows/ios.yml', '.github/workflows/linux.yml',
        'scripts/ios-ci-scope.py', 'examples/docker/daemon.json', 'README.md',
        'tests/integration/docker-overlay.py', 'tests/integration/docker-overlay.sh',
    }


def container_network_path(path):
    return path.startswith(('runtime/crates/trames-net/','runtime/crates/trames-vfs/','runtime/crates/trames-linux/','docs/')) or path in {
        '.github/workflows/ios.yml','.github/workflows/linux.yml','scripts/ios-ci-scope.py',
        'scripts/build-container-network-fixture.sh','tests/fixtures/container-network.c','tests/fixtures/container-forwarding.c',
        'tests/integration/container-network.py','tests/integration/docker.py',
        'scripts/build-docker-network-fixture.sh','tests/fixtures/docker-network-control.c',
        'scripts/build-nftables-fixture.sh','tests/fixtures/nftables.c','tests/integration/nftables.py',
        '.cargo/config.toml','crates/trames-core/src/process.rs',
        'crates/trames-cli/src/main.rs','crates/trames-ffi/src/session.rs',
        'examples/docker/daemon.json','scripts/build-netfilter-nat-fixture.sh',
        'tests/fixtures/netfilter-nat.c','tests/fixtures/docker-network-probe.c',
        'tests/integration/netfilter-nat.py','tests/integration/docker-network.py',
        'tests/integration/docker-network.sh',
    }


def network_test_retry_source_unchanged(sha):
    delta=subprocess.check_output(['git','diff','--name-only',sha,'HEAD'],text=True).splitlines()
    allowed={'.github/ios-validation-plan.json','.github/workflows/ios.yml',
             '.github/workflows/linux.yml','scripts/ios-ci-scope.py',
             'runtime/crates/trames-net/src/nftables_tests.rs','runtime/crates/trames-linux/src/network.rs',
             'runtime/crates/trames-net/tests/virtual.rs'}
    if any(not p.startswith('docs/') and p not in allowed for p in delta):return False
    # nftables_tests is included only under cfg(test). Socket production code,
    # including ancillary parsing, must be byte-identical to the verified run.
    path='runtime/crates/trames-linux/src/network.rs'
    previous=subprocess.check_output(['git','show',sha+':'+path],text=True).split('#[cfg(test)]')[0]
    return previous==Path(path).read_text().split('#[cfg(test)]')[0]


def network_lifetime_source_unchanged(sha):
    delta=subprocess.check_output(['git','diff','--name-only',sha,'HEAD'],text=True).splitlines()
    allowed={'.github/ios-validation-plan.json','.github/workflows/ios.yml',
             '.github/workflows/linux.yml','scripts/ios-ci-scope.py',
             'runtime/crates/trames-net/src/lib.rs','runtime/crates/trames-net/src/netlink.rs',
             'runtime/crates/trames-net/src/routing_tests.rs'}
    if any(not p.startswith('docs/') and p not in allowed for p in delta):return False
    # Exact OnceCell ownership pin + teardown guard, without protocol changes.
    # Any further production change falls back to all affected network checks.
    patch=subprocess.check_output(['git','diff','--binary',sha,'HEAD','--',
        'runtime/crates/trames-net/src/lib.rs','runtime/crates/trames-net/src/netlink.rs'])
    return hashlib.sha256(patch).hexdigest()=='b72cac90931a82d6d21a4432b241ba8fe265206bc035b378f20e6a625f2cbc2c'


def isolation_retry_source_unchanged(sha):
    delta=subprocess.check_output(['git','diff','--name-only',sha,'HEAD'],text=True).splitlines()
    allowed={'.github/ios-validation-plan.json','.github/workflows/ios.yml',
             '.github/workflows/linux.yml','scripts/ios-ci-scope.py',
             'crates/trames-core/src/process_seccomp.rs','runtime/crates/trames-linux/src/network.rs',
             'tests/fixtures/seccomp.c','tests/integration/container-isolation.py'}
    if any(not p.startswith('docs/') and p not in allowed for p in delta):return False
    old=lambda p:subprocess.check_output(['git','show',sha+':'+p],text=True).split('#[cfg(test)]')[0]
    current=lambda p:Path(p).read_text().split('#[cfg(test)]')[0]
    if old('runtime/crates/trames-linux/src/network.rs') != current('runtime/crates/trames-linux/src/network.rs'):return False
    # Reuse Docker/ordinary syscall results only for this exact TRAP-only fix.
    # Any broader production change falls back to the complete affected checks.
    guard='''                // force_sig_seccomp cannot be suppressed by ignoring or
                // blocking SIGSYS; Linux forces its fatal default action.
                if p.mask & (1u64 << 30) != 0 || p.actions[31].handler == 1 {
                    self.exit(pid, p, 31);
                    return true;
                }
'''
    core=current('crates/trames-core/src/process_seccomp.rs')
    return core.count(guard)==1 and core.replace(guard,'')==old('crates/trames-core/src/process_seccomp.rs')




def docker_default_lifecycle_outputs(plan, repo, headers):
    reuse = plan['docker_default_lifecycle']
    source = reuse['sha']
    paths = ['crates', 'Cargo.toml', 'Cargo.lock', 'vendor', '.cargo',
             'rust-toolchain.toml', 'guest', 'examples/docker',
             'tests/fixtures/socket-timeouts.c', 'scripts/build-socket-timeouts-fixture.sh']
    if subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD', '--', *paths]).strip():
        raise RuntimeError('Lifecycle-only acceptance requires unchanged validated runtime/config/C ABI')

    def get(path):
        request = urllib.request.Request(f'https://api.github.com/repos/{repo}/{path}', headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    for key, workflow, name in [('linux', 'linux.yml', 'targeted-runtime'),
                                ('apple', 'ios.yml', 'docker-default-apple')]:
        run_id = reuse[key]
        run = get(f'actions/runs/{run_id}')
        if (run['head_sha'] != source or run['path'] != '.github/workflows/' + workflow
                or run['head_repository']['full_name'] != repo):
            raise RuntimeError('Lifecycle evidence must match the unchanged source/workflow')
        job = next(j for j in get(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == name)
        if key == 'apple' and job['conclusion'] != 'success':
            raise RuntimeError('Missing successful Apple/iOS runtime acceptance')
        if key == 'linux' and not any(s['name'] == 'Check affected modules' and s['conclusion'] == 'success'
                                     for s in job['steps']):
            raise RuntimeError('Missing successful Linux module acceptance')
    print('Runtime unchanged: reuse passed module/C ABI/Apple checks; execute independent default Docker exec and storage lifecycle guests. Combined bind/device/DNS/egress coverage retained with its timeout boundary.')
    return {'frontend_only': 'false', 'targeted': 'true', 'native_scope': 'docker-default-lifecycle',
            'skip_native': 'true', 'skip_linux_native': 'true', 'skip_linux_modules': 'true'}


def docker_defaults_remount_retry(plan, repo, headers):
    """Reuse passed modules only for the exact bind-remount compatibility fix."""
    reuse = plan.get('docker_default_reuse')
    if not reuse:
        return False
    source = reuse['sha']
    patch = subprocess.check_output(['git', 'diff', '--binary', source, 'HEAD', '--', 'crates'])
    if hashlib.sha256(patch).hexdigest() != '6b6511c47bb4b2657fe8ab9b54ce366e8379048280a843277f976db7c6869016':
        raise RuntimeError('Docker retry requires the exact validated bind-remount patch')
    if subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD', '--',
                                'tests/fixtures/socket-timeouts.c',
                                'scripts/build-socket-timeouts-fixture.sh']).strip():
        raise RuntimeError('Cannot reuse changed socket/seccomp C ABI fixtures')

    def get(path):
        request = urllib.request.Request(f'https://api.github.com/repos/{repo}/{path}', headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    for key, workflow, job_name in [('linux', 'linux.yml', 'targeted-runtime'),
                                     ('apple', 'ios.yml', 'docker-default-apple')]:
        run_id = reuse[key]
        run = get(f'actions/runs/{run_id}')
        if (run['head_sha'] != source or run['path'] != '.github/workflows/' + workflow
                or run['head_repository']['full_name'] != repo):
            raise RuntimeError('Docker retry evidence does not match its source/workflow')
        job = next(j for j in get(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == job_name)
        if key == 'apple':
            if job['conclusion'] != 'success':
                raise RuntimeError('Missing successful Apple defaults checks')
        elif not any(s['name'] == 'Check affected modules' and s['conclusion'] == 'success'
                     for s in job['steps']):
            raise RuntimeError('Missing successful Linux defaults module checks')
    print('Reuse passed unchanged net/Linux/core/socket ABI and Apple results; rerun mount regressions, iOS FFI and the unfinished default Docker workflow.')
    return True


def device_build_outputs(plan, repo, headers):
    """Rebuild the device runtime, reusing only source-identical acceptance."""
    reuse = plan['device_build']

    def get(path):
        request = urllib.request.Request(f'https://api.github.com/repos/{repo}/{path}', headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    def verified_run(record, workflow, job_name):
        run_id = record['run']
        run = get(f'actions/runs/{run_id}')
        if (run['conclusion'] != 'success' or run['head_sha'] != record['sha']
                or run['path'] != '.github/workflows/' + workflow
                or run['head_repository']['full_name'] != repo):
            raise RuntimeError(f'Device build requires successful same-repository evidence: {run_id}')
        jobs = get(f'actions/runs/{run_id}/jobs')['jobs']
        job = next((j for j in jobs if j['name'] == job_name and j['conclusion'] == 'success'), None)
        if job is None:
            raise RuntimeError(f'Missing successful {job_name} in {run_id}')
        return {step['name'] for step in job.get('steps', []) if step['conclusion'] == 'success'}

    def unchanged(sha, paths):
        return not subprocess.check_output(['git', 'diff', '--name-only', sha, 'HEAD', '--', *paths], text=True).strip()

    zink = reuse.get('zink')
    if zink:
        import tomllib
        paths = ['crates', 'vendor', '.cargo', 'rust-toolchain.toml', 'accelerator', 'include', 'patches/mesa',
                 'scripts/build-zink-egl.py', 'scripts/zink-egl-requirements.txt']
        layout = reuse.get('layout_runtime')
        runtime = layout or reuse.get('clock_runtime')
        if runtime:
            runtime_paths = ['crates', 'vendor', '.cargo', 'rust-toolchain.toml']
            # Workspace version-only metadata is checked against the graphics
            # checkpoint below; dependency or build-input changes still fail.
            if not layout:
                runtime_paths += ['Cargo.toml', 'Cargo.lock']
            if not unchanged(runtime['sha'], runtime_paths):
                raise RuntimeError('Device build runtime differs from the passed clock/runtime checkpoint')
            names = ['firefox-layout', 'firefox-layout-apple'] if layout else ['firefox-clock', 'firefox-clock-apple']
            for name in names:
                verified_run(runtime, 'linux.yml', name)
            # Recompile the validated current runtime; the older Metal bundle
            # only supplies source-identical guest graphics and their sources.
            paths = ['crates/trames-vulkan', 'runtime/crates/trames-wayland', 'crates/trames-compute',
                     'runtime/crates/trames-shared', 'vendor', '.cargo', 'rust-toolchain.toml',
                     'accelerator', 'include', 'patches/mesa',
                     'patches/moltenvk', 'scripts/prepare-vulkan.py', 'scripts/build-moltenvk.py',
                     'scripts/build-vulkan-native-tf.py',
                     'scripts/build-zink-egl.py', 'scripts/zink-egl-requirements.txt']
        if not unchanged(zink['sha'], paths):
            raise RuntimeError('Zink IPA requires unchanged Metal-validated runtime and guest sources')
        old = tomllib.loads(subprocess.check_output(['git', 'show', zink['sha'] + ':Cargo.toml'], text=True))
        new = tomllib.loads(Path('Cargo.toml').read_text())
        old['workspace']['package']['version'] = new['workspace']['package']['version']
        if old != new:
            raise RuntimeError('Only workspace version metadata may change')
        old = tomllib.loads(subprocess.check_output(['git', 'show', zink['sha'] + ':Cargo.lock'], text=True))
        new = tomllib.loads(Path('Cargo.lock').read_text())
        for package in old['package']:
            if package['name'].startswith('trames-'):
                package['version'] = tomllib.loads(Path('Cargo.toml').read_text())['workspace']['package']['version']
        if old != new:
            raise RuntimeError('Dependency versions or lockfile inputs changed')
        for name in ['zink-wayland-linux', 'zink-wayland-apple']:
            verified_run(zink, 'linux.yml', name)
        frontend = reuse['frontend']
        frontend_paths = ['apps/ios', 'include', 'scripts/package-ios.py', 'scripts/finalize-ios-app.py', ':(exclude)apps/ios/project.yml']
        input_followup = reuse.get('frontend_xim_followup', False)
        if not unchanged(frontend['sha'], frontend_paths):
            delta = subprocess.check_output(['git', 'diff', '--name-only', frontend['sha'], 'HEAD', '--', *frontend_paths], text=True).splitlines()
            affected = {'apps/ios/Trames/TerminalSession.swift', 'apps/ios/Trames/TramesApp.swift',
                        'apps/ios/Trames/WaylandInputView.swift', 'apps/ios/TramesTests/WaylandInputTests.swift',
                        'apps/ios/TramesKit/Sources/TramesKit/LocalSession.swift',
                        'apps/ios/Trames/Notices/Wayland-native-dependencies-NOTICES.txt'}
            if not input_followup or not set(delta) <= affected:
                raise RuntimeError('Frontend acceptance source changed outside the XIM/input follow-up')
        strip = lambda text: '\n'.join(l for l in text.splitlines() if not l.strip().startswith(('MARKETING_VERSION:', 'CURRENT_PROJECT_VERSION:')))
        previous = subprocess.check_output(['git', 'show', frontend['sha'] + ':apps/ios/project.yml'], text=True)
        if strip(previous) != strip(Path('apps/ios/project.yml').read_text()):
            raise RuntimeError('Only app version/build metadata may change')
        completed = verified_run(frontend, 'ios.yml', 'apple-build')
        if not {'Link unsigned iPad simulator app', 'Exercise peer tabs and floating window gestures first', 'Export simulator test summary', 'Link unsigned iPad device app'} <= completed:
            raise RuntimeError('Missing passed frontend interactions')
        guest = reuse['guest']
        guest_paths = ['accelerator/musl', 'accelerator/guest', 'runtime/crates/trames-native/upstream',
                       'scripts/build-musl-hybrid.sh', 'scripts/prepare-musl-hybrid.py',
                       'scripts/vulkan-upstream.json', 'LICENSE']
        if not layout:
            guest_paths.append('scripts/prepare-vulkan.py')
        if not unchanged(guest['sha'], guest_paths):
            raise RuntimeError('Original musl/MoltenVK build inputs changed')
        verified_run(guest, 'ios.yml', 'guest-musl')
        names = {a['name'] for a in get(f"actions/runs/{guest['run']}/artifacts")['artifacts'] if not a['expired']}
        if not {'full-musl-library-and-source', 'vulkan-compute-driver-and-native-source'} <= names:
            raise RuntimeError('Paired original source artifacts missing')
        print('Compile current iPad Release app; reuse exact validated graphics bundle, paired musl/native sources and unchanged frontend acceptance. No repeated passed suites.')
        result = dict(frontend_only='false', targeted='true', native_scope='device-build', skip_native='true',
                      skip_linux_native='true', skip_linux_modules='true', frontend_reused=str(not input_followup).lower(),
                      ui_targets='["TramesTests/WaylandInputTests"]' if input_followup else '[]',
                      baseline_run=str(guest['run']), zink_graphics_run=str(zink['run']))
        if input_followup:
            result['critical_test'] = 'TramesTests/WaylandInputTests/testDeleteWithoutSurroundingTextUsesGuestCaret'
        if reuse.get('xwayland_device_build'):
            result['native_scope'] = 'device-xwayland'
        return result

    metadata = reuse.get('metadata_repackage')
    if metadata:
        delta = subprocess.check_output(['git', 'diff', '--name-only', metadata['sha'], 'HEAD'], text=True).splitlines()
        allowed = {'.github/ios-validation-plan.json', '.github/workflows/ios.yml',
                   'scripts/ios-ci-scope.py', 'apps/ios/project.yml', '.github/workflows/linux.yml',
                   'scripts/publish-ios-release-1.1.py', 'README.md'}
        if any(p not in allowed and not p.startswith('docs/') for p in delta):
            raise RuntimeError('Metadata repackaging must preserve all compiled sources')
        project = 'apps/ios/project.yml'
        previous = subprocess.check_output(['git', 'show', metadata['sha'] + ':' + project], text=True)
        strip_build = lambda text: '\n'.join(line for line in text.splitlines() if not line.strip().startswith('CURRENT_PROJECT_VERSION:'))
        if strip_build(previous) != strip_build(Path(project).read_text()):
            raise RuntimeError('Metadata repackaging may only change the build number')
        completed = verified_run(metadata, 'ios.yml', 'apple-build')
        if not {'Validate affected native modules or the complete Apple core',
                'Link unsigned iPad device app', 'Package unsigned device IPA for sideload signing'} <= completed:
            raise RuntimeError('Missing passed current runtime checks and device compilation')
        print('Repackage successful identical device executables; correct only bundle build numbers, no repeated checks.')
        return {'frontend_only': 'false', 'targeted': 'true', 'native_scope': 'release-cross' if metadata.get('cross_only') else 'device-metadata',
                'repackage_run': str(metadata['run']), 'skip_native': 'true',
                'skip_linux_native': 'true', 'skip_linux_modules': 'true', 'frontend_reused': 'true',
                'ui_targets': '[]', 'baseline_run': str(reuse['guest']['run'])}

    runtime_paths = ['crates', 'Cargo.toml', 'Cargo.lock', 'vendor', '.cargo', 'rust-toolchain.toml']
    memory_va = reuse.get('memory_va')
    startup_mounts = reuse.get('startup_mounts')
    devpts = reuse.get('devpts')
    release_core = reuse.get('release_core')
    costs = reuse.get('runtime_costs') or memory_va or startup_mounts or devpts or release_core
    if costs:
        # These exact MMU/proc changes already passed the recorded Linux tests.
        # Run their Apple counterparts below; reuse only the unchanged modules.
        changed_runtime = set(subprocess.check_output(
            ['git', 'diff', '--name-only', costs['sha'], 'HEAD', '--', *runtime_paths], text=True).splitlines())
        expected = {'runtime/crates/trames-memory/src/mmu/mod.rs',
                    'runtime/crates/trames-memory/src/mmu/tests.rs', 'crates/trames-core/src/process.rs'}
        if memory_va:
            expected = {'runtime/crates/trames-memory/src/mmu/host.rs', 'runtime/crates/trames-memory/src/mmu/mod.rs',
                        'runtime/crates/trames-memory/src/mmu/tests.rs', 'runtime/crates/trames-memory/src/mmu/worker.rs',
                        'runtime/crates/trames-vfs/src/pseudo.rs'}
        if startup_mounts:
            expected = {'crates/trames-ffi/src/session.rs'}
        if devpts:
            expected = {'runtime/crates/trames-vfs/src/pty.rs', 'runtime/crates/trames-vfs/src/pseudo.rs'}
        if release_core:
            expected = {'Cargo.toml', 'Cargo.lock'}
        patch = subprocess.check_output(['git', 'diff', '--binary', costs['sha'], 'HEAD', '--', *runtime_paths])
        if changed_runtime != expected or hashlib.sha256(patch).hexdigest() != costs['patch_sha256']:
            raise RuntimeError('Device IPA requires the exact Linux-validated runtime patch')
        # The prior combined Linux workflow timed out after its module step;
        # require that step, without claiming the unfinished workflow passed.
        if memory_va or startup_mounts or devpts or release_core:
            completed = verified_run({'sha': costs['sha'], 'run': costs['apple']}, 'ios.yml', 'apple-build')
            if not {'Validate affected native modules or the complete Apple core',
                    'Link unsigned iPad device app', 'Package unsigned device IPA for sideload signing'} <= completed:
                raise RuntimeError('Missing successful baseline memory/proc/device verification')
        for key, workflow, name in ([] if memory_va or startup_mounts or devpts or release_core else [('linux', 'linux.yml', 'targeted-runtime'),
                                    ('apple', 'ios.yml', 'docker-default-apple')]):
            run_id = costs[key]
            run = get(f'actions/runs/{run_id}')
            if (run['head_sha'] != costs['sha'] or run['path'] != '.github/workflows/' + workflow
                    or run['head_repository']['full_name'] != repo):
                raise RuntimeError('Performance baseline evidence/source mismatch')
            job = next(j for j in get(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == name)
            if key == 'apple' and job['conclusion'] != 'success':
                raise RuntimeError('Missing successful Apple baseline checks')
            if key == 'linux' and not any(s['name'] == 'Check affected modules' and s['conclusion'] == 'success'
                                         for s in job['steps']):
                raise RuntimeError('Missing successful Linux baseline module checks')
    else:
        for key, workflow, job in [('linux', 'linux.yml', 'targeted-runtime'),
                                   ('apple', 'ios.yml', 'container-storage-apple')]:
            record = reuse[key]
            if not unchanged(record['sha'], runtime_paths):
                raise RuntimeError('Device-only build requires unchanged validated runtime sources')
            verified_run(record, workflow, job)

    frontend = reuse['frontend']
    frontend_paths = ['apps/ios', 'include', 'scripts/build-ios.sh', 'scripts/package-ios.py',
                      'scripts/finalize-ios-app.py', 'scripts/prepare-vulkan.py',
                      ':(exclude)apps/ios/project.yml']
    if not unchanged(frontend['sha'], frontend_paths):
        raise RuntimeError('Device-only build requires unchanged frontend sources')
    project = 'apps/ios/project.yml'
    previous = subprocess.check_output(['git', 'show', frontend['sha'] + ':' + project], text=True)
    # A build-number bump does not invalidate simulator interaction acceptance.
    strip_build = lambda text: '\n'.join(line for line in text.splitlines() if not line.strip().startswith(('CURRENT_PROJECT_VERSION:', 'MARKETING_VERSION:') if release_core else ('CURRENT_PROJECT_VERSION:',)))
    if strip_build(previous) != strip_build(Path(project).read_text()):
        raise RuntimeError('Only the build number may change in the validated Xcode project')
    completed = verified_run(frontend, 'ios.yml', 'apple-build')
    if not {'Link unsigned iPad simulator app', 'Exercise peer tabs and floating window gestures first',
            'Export simulator test summary', 'Link unsigned iPad device app'} <= completed:
        raise RuntimeError('Missing previous simulator/frontend acceptance')

    guest = reuse['guest']
    guest_paths = ['guest', 'include', 'accelerator', 'LICENSE', 'runtime/crates/trames-native/upstream',
                   'scripts/build-musl-hybrid.sh', 'scripts/prepare-musl-hybrid.py',
                   'scripts/build-vulkan-guest.sh', 'scripts/build-vulkan-fixtures.sh',
                   'scripts/prepare-vulkan.py', 'scripts/vulkan-upstream.json',
                   'scripts/prepare-wayland-guest.py', 'scripts/build-xwayland-launcher.sh', 'scripts/build-busybox.sh',
                   'scripts/prepare-alpine.sh', 'tests/integration/m6.py',
                   ':(glob)tests/fixtures/vulkan-*', ':(glob)tests/fixtures/wayland-*']
    if not unchanged(guest['sha'], guest_paths):
        raise RuntimeError('Guest artifact build inputs changed')
    verified_run(guest, 'ios.yml', 'guest-musl')
    artifacts = get(f"actions/runs/{guest['run']}/artifacts?per_page=100")['artifacts']
    names = {artifact['name'] for artifact in artifacts if not artifact['expired']}
    if not {'full-musl-library-and-source', 'vulkan-compute-driver-and-native-source'} <= names:
        raise RuntimeError('Validated guest artifacts expired or missing')
    print('Device IPA: rebuild current ARM64 release runtime/app; reuse source-identical frontend and guest acceptance.')
    if costs:
        print('Formal release: run consolidated Linux/Apple Rust workspace checks and current ARM64 Release build; reuse unchanged UI/GPU/guest acceptance.' if release_core else
              'Exact devpts inode delta: run Apple PTY identity/isolation checks; reuse unchanged runtime/CPU/UI/GPU/guest acceptance.' if devpts else
              'Exact FFI startup delta: run runtime tmpfs/Wayland ordering and cold-reopen checks; reuse unchanged Docker/MMU/CPU/UI/GPU acceptance.' if startup_mounts else
              'Exact Linux-validated delta: run Apple memory, proc-route and Docker startup/VA checks.' if memory_va else
              'Exact Linux-validated MMU/proc delta: run Apple memory + proc-map regressions; unchanged native modules reuse baseline results. No unrelated CPU/UI/GPU/Docker reruns.')
    print(json.dumps(reuse, sort_keys=True))
    return {'frontend_only': 'false', 'targeted': 'true', 'native_scope': 'release-core' if release_core else 'devpts' if devpts else 'startup-mounts' if startup_mounts else 'memory-va' if memory_va else 'runtime-costs' if costs else 'device-build',
            'skip_native': 'false' if costs else 'true', 'skip_linux_native': 'false' if release_core else 'true', 'skip_linux_modules': 'true',
            'frontend_reused': 'true', 'ui_targets': '[]', 'baseline_run': str(guest['run'])}


def firefox_memory_readonly_followup(plan, repo, headers):
    """Reuse unchanged byte-domain checks; validate changed VM preparation and fixture."""
    reuse = plan.get('memory_readonly_followup')
    if not reuse:
        return False
    source, run = reuse['sha'], reuse['run']
    runtime = set(subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD', '--',
        'crates', 'Cargo.toml', 'Cargo.lock', 'vendor', '.cargo', 'rust-toolchain.toml'], text=True).splitlines())
    fixture = 'runtime/crates/trames-memory/src/parallel/readonly.rs'
    if runtime != {'crates/trames-core/src/process.rs', fixture}:
        raise RuntimeError('Readonly follow-up must preserve the memory byte-domain and benchmark sources')
    def outside_helper(text):
        begin = text.index('fn views(')
        end = text.index('\nfn admit(', begin)
        return text[:begin] + text[end:]
    previous = subprocess.check_output(['git', 'show', source + ':' + fixture], text=True)
    if outside_helper(previous) != outside_helper(Path(fixture).read_text()):
        raise RuntimeError('Readonly fixture follow-up changed its cases or assertions')
    def get(endpoint):
        with urllib.request.urlopen(urllib.request.Request(
            f'https://api.github.com/repos/{repo}/actions/{endpoint}', headers=headers), timeout=30) as response:
            return json.load(response)
    validated = get(f'runs/{run}')
    if (validated['head_sha'] != source or validated['path'] != '.github/workflows/linux.yml'
            or validated['head_repository']['full_name'] != repo):
        raise RuntimeError('Readonly reuse evidence must match the repository/source/workflow')
    job = next(j for j in get(f'runs/{run}/jobs')['jobs'] if j['name'] == 'firefox-memory')
    if not any(s['name'] == 'Check affected memory paths and original ARM64 ABI'
               and s['conclusion'] == 'success' for s in job['steps']):
        raise RuntimeError('Readonly reuse requires passed affected modules, Clippy, ABI and Linux benchmark')
    log = subprocess.check_output(['gh', 'run', 'view', str(run), '--repo', repo,
                                   '--job', str(job['id']), '--log'], text=True)
    import re
    log = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', log)
    if ('test result: ok. 6 passed; 0 failed' not in log
            or log.count('test result: ok. 5 passed; 0 failed') < 2
            or 'readonly_guard_keeps_region_ownership_and_the_compact_layout ... ok' not in log):
        raise RuntimeError('Readonly reuse requires passed full-std sharing6/page5/exclusive5 TSan cases')
    print(f'Reuse unchanged Linux memory/Linux/VFS/benchmark and 16 TSan cases from {run}; validate Core, fixed readonly4, remaining VFS TSan and full Apple checks. No IPA.')
    return True


def firefox_memory_apple_retry(plan, repo, headers):
    """Reuse passed Linux/Apple Core; finish the failed socket case and remaining checks."""
    reuse = plan.get('memory_apple_retry')
    if not reuse:
        return False
    source, run = reuse['sha'], reuse['run']
    runtime = subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD', '--',
        'crates', 'Cargo.toml', 'Cargo.lock', 'vendor', '.cargo', 'rust-toolchain.toml'], text=True).splitlines()
    path = 'runtime/crates/trames-linux/src/network.rs'
    if runtime != [path]:
        raise RuntimeError('Apple memory retry requires unchanged runtime except the failed socket test')
    def outside_case(text):
        begin = text.index('    #[test]\n    fn invalid_accept_destination_does_not_drop_queued_connection()')
        end = text.index('    #[test]\n    fn socket_output_preflight_and_descriptor_type_errors()', begin)
        return text[:begin] + text[end:]
    previous = subprocess.check_output(['git', 'show', source + ':' + path], text=True)
    if outside_case(previous) != outside_case(Path(path).read_text()):
        raise RuntimeError('Socket retry changed production code or another passed test')
    def get(endpoint):
        with urllib.request.urlopen(urllib.request.Request(
            f'https://api.github.com/repos/{repo}/actions/{endpoint}', headers=headers), timeout=30) as response:
            return json.load(response)
    validated = get(f'runs/{run}')
    if (validated['head_sha'] != source or validated['path'] != '.github/workflows/linux.yml'
            or validated['head_repository']['full_name'] != repo):
        raise RuntimeError('Memory retry evidence does not match the repository/source/workflow')
    jobs = get(f'runs/{run}/jobs')['jobs']
    if not any(j['name'] == 'firefox-memory' and j['conclusion'] == 'success' for j in jobs):
        raise RuntimeError('Memory retry requires passed Linux modules, ABI and full-std TSan')
    apple = next(j for j in jobs if j['name'] == 'firefox-memory-apple')
    log = subprocess.check_output(['gh', 'run', 'view', str(run), '--repo', repo,
                                   '--job', str(apple['id']), '--log'], text=True)
    import re
    log = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', log)
    # Cargo/gh versions decorate the binary path differently. Bind this result
    # to unique Core cases and the complete summary, rather than its path banner.
    core_cases = ('private_epoch_admission_counts_real_vm_roots_and_external_aliases',
                  'independent_fork_workers_use_private_memory_without_merging_writes')
    core_passed = all(f'test process::tests::{case} ... ok' in log for case in core_cases)
    if not core_passed or 'test result: ok. 83 passed; 0 failed' not in log:
        raise RuntimeError('Memory retry requires the passed exact-source Apple Core suite')
    print(f'Reuse Linux modules/ABI/TSan and Apple Core83 from {run}; run failed socket test, remaining memory/VFS/ABI and ARM64 benchmark. No IPA.')
    return True


def main():
    if os.environ.get("TRAMES_FULL_CHECK") == "true":
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write("frontend_only=false\n")
        return
    if Path('.github/runtime-ci-reuse.json').exists():
        import importlib.util
        spec = importlib.util.spec_from_file_location('runtime_ci_reuse', 'scripts/runtime-ci-reuse.py')
        reuse = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reuse)
        outputs = reuse.apple_outputs()
        if outputs is not None:
            with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                for key, value in outputs.items():
                    output.write(f'{key}={value}\n')
            return
    repo = os.environ["GITHUB_REPOSITORY"]
    workflow = os.environ.get("TRAMES_VALIDATION_WORKFLOW", "ios.yml")
    request = urllib.request.Request(
        f"https://api.github.com/repos/{repo}/actions/workflows/{workflow}/runs?status=success&per_page=30",
        headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                 "Accept": "application/vnd.github+json"},
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        runs = json.load(response)["workflow_runs"]
    plan_path=Path('.github/ios-validation-plan.json')
    if plan_path.exists() and paired_runtime_plan(plan_path):
        plan=json.loads(plan_path.read_text())
        base=plan['base_sha']
        if subprocess.run(['git','merge-base','--is-ancestor',base,'HEAD'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:
            changed=set(subprocess.check_output(['git','diff','--name-only',base,'HEAD'],text=True).splitlines())- {str(plan_path)}
            hashes=plan['changed_file_sha256']
            if changed==set(hashes) and all(Path(path).exists() and hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest for path,digest in hashes.items()):
                if plan.get('native_scope') == 'worker-handoff':
                    subprocess.run(['python3', 'scripts/prepare-worker-ipa.py'], check=True)
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=worker-handoff\n')
                        output.write('skip_linux_native=true\nskip_linux_modules=true\n')
                        output.write('frontend_reused=true\nui_targets=[]\n')
                        output.write('baseline_run=' + str(plan['worker_handoff']['guest']['run']) + '\n')
                    return
                if plan.get('native_scope') == 'vulkan-tf-engine':
                    # This internal engine is deliberately not linked into the
                    # existing Rust/MoltenVK adapter yet. Prove that every passed
                    # guest/runtime/UI input is unchanged before reusing it.
                    reuse = plan['runtime_reuse']
                    allowed = {'.github/ios-validation-plan.json', '.github/workflows/linux.yml',
                               '.github/workflows/ios.yml', 'scripts/ios-ci-scope.py', 'scripts/prepare-vulkan.py',
                               'scripts/build-vulkan-native-tf.py', 'tests/fixtures/vulkan-native-tf-compiler.cpp',
                               'tests/fixtures/vulkan-native-tf-metal.mm', 'tests/fixtures/vulkan-native-tf.vert',
                               'tests/integration/vulkan-native-tf-reference.py', 'tests/integration/vulkan-native-tf-runtime.py'}
                    delta = subprocess.check_output(['git', 'diff', '--name-only', reuse['sha'], 'HEAD'], text=True).splitlines()
                    if any(p not in allowed and not p.startswith(('docs/', 'crates/trames-vulkan/native/transform-feedback/')) for p in delta):
                        raise RuntimeError('Native TF engine reuse changed linked runtime/guest inputs')
                    def tf_evidence(path):
                        req = urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/' + path, headers=dict(request.headers))
                        with urllib.request.urlopen(req, timeout=30) as response:
                            return json.load(response)
                    prior = tf_evidence(f"runs/{reuse['run']}")
                    jobs = tf_evidence(f"runs/{reuse['run']}/jobs")['jobs']
                    if (prior['head_sha'] != reuse['sha'] or prior['head_repository']['full_name'] != repo
                            or prior['path'] != '.github/workflows/linux.yml' or any(not any(
                                j['name'] == name and j['conclusion'] == 'success' for j in jobs)
                                for name in ('zink-foundation', 'zink-gles-apple'))):
                        raise RuntimeError('Native TF engine requires passed exact-source Linux/Metal baseline')
                    compiler_reuse = plan.get('tf_compiler_reuse')
                    if compiler_reuse:
                        compiler_allowed = {'.github/ios-validation-plan.json', '.github/workflows/linux.yml',
                            '.github/workflows/ios.yml', 'scripts/ios-ci-scope.py',
                            'tests/integration/vulkan-native-tf-reference.py', 'tests/integration/vulkan-native-tf-runtime.py', 'tests/fixtures/vulkan-native-tf-metal.mm',
                            'crates/trames-vulkan/native/transform-feedback/assemble.metal',
                            'crates/trames-vulkan/native/transform-feedback/metal.hpp',
                            'crates/trames-vulkan/native/transform-feedback/metal.mm'}
                        delta = subprocess.check_output(['git', 'diff', '--name-only', compiler_reuse['sha'], 'HEAD'], text=True).splitlines()
                        if any(p not in compiler_allowed and not p.startswith('docs/') for p in delta):
                            raise RuntimeError('TF shader reuse changed compiler/shader/build inputs')
                        prior = tf_evidence(f"runs/{compiler_reuse['run']}")
                        jobs = tf_evidence(f"runs/{compiler_reuse['run']}/jobs")['jobs']
                        artifacts = tf_evidence(f"runs/{compiler_reuse['run']}/artifacts")['artifacts']
                        if (prior['head_sha'] != compiler_reuse['sha'] or prior['head_repository']['full_name'] != repo
                                or prior['path'] != '.github/workflows/linux.yml' or not any(
                                    j['name'] == 'vulkan-tf-engine-linux' and j['conclusion'] == 'success' for j in jobs)
                                or not any(a['name'] == 'vulkan-native-tf-shaders' and not a['expired'] for a in artifacts)):
                            raise RuntimeError('Missing passed exact-source TF shader bundle')
                    print('Run only new TF compiler/native Metal engine and iOS native compile; reuse unchanged passed runtime, guest, Vulkan/Mesa and UI. No IPA; no public TF capability claim.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=vulkan-tf-engine\n')
                        output.write('skip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                        if compiler_reuse:
                            output.write(f"tf_shader_bundle_run={compiler_reuse['run']}\ntf_shader_bundle_sha={compiler_reuse['sha']}\n")
                    return
                if plan.get('native_scope') == 'firefox-memory':
                    apple_retry = firefox_memory_apple_retry(plan, repo, dict(request.headers))
                    readonly_followup = firefox_memory_readonly_followup(plan, repo, dict(request.headers))
                    allowed = {'AGENTS.md', 'crates/trames-core/src/lib.rs', 'crates/trames-core/src/process.rs',
                               'crates/trames-core/src/process_service.rs', 'runtime/crates/trames-linux/src/state.rs',
                               'runtime/crates/trames-vfs/src/mapped.rs', 'tests/fixtures/firefox-memory-probe.c',
                               'tests/integration/firefox.py', 'scripts/build-firefox-fixture.sh',
                               'scripts/ios-ci-scope.py', '.github/workflows/linux.yml', '.github/workflows/ios.yml'}
                    if apple_retry:
                        allowed.add('runtime/crates/trames-linux/src/network.rs')
                    if any(p not in allowed and not p.startswith(('docs/', 'runtime/crates/trames-memory/src/', 'runtime/crates/trames-memory/examples/')) for p in changed):
                        raise RuntimeError('Firefox memory plan includes unrelated changes')
                    print('Check demand-zero MMU/accounting, coordinator, mapped VFS, Linux and original-musl sparse BSS on Linux/Apple ARM64. Reuse unchanged CPU opcode/GPU/UI/guest artifacts. No IPA.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=firefox-memory\nskip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                        output.write('firefox_memory_apple_retry=' + str(apple_retry).lower() + '\n')
                        output.write('firefox_memory_retry_run=' + str(plan['memory_apple_retry']['run'] if apple_retry else '') + '\n')
                        output.write('firefox_memory_readonly_followup=' + str(readonly_followup).lower() + '\n')
                    return
                if plan.get('native_scope') == 'firefox-clock':
                    allowed = {'crates/trames-core/src/process.rs', 'crates/trames-core/src/process_service.rs',
                               'crates/trames-core/src/worker_syscalls.rs', 'tests/integration/firefox.py',
                               'tests/fixtures/firefox-clock-probe.c', 'scripts/build-firefox-clock-fixture.sh',
                               'scripts/ios-ci-scope.py', '.github/workflows/linux.yml', '.github/workflows/ios.yml'}
                    if any(p not in allowed and not p.startswith('docs/') for p in changed):
                        raise RuntimeError('Firefox clock plan includes unrelated changes')
                    print('Check coordinator storage and coarse-clock epochs: Core tests and original musl clock ABI on Linux/Apple ARM64. Reuse unchanged Memory/VFS/Linux/CPU/GPU/UI checks. No IPA.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=firefox-clock\nskip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                    return
                if plan.get('native_scope') in ('firefox-layout', 'firefox-mmu-cap'):
                    allowed = {'runtime/crates/trames-linux/src/state.rs', 'tests/integration/firefox.py',
                               'runtime/crates/trames-memory/src/mmu/host.rs', 'runtime/crates/trames-memory/src/mmu/fault.rs',
                               'scripts/ios-ci-scope.py', '.github/workflows/linux.yml',
                               '.github/workflows/ios.yml'}
                    if plan['native_scope'] == 'firefox-mmu-cap':
                        allowed.remove('runtime/crates/trames-linux/src/state.rs')
                        allowed.update({'runtime/crates/trames-memory/src/mmu/mod.rs', 'runtime/crates/trames-memory/src/mmu/tests.rs'})
                    else:
                        allowed.difference_update({'runtime/crates/trames-memory/src/mmu/host.rs', 'runtime/crates/trames-memory/src/mmu/fault.rs'})
                    if any(p not in allowed and not p.startswith('docs/') for p in changed):
                        raise RuntimeError('Firefox address-layout plan includes unrelated changes')
                    print('Check affected Linux mapping policy or MMU capacity on Linux/Apple ARM64 plus iOS embedding; reuse unchanged modules, graphics, UI and guest artifacts. No IPA yet.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope='+plan['native_scope']+'\nskip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                    return
                if plan.get('native_scope') == 'firefox-runtime':
                    allowed = {'runtime/crates/trames-vfs/src/lib.rs', 'runtime/crates/trames-vfs/src/ipc.rs',
                               'runtime/crates/trames-vfs/src/clone_resources.rs', 'crates/trames-core/src/process.rs',
                               'runtime/crates/trames-vfs/src/mapped.rs', 'runtime/crates/trames-memory/src/backing.rs',
                               'runtime/crates/trames-memory/src/parallel/snapshot.rs',
                               'runtime/crates/trames-vfs/src/ipc/native.rs', 'runtime/crates/trames-linux/src/state.rs',
                               'tests/fixtures/firefox-socket-probe.c', 'tests/integration/firefox.py',
                               'scripts/build-firefox-fixture.sh', 'scripts/ios-ci-scope.py',
                               '.github/workflows/linux.yml', '.github/workflows/ios.yml'}
                    if any(p not in allowed and not p.startswith('docs/') for p in changed):
                        raise RuntimeError('Firefox runtime plan includes unrelated changes')
                    print('Check changed coordinator/clone-resource paths and original musl socket ABI on Linux/Apple ARM64; reuse unchanged CPU/MMU/GPU/UI/guest artifacts. No IPA.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=firefox-runtime\nskip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                    return
                if plan.get('native_scope') == 'xwayland-dialog':
                    allowed = {'patches/moltenvk/0006-pack-compatible-raster-varying-components.patch', 'tests/fixtures/vulkan-components-packed.vert', 'tests/fixtures/vulkan-components-packed.frag', 'tests/fixtures/vulkan-components-split.vert', 'tests/fixtures/vulkan-components-split.frag', 'scripts/build-moltenvk.py', 'patches/moltenvk/0005-native-transparent-black-border.patch', 'tests/fixtures/vulkan-texture.c', 'accelerator/vulkan/graphics.inc', 'crates/trames-vulkan/src/backend/graphics.rs', 'tests/fixtures/zink-metal-pixels.m', 'tests/fixtures/vulkan-render.c',
                               'tests/integration/xwayland-dialog.py', 'tests/integration/xwayland-dialog-prepare.py',
                               '.github/workflows/xwayland-dialog.yml', '.github/workflows/ios.yml',
                               '.github/workflows/linux.yml', 'scripts/ios-ci-scope.py'}
                    if any(p not in allowed and not p.startswith('docs/') for p in changed):
                        raise RuntimeError('Dialog scope includes unrelated changes')
                    print('Only Vulkan bridge/layout and original mousepad save-dialog checks run in xwayland-dialog.yml. Reuse passed unchanged CPU/Memory/Linux/guest/UI. No App rebuild.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=xwayland-dialog\nskip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                    return
                if plan.get('native_scope') == 'compatibility-probe':
                    outputs = compatibility_probe_outputs(plan, repo, dict(request.headers))
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        for key, value in outputs.items():
                            output.write(f'{key}={value}\n')
                    return
                if plan.get('native_scope') in {'zink-foundation', 'zink-sync', 'zink-gles', 'zink-wayland'}:
                    allowed = {'accelerator/vulkan/icd.c', 'accelerator/vulkan/queries.inc',
                               'accelerator/vulkan/templates.inc', 'accelerator/vulkan/entrypoints.inc',
                               'runtime/crates/trames-distro/src/vulkan.rs', 'tests/fixtures/vulkan-queries.c',
                               'tests/fixtures/zink-egl.c', 'tests/fixtures/vulkan-compute.c',
                               'tests/fixtures/zink-es3.c', 'tests/integration/zink-es3.py',
                               'tests/fixtures/vulkan-feedback.c', 'tests/fixtures/vulkan-feedback.vert',
                               'tests/integration/vulkan-feedback.py',
                               'tests/fixtures/vulkan-stencil.c', 'tests/fixtures/vulkan-stencil.vert',
                               'tests/fixtures/vulkan-stencil.frag', 'tests/fixtures/vulkan-stencil-sample.frag',
                               'tests/integration/vulkan-stencil.py',
                               'tests/fixtures/vulkan-divisor.c', 'tests/fixtures/vulkan-divisor.vert',
                               'tests/fixtures/vulkan-divisor.frag', 'tests/integration/vulkan-divisor.py',
                               'tests/fixtures/vulkan-texture.c', 'tests/integration/zink-foundation.py',
                               'scripts/prepare-zink-guest.py', 'scripts/build-vulkan-guest.sh',
                               'scripts/ios-ci-scope.py', '.github/workflows/ios.yml', '.github/workflows/linux.yml',
                               'scripts/prepare-vulkan.py', 'scripts/build-moltenvk.py', 'scripts/build-ios.sh',
                               'patches/moltenvk/0001-use-source-mip-depth-for-volume-blits.patch',
                               'patches/moltenvk/0002-require-native-gpu-timestamp-counters.patch', 'patches/moltenvk/0003-native-gpu-transform-feedback.patch', 'patches/moltenvk/0004-allow-native-tf-entry-arguments.patch', 'scripts/build-vulkan-native-tf.py', '.github/workflows/ios.yml', 'tests/integration/vulkan-native-tf-reference.py', 'tests/integration/vulkan-native-tf-runtime.py'}
                    sync = plan.get('native_scope') in {'zink-sync','zink-gles','zink-wayland'}
                    if sync:
                        allowed.update({'accelerator/vulkan/zink.inc', 'accelerator/vulkan/graphics.inc', 'tests/fixtures/vulkan-timeline.c',
                                        'tests/fixtures/vulkan-render.c', 'tests/fixtures/vulkan-resources.c', 'tests/fixtures/vulkan-resources.comp', 'tests/fixtures/vulkan-budget.c',
                                        'scripts/build-vulkan-fixtures.sh', 'scripts/prepare-zink-native-bundle.py'})
                    if plan.get('native_scope') == 'zink-wayland':
                        allowed.update({'tests/fixtures/zink-wayland.c', 'tests/fixtures/vulkan-copy.c',
                                        'tests/fixtures/wayland-relay.h', 'accelerator/vulkan/wayland.inc', 'accelerator/vulkan/wsi.inc',
                                        'tests/integration/firefox.py',
                                        'Cargo.lock', 'runtime/crates/trames-wayland/Cargo.toml',
                                        'runtime/crates/trames-wayland/native-wsi.xml',
                                        'runtime/crates/trames-wayland/src/lib.rs', 'runtime/crates/trames-wayland/src/wsi.rs', 'runtime/crates/trames-wayland/src/frame.rs',
                                        'runtime/crates/trames-wayland/src/server.rs', 'runtime/crates/trames-wayland/src/protocol.rs', 'runtime/crates/trames-wayland/src/scene.rs',
                                        'runtime/crates/trames-vfs/src/ipc/native.rs',
                                        'tests/fixtures/zink-metal-pixels.m', 'tests/integration/zink-wayland.py',
                                        'scripts/build-zink-egl.py', 'scripts/zink-egl-requirements.txt', 'patches/mesa/0001-wayland-kopper-native-wsi.patch',
                                        'runtime/crates/trames-distro/src/lib.rs', 'crates/trames-ffi/src/session.rs', 'scripts/build-ios.sh'})
                        clear_reuse = plan.get('zink_attachment_clear_reuse')
                        if clear_reuse:
                            # The new operation/range metadata does not change
                            # socket capabilities, native WSI, sampling or TF.
                            stable = ['runtime/crates/trames-wayland', 'runtime/crates/trames-vfs',
                                      'crates/trames-core', 'runtime/crates/trames-memory',
                                      'crates/trames-vulkan/src/backend/wayland.rs',
                                      'crates/trames-vulkan/native', 'accelerator/vulkan/wayland.inc',
                                      'accelerator/vulkan/icd.c', 'Cargo.toml', 'Cargo.lock',
                                      'rust-toolchain.toml', 'vendor', 'patches/moltenvk']
                            if subprocess.check_output(['git', 'diff', '--name-only', clear_reuse['sha'], 'HEAD', '--', *stable]).strip():
                                raise RuntimeError('Attachment-clear follow-up changed passed WSI/runtime inputs')
                            def clear_evidence(path):
                                req = urllib.request.Request(f'https://api.github.com/repos/{repo}/'+path, headers=dict(request.headers))
                                with urllib.request.urlopen(req, timeout=30) as response:
                                    return json.load(response)
                            run = clear_evidence(f"actions/runs/{clear_reuse['run']}")
                            jobs = clear_evidence(f"actions/runs/{clear_reuse['run']}/jobs")['jobs']
                            if run['head_sha'] != clear_reuse['sha'] or run['head_repository']['full_name'] != repo or run['path'] != '.github/workflows/linux.yml' or any(
                                not any(j['name'] == name and j['conclusion'] == 'success' for j in jobs)
                                for name in ('zink-wayland-linux', 'zink-wayland-apple')):
                                raise RuntimeError('Missing passed source-bound Linux/Metal relay and sampling evidence')
                            print('Reuse passed unchanged WSI capabilities, relay lifecycle, descriptor and sampling GPU checks; run new attachment-clear pixels and Vulkan/iOS checks.')
                        damage_reuse = plan.get('zink_present_damage_reuse')
                        if damage_reuse:
                            stable = ['crates/trames-vulkan', 'runtime/crates/trames-vfs',
                                      'crates/trames-core', 'runtime/crates/trames-memory', 'crates/trames-cpu',
                                      'crates/trames-accel', 'crates/trames-native', 'runtime/crates/trames-distro',
                                      'crates/trames-ffi', 'accelerator/musl',
                                      'accelerator/vulkan/graphics.inc', 'accelerator/vulkan/zink.inc',
                                      'accelerator/vulkan/wayland.inc', 'accelerator/vulkan/queries.inc',
                                      'accelerator/vulkan/templates.inc', 'Cargo.toml', 'Cargo.lock',
                                      'rust-toolchain.toml', 'vendor', 'patches/moltenvk']
                            if subprocess.check_output(['git', 'diff', '--name-only', damage_reuse['sha'], 'HEAD', '--', *stable]).strip():
                                raise RuntimeError('Damage-present follow-up changed passed GPU/runtime inputs')
                            def damage_evidence(path):
                                req = urllib.request.Request(f'https://api.github.com/repos/{repo}/'+path, headers=dict(request.headers))
                                with urllib.request.urlopen(req, timeout=30) as response:
                                    return json.load(response)
                            run = damage_evidence(f"actions/runs/{damage_reuse['run']}")
                            jobs = damage_evidence(f"actions/runs/{damage_reuse['run']}/jobs")['jobs']
                            if run['head_sha'] != damage_reuse['sha'] or run['head_repository']['full_name'] != repo or run['path'] != '.github/workflows/linux.yml' or any(
                                not any(j['name'] == name and j['conclusion'] == 'success' for j in jobs)
                                for name in ('zink-wayland-linux', 'zink-wayland-apple')):
                                raise RuntimeError('Missing passed source-bound Linux/Metal GPU evidence')
                            print('Reuse unchanged native GPU/TF/descriptor/sampling/clear modules; check affected Wayland, relayed EGL damage-swap pixels/lifecycle and iOS embedding.')
                        budget_reuse = plan.get('zink_gpu_budget_reuse')
                        if budget_reuse:
                            allowed.add('crates/trames-core/src/process.rs')
                            stable = ['runtime/crates/trames-wayland', 'runtime/crates/trames-vfs',
                                      'runtime/crates/trames-memory', 'crates/trames-cpu', 'runtime/crates/trames-linux',
                                      'crates/trames-accel', 'crates/trames-native', 'runtime/crates/trames-distro',
                                      'crates/trames-ffi', 'crates/trames-vulkan/native',
                                      'crates/trames-vulkan/src/backend/coherent.rs',
                                      'crates/trames-vulkan/src/backend/wayland.rs',
                                      'accelerator', 'Cargo.toml', 'Cargo.lock',
                                      'rust-toolchain.toml', 'vendor', 'patches/moltenvk']
                            if subprocess.check_output(['git', 'diff', '--name-only', budget_reuse['sha'], 'HEAD', '--', *stable]).strip():
                                raise RuntimeError('GPU quota follow-up changed passed WSI/native runtime inputs')
                            core = 'crates/trames-core/src/process.rs'
                            core_delta = subprocess.check_output(['git', 'diff', '--name-only', budget_reuse['sha'], 'HEAD', '--', 'crates/trames-core'], text=True).splitlines()
                            old = subprocess.check_output(['git', 'show', budget_reuse['sha']+':'+core], text=True)
                            expected = old.replace('gpu_budget: trames_vulkan::Budget::default(),', 'gpu_budget: trames_vulkan::Budget::for_guest_memory(runtime.memory_limits.total),', 1)
                            if any(p != core for p in core_delta) or Path(core).read_text() != expected:
                                raise RuntimeError('GPU quota follow-up changed unrelated Core behavior')
                            def budget_evidence(path):
                                req = urllib.request.Request(f'https://api.github.com/repos/{repo}/'+path, headers=dict(request.headers))
                                with urllib.request.urlopen(req, timeout=30) as response:
                                    return json.load(response)
                            run = budget_evidence(f"actions/runs/{budget_reuse['run']}")
                            jobs = budget_evidence(f"actions/runs/{budget_reuse['run']}/jobs")['jobs']
                            if run['head_sha'] != budget_reuse['sha'] or run['head_repository']['full_name'] != repo or run['path'] != '.github/workflows/linux.yml' or any(
                                not any(j['name'] == name and j['conclusion'] == 'success' for j in jobs)
                                for name in ('zink-wayland-linux', 'zink-wayland-apple')):
                                raise RuntimeError('Missing source-bound Linux/Metal damage-swap evidence')
                            print('Reuse passed Wayland/damage-swap pixels; check changed Vulkan quota/metadata, original C simultaneous coherent mappings/reallocation and iOS embedding.')
                        frame_reuse = plan.get('zink_frame_boundaries_reuse')
                        if frame_reuse:
                            stable = ['runtime/crates/trames-vfs', 'crates/trames-core', 'runtime/crates/trames-memory',
                                      'crates/trames-cpu', 'runtime/crates/trames-linux', 'crates/trames-accel',
                                      'crates/trames-native', 'runtime/crates/trames-distro', 'crates/trames-ffi',
                                      'crates/trames-vulkan/native', 'crates/trames-vulkan/src/budget.rs',
                                      'crates/trames-vulkan/src/backend/coherent.rs',
                                      'crates/trames-vulkan/src/backend/wayland.rs',
                                      'accelerator', 'Cargo.toml', 'Cargo.lock', 'rust-toolchain.toml',
                                      'vendor', 'patches/moltenvk']
                            if subprocess.check_output(['git', 'diff', '--name-only', frame_reuse['sha'], 'HEAD', '--', *stable]).strip():
                                raise RuntimeError('Frame boundary repair changed passed runtime/native WSI/quota/TF inputs')
                            def frame_evidence(path):
                                req = urllib.request.Request(f'https://api.github.com/repos/{repo}/'+path, headers=dict(request.headers))
                                with urllib.request.urlopen(req, timeout=30) as response:
                                    return json.load(response)
                            run = frame_evidence(f"actions/runs/{frame_reuse['run']}")
                            jobs = frame_evidence(f"actions/runs/{frame_reuse['run']}/jobs")['jobs']
                            if run['head_sha'] != frame_reuse['sha'] or run['head_repository']['full_name'] != repo or run['path'] != '.github/workflows/linux.yml' or any(
                                not any(j['name'] == name and j['conclusion'] == 'success' for j in jobs)
                                for name in ('zink-wayland-linux', 'zink-wayland-apple')):
                                raise RuntimeError('Missing passed exact-source Linux/Metal quota evidence')
                            print('Reuse passed unchanged runtime, native WSI/TF and quota; check changed Wayland scheduling, Vulkan boundary pixels and iOS embedding.')
                    if any(p not in allowed and not p.startswith(('docs/', 'tests/fixtures/vulkan-native-tf')) and not (sync and p.startswith('crates/trames-vulkan/')) for p in changed):
                        raise RuntimeError('Zink foundation plan includes unrelated source changes')
                    path = 'runtime/crates/trames-distro/src/vulkan.rs'
                    old = subprocess.check_output(['git', 'show', base + ':' + path], text=True).split('#[cfg(test)]')[0]
                    new = Path(path).read_text().split('#[cfg(test)]')[0]
                    if plan.get('native_scope') != 'zink-wayland' and old.replace('"api_version":"1.0.0"', '"api_version":"1.1.0"') != new:
                        raise RuntimeError('Only bundled instance-version metadata may change in native Rust')
                    gpu_reuse = plan.get('zink_vulkan_validation_reuse')
                    if gpu_reuse:
                        source = gpu_reuse['sha']
                        if plan.get('native_scope') != 'zink-wayland' or subprocess.check_output([
                                'git', 'diff', '--name-only', source, 'HEAD', '--',
                                'crates/trames-vulkan', 'accelerator/vulkan', 'Cargo.lock', 'rust-toolchain.toml']).strip():
                            raise RuntimeError('Vulkan validation reuse requires exact unchanged Vulkan source/toolchain')
                        def gpu_evidence(path):
                            req = urllib.request.Request(f'https://api.github.com/repos/{repo}/' + path, headers=dict(request.headers))
                            with urllib.request.urlopen(req, timeout=30) as response:
                                return json.load(response)
                        run = gpu_evidence(f"actions/runs/{gpu_reuse['run']}")
                        if run['head_sha'] != source or run['head_repository']['full_name'] != repo:
                            raise RuntimeError('Vulkan validation source/repository mismatch')
                        jobs = gpu_evidence(f"actions/runs/{gpu_reuse['run']}/jobs")['jobs']
                        for name, step in [
                                ('zink-wayland-linux', 'Check affected Vulkan module and build production embedding'),
                                ('zink-wayland-apple', 'Verify exact guest inputs and Apple embedding')]:
                            job = next(j for j in jobs if j['name'] == name)
                            if not any(s['name'] == step and s['conclusion'] == 'success' for s in job['steps']):
                                raise RuntimeError('Missing passed exact-source Vulkan module checks: ' + name)
                    wayland_reuse = plan.get('zink_wayland_linux_reuse')
                    if wayland_reuse:
                        source = wayland_reuse['sha']
                        delta = subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD'], text=True).splitlines()
                        ci_only = {'.github/ios-validation-plan.json', '.github/workflows/linux.yml',
                                   'scripts/ios-ci-scope.py', 'scripts/prepare-zink-native-bundle.py'}
                        if plan.get('native_scope') != 'zink-wayland' or any(p not in ci_only and not p.startswith('docs/') for p in delta):
                            raise RuntimeError('Wayland Linux reuse requires unchanged runtime, EGL, fixtures and build inputs')
                        run_id = wayland_reuse['run']
                        run = gpu_evidence(f'actions/runs/{run_id}')
                        job = next(j for j in gpu_evidence(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == 'zink-wayland-linux')
                        if (run['head_sha'] != source or run['path'] != '.github/workflows/linux.yml'
                                or run['head_repository']['full_name'] != repo or job['conclusion'] != 'success'):
                            raise RuntimeError('Missing passed exact-source Linux Wayland job')
                        artifacts = gpu_evidence(f'actions/runs/{run_id}/artifacts')['artifacts']
                        if not any(a['name'] == 'zink-native-bundle' and not a['expired'] for a in artifacts):
                            raise RuntimeError('Passed Linux guest bundle expired or missing')
                    egl_apple_reuse = plan.get('zink_egl_apple_validation_reuse')
                    if egl_apple_reuse:
                        source = egl_apple_reuse['sha']
                        delta = subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD'], text=True).splitlines()
                        if not wayland_reuse or any(p not in ci_only and not p.startswith('docs/') for p in delta):
                            raise RuntimeError('Apple EGL module reuse requires unchanged runtime and tests')
                        run_id = egl_apple_reuse['run']
                        run = gpu_evidence(f'actions/runs/{run_id}')
                        job = next(j for j in gpu_evidence(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == 'zink-wayland-apple')
                        if (run['head_sha'] != source or run['head_repository']['full_name'] != repo
                                or not any(s['name'] == 'Verify exact guest inputs and Apple embedding' and s['conclusion'] == 'success' for s in job['steps'])):
                            raise RuntimeError('Missing passed exact-source Apple EGL module checks')
                    reuse = plan.get('zink_reuse_apple')
                    if reuse:
                        source = reuse['sha']
                        paths = ['crates', 'Cargo.toml', 'Cargo.lock', '.cargo', 'vendor', 'rust-toolchain.toml']
                        if subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD', '--', *paths]).strip():
                            raise RuntimeError('Apple Zink metadata acceptance requires unchanged Rust/build inputs')
                        def apple_job(text):
                            block = text.split('  zink-foundation-apple:\n', 1)[1].split('  runtime-fastpaths-apple:\n', 1)[0]
                            return '\n'.join(l for l in block.splitlines() if not l.strip().startswith('if:'))
                        previous = subprocess.check_output(['git', 'show', source + ':.github/workflows/ios.yml'], text=True)
                        if apple_job(previous) != apple_job(Path('.github/workflows/ios.yml').read_text()):
                            raise RuntimeError('Apple metadata job commands changed; cannot reuse')
                        def evidence(path):
                            req = urllib.request.Request(f'https://api.github.com/repos/{repo}/' + path, headers=dict(request.headers))
                            with urllib.request.urlopen(req, timeout=30) as response:
                                return json.load(response)
                        run_id = reuse['run']
                        run = evidence(f'actions/runs/{run_id}')
                        job = next(j for j in evidence(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == 'zink-foundation-apple')
                        if run['head_sha'] != source or job['conclusion'] != 'success':
                            raise RuntimeError('Missing exact-source successful Apple Zink metadata job')
                    linux_reuse = plan.get('zink_reuse_linux_checks')
                    if linux_reuse:
                        source = linux_reuse['sha']
                        paths = ['crates', 'Cargo.toml', 'Cargo.lock', '.cargo', 'vendor', 'rust-toolchain.toml',
                                 'accelerator', 'scripts/build-vulkan-guest.sh', 'tests/integration/embedding-reopen.py']
                        if subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD', '--', *paths]).strip():
                            raise RuntimeError('Linux metadata/reopen reuse requires unchanged runtime, guest ICD and build inputs')
                        def linux_checks(text):
                            block = text.split('      - name: Check bundle metadata and build unchanged native backend\n', 1)[1].split('      - name:', 1)[0]
                            return '\n'.join(l for l in block.splitlines() if not l.strip().startswith('if:'))
                        previous = subprocess.check_output(['git', 'show', source + ':.github/workflows/linux.yml'], text=True)
                        if linux_checks(previous) != linux_checks(Path('.github/workflows/linux.yml').read_text()):
                            raise RuntimeError('Linux metadata/reopen job commands changed; cannot reuse')
                        run_id = linux_reuse['run']
                        def linux_evidence(path):
                            req = urllib.request.Request(f'https://api.github.com/repos/{repo}/' + path, headers=dict(request.headers))
                            with urllib.request.urlopen(req, timeout=30) as response:
                                return json.load(response)
                        run = linux_evidence(f'actions/runs/{run_id}')
                        job = next(j for j in linux_evidence(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == 'zink-foundation')
                        if (run['head_sha'] != source or run['path'] != '.github/workflows/linux.yml'
                                or run['head_repository']['full_name'] != repo or not any(
                                    s['name'] == 'Check bundle metadata and build unchanged native backend' and s['conclusion'] == 'success'
                                    for s in job['steps'])):
                            raise RuntimeError('Missing successful exact-source Linux bundle/build/reopen checks')
                    sync_reuse = plan.get('zink_sync_reuse_checks')
                    if sync_reuse:
                        source = sync_reuse['sha']
                        delta = subprocess.check_output(['git', 'diff', '--name-only', source, 'HEAD'], text=True).splitlines()
                        ci_only = {'.github/ios-validation-plan.json', '.github/workflows/ios.yml', '.github/workflows/linux.yml', 'scripts/ios-ci-scope.py'}
                        if not sync or any(p not in ci_only and not p.startswith('docs/') for p in delta):
                            raise RuntimeError('Zink sync reuse requires exact unchanged runtime, guest ICD, fixtures and build scripts')
                        def no_conditions(block):
                            return '\n'.join(l for l in block.splitlines() if not l.strip().startswith('if:'))
                        for filename, start, end in [
                            ('linux.yml', '  zink-foundation:\n', '  runtime-fastpaths:\n'),
                            ('ios.yml', '      - name: Check changed Apple Vulkan backend and iOS embedding\n', '      - name: Validate original MoltenVK timeline GPU path\n')]:
                            path = '.github/workflows/' + filename
                            previous = subprocess.check_output(['git', 'show', source + ':' + path], text=True)
                            old = previous.split(start, 1)[1].split(end, 1)[0]
                            new = Path(path).read_text().split(start, 1)[1].split(end, 1)[0]
                            if no_conditions(old) != no_conditions(new):
                                raise RuntimeError('Reused Zink validation commands changed')
                        def sync_evidence(path):
                            req = urllib.request.Request(f'https://api.github.com/repos/{repo}/' + path, headers=dict(request.headers))
                            with urllib.request.urlopen(req, timeout=30) as response:
                                return json.load(response)
                        for filename, key, name in [('linux.yml','linux','zink-foundation'), ('ios.yml','apple','zink-foundation-apple')]:
                            run_id = sync_reuse[key]
                            run = sync_evidence(f'actions/runs/{run_id}')
                            job = next(j for j in sync_evidence(f'actions/runs/{run_id}/jobs')['jobs'] if j['name'] == name)
                            if run['head_sha'] != source or run['path'] != '.github/workflows/' + filename or run['head_repository']['full_name'] != repo:
                                raise RuntimeError('Zink sync acceptance does not match exact source/workflow')
                            if key == 'linux' and job['conclusion'] != 'success':
                                raise RuntimeError('Missing passed exact-source Linux GPU foundation')
                            if key == 'apple' and not any(step['name'] == 'Check changed Apple Vulkan backend and iOS embedding' and step['conclusion'] == 'success' for step in job['steps']):
                                raise RuntimeError('Missing passed exact-source Apple Vulkan/iOS checks')
                    metal_reuse = zink_metal_only_reuse(plan, repo, dict(request.headers))
                    cache_only = tf_cache_only_reuse(plan, repo, dict(request.headers))
                    print('Original Mesa Wayland EGL: native WSI/image copies, two-window resize/close and retained Metal texture pixels; reuse unchanged offscreen/CPU/MMU/UI/network checks; no IPA.' if plan.get('native_scope') == 'zink-wayland' else 'Zink synchronization: changed guest ICD and native Vulkan backend, original driver timeline/viewport GPU checks and iOS embedding; CPU/MMU/UI/network unchanged, no IPA.' if sync else 'Guest ICD query/template foundation: original loader/Mesa probe, native GPU compute/texture oracles, bundle upgrade/reopen and Apple/iOS metadata checks. CPU/MMU/native GPU backend/UI unchanged; no IPA or repeated unrelated acceptance.')
                    feedback_retry = plan.get('zink_feedback_only')
                    if feedback_retry:
                        source = feedback_retry['sha']
                        if plan['native_scope'] != 'zink-gles' or base != source or subprocess.check_output([
                                'git', 'diff', '--name-only', source, 'HEAD', '--',
                                'crates/trames-vulkan/src', 'accelerator/vulkan', 'Cargo.lock', 'rust-toolchain.toml']).strip():
                            raise RuntimeError('Feedback retry requires unchanged validated Vulkan/ICD runtime')
                        def feedback_evidence(path):
                            req = urllib.request.Request(f'https://api.github.com/repos/{repo}/' + path, headers=dict(request.headers))
                            with urllib.request.urlopen(req, timeout=30) as response:
                                return json.load(response)
                        retry_run = feedback_evidence(f"actions/runs/{feedback_retry['run']}")
                        retry_jobs = feedback_evidence(f"actions/runs/{feedback_retry['run']}/jobs")['jobs']
                        linux = next(j for j in retry_jobs if j['name'] == 'zink-foundation')
                        apple = next(j for j in retry_jobs if j['name'] == 'zink-gles-apple')
                        if (retry_run['head_sha'] != source or retry_run['head_repository']['full_name'] != repo
                                or retry_run['path'] != '.github/workflows/linux.yml' or linux['conclusion'] != 'success'
                                or not any(s['name'] == 'Verify exact guest inputs, Apple modules and iOS embedding'
                                           and s['conclusion'] == 'success' for s in apple['steps'])):
                            raise RuntimeError('Missing passed exact-source Linux and Apple module checks')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=' + plan['native_scope'] + '\n')
                        if plan.get('zink_tf_only'):
                            output.write('zink_tf_only=true\nzink_feedback_only=true\n')
                        if cache_only:
                            output.write('zink_tf_cache_only=true\n')
                        if plan.get('zink_mesa_only'):
                            if not metal_reuse:
                                raise RuntimeError('Mesa-only completion requires verified exact-source Linux/Apple query reuse')
                            output.write('zink_mesa_only=true\n')
                        if feedback_retry:
                            output.write('zink_feedback_only=true\n')
                            print('Retry changed query/feedback clients and original native timestamp capability patch only; reuse passed unchanged Vulkan module, Mesa/image and iOS FFI acceptance.')
                        output.write('skip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                        if metal_reuse:
                            output.write(f"zink_sync_skip_linux=true\nzink_guest_bundle_run={metal_reuse['run']}\nzink_guest_bundle_sha={metal_reuse['sha']}\n")
                            print('Reuse exact-source Linux GPU/Mesa and passed Apple module/iOS checks; rebuild corrected MoltenVK source and rerun only native Metal pixels/Mesa plus iOS native library compilation. No IPA.')
                        if plan.get('zink_divisor_only'):
                            if plan['native_scope'] != 'zink-gles' or not plan.get('zink_mrt_only'):
                                raise RuntimeError('Divisor plan requires changed GLES backend validation')
                            output.write('zink_divisor_only=true\n')
                            print('Run affected native/guest vertex input rates, original Mesa instancing and real GLES3/TF gate plus iOS FFI; reuse passed unchanged image/stencil, query, CPU/network/UI checks.')
                        if plan.get('zink_stencil_only'):
                            if plan['native_scope'] != 'zink-gles' or not plan.get('zink_mrt_only'):
                                raise RuntimeError('Stencil plan requires changed GLES backend validation')
                            output.write('zink_stencil_only=true\n')
                            print('Run affected depth/stencil transfer, rasterization and sampling checks, original Mesa capability probes and iOS embedding; reuse passed unchanged query/TF, color-only Mesa, CPU/network/UI checks.')
                        if plan.get('zink_mrt_only'):
                            if plan['native_scope'] != 'zink-gles':
                                raise RuntimeError('MRT plan requires the changed GLES native/guest validation')
                            output.write('zink_mrt_only=true\n')
                            print('Run changed MRT/format/image-subresource native pixels plus original Mesa dense/sparse, mipmap and cube draws on Linux and Apple; reuse unchanged CPU/MMU/network/UI and unrelated compute/timeline checks. No IPA.')
                        if gpu_reuse:
                            output.write('zink_egl_only=true\n')
                            print('Reuse exact unchanged Linux/Apple Vulkan module checks; validate changed EGL component, distro install and embedding.')
                        if wayland_reuse:
                            output.write(f"zink_wayland_linux_run={wayland_reuse['run']}\nzink_wayland_linux_sha={wayland_reuse['sha']}\n")
                            print('Reuse passed unchanged Linux Wayland build and exact guest bundle; retry only Apple acceptance.')
                        if egl_apple_reuse:
                            output.write('zink_egl_skip_apple_checks=true\n')
                            print('Reuse unchanged passed Apple distro/FFI Clippy, three installation tests and iOS compilation.')
                        if sync_reuse:
                            output.write('zink_sync_skip_linux=true\nzink_sync_skip_apple_checks=true\n')
                            print('Reuse exact-source passed Linux GPU and Apple module/iOS checks; retry only macOS MoltenVK native-example linking/runtime.')
                        if reuse:
                            output.write('zink_skip_apple=true\n')
                            print('Reuse exact unchanged Apple manifest/iOS checks; retry unfinished Linux preparation and foundation checks only.')
                        if linux_reuse:
                            output.write('zink_skip_linux_checks=true\n')
                            print('Reuse exact unchanged passed Linux bundle/reopen/module checks; build the runner and retry the changed native query oracle and unfinished graphics checks.')
                    return
                if plan.get('native_scope') == 'docker-default-lifecycle':
                    allowed = {'.github/workflows/linux.yml', '.github/workflows/ios.yml', 'README.md',
                               'scripts/ios-ci-scope.py', 'tests/integration/docker-default.py',
                               'tests/integration/docker-default-lifecycle.py',
                               'tests/integration/docker-default-lifecycle.sh'}
                    if not all(p.startswith('docs/') or p in allowed for p in changed):
                        raise RuntimeError('Lifecycle-only plan includes unrelated files')
                    outputs = docker_default_lifecycle_outputs(plan, repo, dict(request.headers))
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        for key, value in outputs.items():
                            output.write(f'{key}={value}\n')
                    return
                if plan.get('native_scope') == 'docker-default':
                    allowed = {'runtime/crates/trames-vfs/src/lib.rs', 'runtime/crates/trames-vfs/src/pseudo.rs',
                               'runtime/crates/trames-vfs/src/mounts.rs',
                               'runtime/crates/trames-net/src/lib.rs', 'runtime/crates/trames-linux/src/network.rs',
                               'crates/trames-core/src/process.rs', 'tests/fixtures/socket-timeouts.c',
                               'crates/trames-cli/src/main.rs',
                               'scripts/build-socket-timeouts-fixture.sh',
                               'tests/integration/docker-default.py', 'tests/integration/docker-default.sh',
                               'examples/docker/daemon.json', 'examples/docker/containerd.toml',
                               '.github/workflows/ios.yml', '.github/workflows/linux.yml',
                               'scripts/ios-ci-scope.py', 'README.md'}
                    if not all(p.startswith('docs/') or p in allowed for p in changed):
                        raise RuntimeError('Docker defaults plan contains unrelated source changes')
                    print('Original Docker defaults: affected VFS/proc metadata, socket timeouts and scheduler deadlines, original managed containerd/runc workflow, Apple counterparts and iOS FFI; reuse unchanged CPU/UI/GPU and unrelated network acceptance. No IPA.')
                    remount_retry = docker_defaults_remount_retry(plan, repo, dict(request.headers))
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=docker-default\n')
                        if remount_retry:
                            output.write('docker_default_remount=true\n')
                    return
                if plan.get('native_scope') == 'runtime-fastpaths':
                    allowed = {'crates/trames-core/src/process.rs', 'runtime/crates/trames-vfs/src/lib.rs',
                               'crates/trames-core/src/workers.rs', 'crates/trames-core/src/worker_syscalls.rs',
                               'crates/trames-core/src/lib.rs', 'crates/trames-core/src/process_seccomp.rs',
                               'crates/trames-core/src/process_service.rs', 'runtime/crates/trames-linux/src/state.rs',
                               'tests/fixtures/epoch-services.c', 'tests/fixtures/epoch-services-native.S',
                               'scripts/build-epoch-services-fixture.sh', 'tests/performance/epoch-services.py',
                               'tests/integration/epoch-services.py',
                               'crates/trames-cli/src/main.rs', 'tests/performance/session-sampler.c',
                               'runtime/crates/trames-vfs/src/mounts.rs', 'runtime/crates/trames-vfs/src/directory.rs',
                               'runtime/crates/trames-vfs/src/storage.rs', 'runtime/crates/trames-vfs/src/storage_tests.rs',
                               'runtime/crates/trames-vfs/src/disk_io.rs', 'runtime/crates/trames-vfs/src/pseudo.rs',
                               'runtime/crates/trames-vfs/src/overlay.rs', 'runtime/crates/trames-vfs/src/mapped.rs', 'runtime/crates/trames-vfs/src/pty.rs',
                               'runtime/crates/trames-vfs/src/memfd.rs', 'runtime/crates/trames-vfs/src/cgroup.rs',
                               'runtime/crates/trames-vfs/src/ipc/native.rs', 'scripts/ios-ci-scope.py',
                               '.github/workflows/ios.yml', '.github/workflows/linux.yml',
                               'tests/performance/session-coremark.py', 'tests/performance/prepare-coremark.py',
                               'tests/performance/session-docker-tty.py', 'tests/performance/disk-io-counts.c', 'tests/fixtures/disk-read-cost.c'}
                    if any(p not in allowed and not p.startswith('docs/') for p in changed):
                        raise RuntimeError('Runtime fastpath plan includes unrelated source changes')
                    if not plan.get('syscall_parallel'):
                        print('VFS path lookup and solo scheduling: affected Core/VFS tests, production multi-terminal ABI, paired original CoreMark on Apple host. Reuse unchanged CPU/MMU/network/GPU/UI acceptance; no IPA.')
                    reused = runtime_fastpaths_reuse(plan, repo, dict(request.headers))
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=runtime-fastpaths\n')
                        output.write('skip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
                        if plan.get('syscall_parallel'):
                            print('Worker-local unfiltered syscalls and VFS synchronization: check Core/VFS/CLI, real terminal control and Apple memory granularity. Reuse unchanged CPU interpreter/CoreMark acceptance; Docker timings retained separately. No IPA.')
                            output.write('syscall_parallel=true\n')
                        if plan.get('epoch_services'):
                            output.write('epoch_services=true\n')
                        if reused:
                            output.write('runtime_linux_reused=true\nruntime_apple_modules_reused=true\n')
                    return
                if plan.get('device_build'):
                    outputs = device_build_outputs(plan, repo, dict(request.headers))
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        for key, value in outputs.items():
                            output.write(f'{key}={value}\n')
                    return
                if plan.get('native_scope') == 'container-storage' and changed and all(container_storage_path(p) for p in changed):
                    print('Guest overlay storage: affected VFS and Linux file ABI, original Docker overlay2, Apple VFS and iOS FFI. Reuse unchanged CPU/UI/GPU/network and guest libraries; no IPA.')
                    skip_apple = skip_linux = skip_modules = proc_only = False
                    reuse = plan.get('storage_validation_reuse')
                    if reuse:
                        delta = subprocess.check_output(['git', 'diff', '--name-only', reuse['sha'], 'HEAD'], text=True).splitlines()
                        allowed = {'.github/ios-validation-plan.json', '.github/workflows/ios.yml',
                                   '.github/workflows/linux.yml', 'scripts/ios-ci-scope.py', 'README.md'}
                        unchanged = all(p.startswith('docs/') or p in allowed for p in delta)
                        proc_patch = reuse.get('proc_reopen_patch_sha256')
                        exact_proc_patch = False
                        if proc_patch and all(p.startswith('docs/') or p in allowed or p in {
                                'runtime/crates/trames-vfs/src/pseudo.rs', 'runtime/crates/trames-vfs/src/overlay.rs'} for p in delta):
                            patch = subprocess.check_output(['git', 'diff', '--binary', reuse['sha'], 'HEAD', '--', 'runtime/crates/trames-vfs/src/pseudo.rs'])
                            previous_overlay = subprocess.check_output(['git', 'show', reuse['sha'] + ':runtime/crates/trames-vfs/src/overlay.rs'], text=True).split('#[cfg(test)]')[0]
                            current_overlay = Path('runtime/crates/trames-vfs/src/overlay.rs').read_text().split('#[cfg(test)]')[0]
                            exact_proc_patch = hashlib.sha256(patch).hexdigest() == proc_patch and previous_overlay == current_overlay
                        if unchanged or exact_proc_patch:
                            for workflow_name, run_id in [('linux.yml', reuse['linux_run']), ('ios.yml', reuse['apple_run'])]:
                                url = f'https://api.github.com/repos/{repo}/actions/runs/{run_id}'
                                with urllib.request.urlopen(urllib.request.Request(url, headers=dict(request.headers)), timeout=30) as response:
                                    prior = json.load(response)
                                if prior['head_sha'] != reuse['sha'] or prior['path'] != '.github/workflows/' + workflow_name or prior['head_repository']['full_name'] != repo:
                                    continue
                                with urllib.request.urlopen(urllib.request.Request(url + '/jobs', headers=dict(request.headers)), timeout=30) as response:
                                    jobs = json.load(response)['jobs']
                                if workflow_name == 'linux.yml':
                                    steps = {step['name'] for job in jobs if job['name'] == 'targeted-runtime'
                                             for step in job.get('steps', []) if step['conclusion'] == 'success'}
                                    skip_modules = 'Check affected modules' in steps
                                    skip_linux = skip_modules and 'Exercise original guest Docker overlay2 and persistent image layers' in steps
                                else:
                                    skip_apple = any(job['name'] == 'container-storage-apple' and job['conclusion'] == 'success' for job in jobs)
                            print(f'Reuse unchanged overlay evidence: Linux modules={skip_modules}, Docker={skip_linux}, Apple/iOS={skip_apple}.')
                            if exact_proc_patch and skip_modules and skip_linux and skip_apple:
                                # Ordinary opens and dup/fork are unchanged; the
                                # exact proc reopen fix needs its own regression,
                                # affected clippy and cross-target compilation.
                                proc_only = True
                                skip_apple = skip_linux = skip_modules = False
                                print('Validate exact proc reopen delta; reuse completed overlay/Docker/platform IO checks.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=container-storage\n')
                        output.write('skip_native=' + str(skip_apple).lower() + '\n')
                        output.write('skip_linux_native=' + str(skip_linux).lower() + '\n')
                        output.write('skip_linux_modules=' + str(skip_modules).lower() + '\n')
                        output.write('storage_proc_only=' + str(proc_only).lower() + '\n')
                    return
                if plan.get('native_scope') == 'container-network' and changed and all(container_network_path(p) for p in changed):
                    print('Container network: source-bounded Net/VFS/Linux and affected kernel adapters, actual guest networking/Docker and ARM64 iOS FFI only. Reuse unchanged CPU/UI/GPU/guest libraries; no IPA.')
                    retry=False
                    reuse=plan.get('network_lifetime_reuse')
                    if reuse and network_lifetime_source_unchanged(reuse['sha']):
                        passed=[]
                        for workflow_name,run_id in [('linux.yml',reuse['linux_run']),('ios.yml',reuse['apple_run'])]:
                            with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{run_id}',headers=dict(request.headers)),timeout=30) as response:
                                previous=json.load(response)
                            passed.append(previous['conclusion']=='success' and previous['head_sha']==reuse['sha'] and previous['path']=='.github/workflows/'+workflow_name and previous['head_repository']['full_name']==repo)
                        retry=all(passed)
                    fixture_retry=False
                    fixture_reuse=plan.get('network_validation_reuse')
                    if fixture_reuse and network_test_retry_source_unchanged(fixture_reuse['sha']):
                        verified=[]
                        for workflow_name,run_id,conclusion in [('linux.yml',fixture_reuse['linux_run'],'success'),('ios.yml',fixture_reuse['apple_run'],'failure')]:
                            with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{run_id}',headers=dict(request.headers)),timeout=30)as response:
                                prior=json.load(response)
                            verified.append(prior['conclusion']==conclusion and prior['head_sha']==fixture_reuse['sha'] and prior['path']=='.github/workflows/'+workflow_name and prior['head_repository']['full_name']==repo)
                        fixture_retry=all(verified)
                    virtual_retry=False
                    virtual_reuse=plan.get('network_virtual_reuse')
                    if fixture_retry and virtual_reuse:
                        verified=[]
                        for workflow_name,run_id,conclusion in [('linux.yml',virtual_reuse['linux_run'],'success'),('ios.yml',virtual_reuse['apple_run'],'failure')]:
                            with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{run_id}',headers=dict(request.headers)),timeout=30)as response:
                                prior=json.load(response)
                            verified.append(prior['conclusion']==conclusion and prior['head_sha']==virtual_reuse['sha'] and prior['path']=='.github/workflows/'+workflow_name and prior['head_repository']['full_name']==repo)
                        virtual_retry=all(verified)
                    if virtual_retry:print('Darwin host UDP echo fixture now uses an explicit 64KiB send buffer: reuse all40 Apple Net tests, 10 virtual tests and Linux changed-fixture results. Retry the single large-datagram test and remaining Apple checks.')
                    if fixture_retry:print('Only bounded native UDP test delivery waits changed: reuse successful Linux runtime/default Docker network and 39 Apple Net tests; retry changed fixtures and remaining Apple checks. All production bytes are unchanged.')
                    if retry:print('Reuse successful Linux/Apple libraries, virtual network and original Docker checks; only exact stack-lifetime delta, affected guest networking and iOS FFI run.')
                    with open(os.environ['GITHUB_OUTPUT'],'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=container-network\n')
                        output.write('skip_native=false\nskip_linux_native=false\nskip_linux_modules=false\n')
                        output.write('network_retry='+str(retry).lower()+'\n')
                        output.write('nftables='+str(plan.get('nftables_stage') is True).lower()+'\n')
                        output.write('docker_network='+str(plan.get('docker_network_stage') is True).lower()+'\n')
                        output.write('docker_network_retry='+str(fixture_retry).lower()+'\n')
                        output.write('docker_network_virtual_retry='+str(virtual_retry).lower()+'\n')
                        output.write('docker_bridge='+str(plan.get('docker_bridge_stage') is True).lower()+'\n')
                    if plan.get('docker_network_stage') is True:
                        print('Docker full network stage: Net/virtual, VFS network/events, Linux network/splice, Core splice scheduler compile, FFI managed DNS, CLI; signed iptables/NAT 1/4 CPU and original default Docker network/DNS/TCP/UDP/isolation at 4 CPU; Apple counterparts and iOS FFI. Reuse unchanged CPU/UI/GPU/guest libraries; no IPA.')
                    if plan.get('nftables_stage') is True:
                        print('nftables stage: affected Net libraries/virtual sockets, forwarding/TTL 1/4 CPU, original signed iptables-nft data filtering 1/4 CPU, iOS FFI. Reuse unchanged VFS/Linux/Core/Docker daemon/CPU/UI/GPU and guest libraries.')
                    if plan.get('docker_bridge_stage') is True:
                        print('Docker bridge stage: Net and VFS network checks, new Go/BusyBox control fixture 1/4 CPU, original Docker internal bridge/HTTP at 4 CPU, iOS FFI. Reuse unchanged Linux/Core/CPU/UI/GPU and previous forwarding/none-profile acceptance.')
                    return
                if plan.get('native_scope') == 'container-isolation' and changed and all(container_isolation_path(p) for p in changed):
                    # All affected kernel/network crates and the CPU hint are
                    # checked, followed by real guest ABI and original Docker.
                    # Untouched App/guest-library inputs retain 1.0.0 coverage.
                    print('Container isolation: affected native/ABI/Docker checks only; unchanged UI/GPU/guest libraries reused, no new IPA.')
                    retry=False
                    if plan.get('isolation_retry'):
                        reuse=plan['isolation_retry']
                        if isolation_retry_source_unchanged(reuse['sha']):
                            with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{reuse["linux_run"]}',headers=dict(request.headers)),timeout=30) as response:
                                previous=json.load(response)
                            retry=previous['conclusion']=='success' and previous['head_sha']==reuse['sha'] and previous['path']=='.github/workflows/linux.yml' and previous['head_repository']['full_name']==repo
                        if retry: print('Reusing passed Docker/network/library checks; only the new seccomp TRAP case, failed Darwin test and remaining Apple checks run.')
                    with open(os.environ['GITHUB_OUTPUT'],'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=container-isolation\n')
                        output.write('skip_native=false\nskip_linux_native=false\nskip_linux_modules=false\n')
                        output.write('isolation_retry='+str(retry).lower()+'\n')
                    return
                baseline=plan['reuse_guest_run']
                url=f'https://api.github.com/repos/{repo}/actions/runs/{baseline}'
                with urllib.request.urlopen(urllib.request.Request(url,headers=dict(request.headers)),timeout=30) as response:
                    validated=json.load(response)
                guest_paths=['guest','include','scripts/build-musl-hybrid.sh','scripts/build-vulkan-fixtures.sh','scripts/prepare-wayland-guest.py','scripts/prepare-vulkan.py',':(glob)tests/fixtures/*.c']
                guest_unchanged=not subprocess.check_output(['git','diff','--name-only',validated['head_sha'],'HEAD','--',*guest_paths],text=True).strip()
                if validated['conclusion']=='success' and guest_unchanged:
                    skip_native=False;skip_linux_native=False;skip_linux_modules=False
                    if plan.get('native_reuse'):
                        reuse=plan['native_reuse'];same_native=not subprocess.check_output(['git','diff','--name-only',reuse['sha'],'HEAD','--','crates','Cargo.toml','Cargo.lock','vendor','.cargo','rust-toolchain.toml'],text=True).strip()
                        if same_native:
                            results=[]
                            for name,run_id in [('apple',reuse['apple_run']),('linux',reuse['linux_run'])]:
                                with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{run_id}/jobs',headers=dict(request.headers)),timeout=30) as response:
                                    jobs=json.load(response)['jobs']
                                if name=='apple':
                                    results.append(any(step['name']=='Validate affected native modules or the complete Apple core' and step['conclusion']=='success' for job in jobs for step in job['steps']))
                                else:
                                    completed={step['name'] for job in jobs if job['name']=='targeted-runtime' for step in job['steps'] if step['conclusion']=='success'}
                                    skip_linux_modules='Check affected modules' in completed and (plan.get('native_scope')=='host-terminals' or 'Exercise original GTK3 and mousepad, including clipboard FD transfers' in completed)
                                    results.append(any(job['name']=='targeted-runtime' and job['conclusion']=='success' for job in jobs))
                            skip_native=results[0];skip_linux_native=results[1]
                            if skip_native or skip_linux_native:print(f'Reusing unchanged native checks: Apple={skip_native}, Linux={skip_linux_native}.')
                    native_scope=''
                    if plan.get('native_scope') == 'host-terminals' and plan.get('native_reuse'):
                        paths=subprocess.check_output(['git','diff','--name-only',plan['native_reuse']['sha'],'HEAD','--','crates','Cargo.toml','Cargo.lock','vendor','.cargo','rust-toolchain.toml'],text=True).splitlines()
                        allowed={'Cargo.toml','Cargo.lock','crates/trames-core/src/process.rs','runtime/crates/trames-vfs/src/pty.rs','crates/trames-ffi/src/lib.rs','crates/trames-ffi/src/session.rs','crates/trames-ffi/src/terminals.rs','crates/trames-ffi/include/trames.h'}
                        if set(paths)<=allowed:
                            native_scope='host-terminals'
                            print('Validate host PTY lifecycle, coordinator signals and original Alpine multiple shells; reuse unrelated CPU, GTK, GPU and networking checks.')
                    if plan.get('native_scope') in {'wayland-input','wayland-clipboard'} and plan.get('native_reuse'):
                        native_paths=subprocess.check_output(['git','diff','--name-only',plan['native_reuse']['sha'],'HEAD','--','crates','Cargo.toml','Cargo.lock','vendor','.cargo','rust-toolchain.toml'],text=True).splitlines()
                        if native_paths and all(path.startswith('runtime/crates/trames-wayland/') for path in native_paths):
                            native_scope=plan['native_scope']
                            print(f'Only compositor code changed; use {native_scope} checks, reusing unchanged native modules and foot.')
                    frontend_reused=False
                    if plan.get('frontend_reuse') and native_scope=='wayland-input':
                        reuse=plan['frontend_reuse']
                        app_paths=['apps/ios','include','guest','scripts/build-ios.sh','scripts/package-ios.py','scripts/finalize-ios-app.py','scripts/prepare-vulkan.py','scripts/prepare-wayland-guest.py']
                        same_frontend=not subprocess.check_output(['git','diff','--name-only',reuse['sha'],'HEAD','--',*app_paths],text=True).strip()
                        if same_frontend:
                            with urllib.request.urlopen(urllib.request.Request(f"https://api.github.com/repos/{repo}/actions/runs/{reuse['run']}/jobs",headers=dict(request.headers)),timeout=30) as response:
                                jobs=json.load(response)['jobs']
                            required={'Parse frontend Swift sources','Link unsigned iPad simulator app','Exercise peer tabs and floating window gestures first','Export simulator test summary','Link unsigned iPad device app','Package unsigned device IPA for sideload signing'}
                            frontend_reused=any(job['name']=='apple-build' and job['conclusion']=='success' and required<={step['name'] for step in job['steps'] if step['conclusion']=='success'} for job in jobs)
                            if frontend_reused:print(f"Reusing unchanged frontend/clipboard acceptance from {reuse['run']}; rebuild the device app with the newly tested compositor only.")
                    repackage_run=''
                    if plan.get('repackage'):
                        reuse=plan['repackage']
                        paths=subprocess.check_output(['git','diff','--name-only',reuse['sha'],'HEAD'],text=True).splitlines()
                        allowed={'.github/ios-validation-plan.json','.github/workflows/ios.yml','scripts/ios-ci-scope.py','scripts/build-ios.sh','scripts/finalize-ios-app.py','README.md','docs/validation/release-1.0.json','docs/validation/release-1.0.md','scripts/publish-ios-release-1.0.py','.github/workflows/release-1.0.yml','tests/integration/multiple-terminals.py','apps/ios/TramesUITests/LaunchTests.swift','apps/ios/TramesTests/WorkspaceInteractionTests.swift'}
                        if any(path not in allowed for path in paths):raise RuntimeError('Repackaging must preserve compiled app sources and build settings')
                        with urllib.request.urlopen(urllib.request.Request(f"https://api.github.com/repos/{repo}/actions/runs/{reuse['run']}/jobs",headers=dict(request.headers)),timeout=30) as response:
                            jobs=json.load(response)['jobs']
                        completed={step['name'] for job in jobs if job['name']=='apple-build' for step in job['steps'] if step['conclusion']=='success'}
                        required={'Parse frontend Swift sources','Link unsigned iPad simulator app','Exercise peer tabs and floating window gestures first','Export simulator test summary','Link unsigned iPad device app'}
                        if not required<=completed:raise RuntimeError('Repackaging requires successful source checks, frontend summary and device compilation')
                        repackage_run=str(reuse['run'])
                        print(f'Repackaging validated unchanged device executables from run {repackage_run}; no recompilation or repeated tests.')
                    print('Applying the source-hash-bound targeted validation plan; unchanged guest inputs are reused.')
                    with open(os.environ['GITHUB_OUTPUT'],'a') as output:
                        output.write('frontend_only=false\ntargeted=true\n')
                        output.write('repackage_run='+repackage_run+'\n')
                        output.write('native_scope='+native_scope+'\n')
                        output.write('frontend_reused='+str(frontend_reused).lower()+'\n')
                        output.write('skip_native='+str(skip_native).lower()+'\n')
                        output.write('skip_linux_native='+str(skip_linux_native).lower()+'\n')
                        output.write('skip_linux_modules='+str(skip_linux_modules).lower()+'\n')
                        output.write(f'baseline_run={baseline}\n')
                        output.write('critical_test='+plan['critical_test']+'\n')
                        output.write('ui_targets='+json.dumps(plan['ui_targets'],separators=(',',':'))+'\n')
                    return
    for run in runs:
        sha = run["head_sha"]
        if subprocess.run(["git", "merge-base", "--is-ancestor", sha, "HEAD"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
            continue
        paths = subprocess.check_output(["git", "diff", "--name-only", sha, "HEAD"], text=True).splitlines()
        if all(frontend_path(path) for path in paths):
            if os.environ.get("TRAMES_NEEDS_GUEST_ARTIFACTS", "true") == "true":
                # Targeted frontend runs do not republish unchanged guest artifacts.
                artifacts_request = urllib.request.Request(
                    f"https://api.github.com/repos/{repo}/actions/runs/{run['id']}/artifacts?per_page=100",
                    headers=dict(request.headers),
                )
                with urllib.request.urlopen(artifacts_request, timeout=30) as response:
                    artifacts = {a["name"] for a in json.load(response)["artifacts"] if not a["expired"]}
                if not {"full-musl-library-and-source", "vulkan-compute-driver-and-native-source"} <= artifacts:
                    continue
            print(f"Reusing runtime and guest validation from successful run {run['id']} ({sha}).")
            with open(os.environ["GITHUB_OUTPUT"], "a") as output:
                output.write(f"frontend_only=true\nbaseline_run={run['id']}\nbaseline_sha={sha}\n")
            return
        # The newest successful ancestor includes all available previous evidence.
        break
    with open(os.environ["GITHUB_OUTPUT"], "a") as output:
        output.write("frontend_only=false\n")


if __name__ == "__main__":
    main()
