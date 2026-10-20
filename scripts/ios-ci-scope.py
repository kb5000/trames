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
    }


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
    return path.startswith(('crates/trames-net/', 'crates/trames-vfs/',
                            'crates/trames-linux/', 'crates/trames-core/', 'docs/')) or path in {
        'crates/trames-cpu/src/extended.rs', 'crates/trames-cpu/src/compatibility.rs',
        'tests/fixtures/seccomp.c', 'tests/fixtures/network-namespaces.c',
        'scripts/build-container-isolation-fixtures.sh',
        'tests/integration/container-isolation.py', 'tests/integration/docker.py',
        '.github/workflows/ios.yml', '.github/workflows/linux.yml',
        'scripts/ios-ci-scope.py',
    }


def container_storage_path(path):
    return path.startswith(('crates/trames-vfs/', 'docs/')) or path in {
        '.github/workflows/ios.yml', '.github/workflows/linux.yml',
        'scripts/ios-ci-scope.py', 'examples/docker/daemon.json', 'README.md',
        'tests/integration/docker-overlay.py', 'tests/integration/docker-overlay.sh',
    }


def container_network_path(path):
    return path.startswith(('crates/trames-net/','crates/trames-vfs/','crates/trames-linux/','docs/')) or path in {
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
             'crates/trames-net/src/nftables_tests.rs','crates/trames-linux/src/network.rs',
             'crates/trames-net/tests/virtual.rs'}
    if any(not p.startswith('docs/') and p not in allowed for p in delta):return False
    # nftables_tests is included only under cfg(test). Socket production code,
    # including ancillary parsing, must be byte-identical to the verified run.
    path='crates/trames-linux/src/network.rs'
    previous=subprocess.check_output(['git','show',sha+':'+path],text=True).split('#[cfg(test)]')[0]
    return previous==Path(path).read_text().split('#[cfg(test)]')[0]


def network_lifetime_source_unchanged(sha):
    delta=subprocess.check_output(['git','diff','--name-only',sha,'HEAD'],text=True).splitlines()
    allowed={'.github/ios-validation-plan.json','.github/workflows/ios.yml',
             '.github/workflows/linux.yml','scripts/ios-ci-scope.py',
             'crates/trames-net/src/lib.rs','crates/trames-net/src/netlink.rs',
             'crates/trames-net/src/routing_tests.rs'}
    if any(not p.startswith('docs/') and p not in allowed for p in delta):return False
    # Exact OnceCell ownership pin + teardown guard, without protocol changes.
    # Any further production change falls back to all affected network checks.
    patch=subprocess.check_output(['git','diff','--binary',sha,'HEAD','--',
        'crates/trames-net/src/lib.rs','crates/trames-net/src/netlink.rs'])
    return hashlib.sha256(patch).hexdigest()=='b72cac90931a82d6d21a4432b241ba8fe265206bc035b378f20e6a625f2cbc2c'


def isolation_retry_source_unchanged(sha):
    delta=subprocess.check_output(['git','diff','--name-only',sha,'HEAD'],text=True).splitlines()
    allowed={'.github/ios-validation-plan.json','.github/workflows/ios.yml',
             '.github/workflows/linux.yml','scripts/ios-ci-scope.py',
             'crates/trames-core/src/process_seccomp.rs','crates/trames-linux/src/network.rs',
             'tests/fixtures/seccomp.c','tests/integration/container-isolation.py'}
    if any(not p.startswith('docs/') and p not in allowed for p in delta):return False
    old=lambda p:subprocess.check_output(['git','show',sha+':'+p],text=True).split('#[cfg(test)]')[0]
    current=lambda p:Path(p).read_text().split('#[cfg(test)]')[0]
    if old('crates/trames-linux/src/network.rs') != current('crates/trames-linux/src/network.rs'):return False
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
        if not unchanged(frontend['sha'], ['apps/ios', 'include', 'scripts/package-ios.py', 'scripts/finalize-ios-app.py', ':(exclude)apps/ios/project.yml']):
            raise RuntimeError('Frontend acceptance source changed')
        strip = lambda text: '\n'.join(l for l in text.splitlines() if not l.strip().startswith(('MARKETING_VERSION:', 'CURRENT_PROJECT_VERSION:')))
        previous = subprocess.check_output(['git', 'show', frontend['sha'] + ':apps/ios/project.yml'], text=True)
        if strip(previous) != strip(Path('apps/ios/project.yml').read_text()):
            raise RuntimeError('Only app version/build metadata may change')
        completed = verified_run(frontend, 'ios.yml', 'apple-build')
        if not {'Link unsigned iPad simulator app', 'Exercise peer tabs and floating window gestures first', 'Export simulator test summary', 'Link unsigned iPad device app'} <= completed:
            raise RuntimeError('Missing passed frontend interactions')
        guest = reuse['guest']
        if not unchanged(guest['sha'], ['accelerator/musl', 'accelerator/guest', 'crates/trames-native/upstream',
                                       'scripts/build-musl-hybrid.sh', 'scripts/prepare-musl-hybrid.py',
                                       'scripts/prepare-vulkan.py', 'scripts/vulkan-upstream.json', 'LICENSE']):
            raise RuntimeError('Original musl/MoltenVK build inputs changed')
        verified_run(guest, 'ios.yml', 'guest-musl')
        names = {a['name'] for a in get(f"actions/runs/{guest['run']}/artifacts")['artifacts'] if not a['expired']}
        if not {'full-musl-library-and-source', 'vulkan-compute-driver-and-native-source'} <= names:
            raise RuntimeError('Paired original source artifacts missing')
        print('Compile current iPad Release app; reuse exact validated graphics bundle, paired musl/native sources and unchanged frontend acceptance. No repeated passed suites.')
        return dict(frontend_only='false', targeted='true', native_scope='device-build', skip_native='true',
                    skip_linux_native='true', skip_linux_modules='true', frontend_reused='true', ui_targets='[]',
                    baseline_run=str(guest['run']), zink_graphics_run=str(zink['run']))

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
        expected = {'crates/trames-memory/src/mmu/mod.rs',
                    'crates/trames-memory/src/mmu/tests.rs', 'crates/trames-core/src/process.rs'}
        if memory_va:
            expected = {'crates/trames-memory/src/mmu/host.rs', 'crates/trames-memory/src/mmu/mod.rs',
                        'crates/trames-memory/src/mmu/tests.rs', 'crates/trames-memory/src/mmu/worker.rs',
                        'crates/trames-vfs/src/pseudo.rs'}
        if startup_mounts:
            expected = {'crates/trames-ffi/src/session.rs'}
        if devpts:
            expected = {'crates/trames-vfs/src/pty.rs', 'crates/trames-vfs/src/pseudo.rs'}
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
    guest_paths = ['guest', 'include', 'accelerator', 'LICENSE', 'crates/trames-native/upstream',
                   'scripts/build-musl-hybrid.sh', 'scripts/prepare-musl-hybrid.py',
                   'scripts/build-vulkan-guest.sh', 'scripts/build-vulkan-fixtures.sh',
                   'scripts/prepare-vulkan.py', 'scripts/vulkan-upstream.json',
                   'scripts/prepare-wayland-guest.py', 'scripts/build-busybox.sh',
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


def main():
    if os.environ.get("TRAMES_FULL_CHECK") == "true":
        with open(os.environ["GITHUB_OUTPUT"], "a") as output:
            output.write("frontend_only=false\n")
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
    if plan_path.exists():
        plan=json.loads(plan_path.read_text())
        base=plan['base_sha']
        if subprocess.run(['git','merge-base','--is-ancestor',base,'HEAD'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0:
            changed=set(subprocess.check_output(['git','diff','--name-only',base,'HEAD'],text=True).splitlines())- {str(plan_path)}
            hashes=plan['changed_file_sha256']
            if changed==set(hashes) and all(Path(path).exists() and hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest for path,digest in hashes.items()):
                if plan.get('native_scope') in {'zink-foundation', 'zink-sync', 'zink-gles', 'zink-wayland'}:
                    allowed = {'accelerator/vulkan/icd.c', 'accelerator/vulkan/queries.inc',
                               'accelerator/vulkan/templates.inc', 'accelerator/vulkan/entrypoints.inc',
                               'crates/trames-distro/src/vulkan.rs', 'tests/fixtures/vulkan-queries.c',
                               'tests/fixtures/zink-egl.c', 'tests/fixtures/vulkan-compute.c',
                               'tests/fixtures/vulkan-texture.c', 'tests/integration/zink-foundation.py',
                               'scripts/prepare-zink-guest.py', 'scripts/build-vulkan-guest.sh',
                               'scripts/ios-ci-scope.py', '.github/workflows/ios.yml', '.github/workflows/linux.yml'}
                    sync = plan.get('native_scope') in {'zink-sync','zink-gles','zink-wayland'}
                    if sync:
                        allowed.update({'accelerator/vulkan/zink.inc', 'accelerator/vulkan/graphics.inc', 'tests/fixtures/vulkan-timeline.c',
                                        'tests/fixtures/vulkan-render.c', 'tests/fixtures/vulkan-resources.c', 'tests/fixtures/vulkan-resources.comp',
                                        'scripts/build-vulkan-fixtures.sh', 'scripts/prepare-zink-native-bundle.py'})
                    if plan.get('native_scope') == 'zink-wayland':
                        allowed.update({'tests/fixtures/zink-wayland.c', 'tests/fixtures/vulkan-copy.c',
                                        'tests/fixtures/zink-metal-pixels.m', 'tests/integration/zink-wayland.py',
                                        'scripts/build-zink-egl.py', 'scripts/zink-egl-requirements.txt', 'patches/mesa/0001-wayland-kopper-native-wsi.patch',
                                        'crates/trames-distro/src/lib.rs', 'crates/trames-ffi/src/session.rs', 'scripts/build-ios.sh'})
                    if any(p not in allowed and not p.startswith('docs/') and not (sync and p.startswith('crates/trames-vulkan/')) for p in changed):
                        raise RuntimeError('Zink foundation plan includes unrelated source changes')
                    path = 'crates/trames-distro/src/vulkan.rs'
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
                    print('Original Mesa Wayland EGL: native WSI/image copies, two-window resize/close and retained Metal texture pixels; reuse unchanged offscreen/CPU/MMU/UI/network checks; no IPA.' if plan.get('native_scope') == 'zink-wayland' else 'Zink synchronization: changed guest ICD and native Vulkan backend, original driver timeline/viewport GPU checks and iOS embedding; CPU/MMU/UI/network unchanged, no IPA.' if sync else 'Guest ICD query/template foundation: original loader/Mesa probe, native GPU compute/texture oracles, bundle upgrade/reopen and Apple/iOS metadata checks. CPU/MMU/native GPU backend/UI unchanged; no IPA or repeated unrelated acceptance.')
                    with open(os.environ['GITHUB_OUTPUT'], 'a') as output:
                        output.write('frontend_only=false\ntargeted=true\nnative_scope=' + plan['native_scope'] + '\n')
                        output.write('skip_native=true\nskip_linux_native=true\nskip_linux_modules=true\n')
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
                    allowed = {'crates/trames-vfs/src/lib.rs', 'crates/trames-vfs/src/pseudo.rs',
                               'crates/trames-vfs/src/mounts.rs',
                               'crates/trames-net/src/lib.rs', 'crates/trames-linux/src/network.rs',
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
                    allowed = {'crates/trames-core/src/process.rs', 'crates/trames-vfs/src/lib.rs',
                               'crates/trames-core/src/workers.rs', 'crates/trames-core/src/worker_syscalls.rs',
                               'crates/trames-core/src/lib.rs', 'crates/trames-core/src/process_seccomp.rs',
                               'crates/trames-core/src/process_service.rs', 'crates/trames-linux/src/state.rs',
                               'tests/fixtures/epoch-services.c', 'tests/fixtures/epoch-services-native.S',
                               'scripts/build-epoch-services-fixture.sh', 'tests/performance/epoch-services.py',
                               'tests/integration/epoch-services.py',
                               'crates/trames-cli/src/main.rs', 'tests/performance/session-sampler.c',
                               'crates/trames-vfs/src/mounts.rs', 'crates/trames-vfs/src/directory.rs',
                               'crates/trames-vfs/src/storage.rs', 'crates/trames-vfs/src/pseudo.rs',
                               'crates/trames-vfs/src/overlay.rs', 'crates/trames-vfs/src/mapped.rs', 'crates/trames-vfs/src/pty.rs',
                               'crates/trames-vfs/src/memfd.rs', 'crates/trames-vfs/src/cgroup.rs',
                               'crates/trames-vfs/src/ipc/native.rs', 'scripts/ios-ci-scope.py',
                               '.github/workflows/ios.yml', '.github/workflows/linux.yml',
                               'tests/performance/session-coremark.py', 'tests/performance/prepare-coremark.py',
                               'tests/performance/session-docker-tty.py'}
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
                                'crates/trames-vfs/src/pseudo.rs', 'crates/trames-vfs/src/overlay.rs'} for p in delta):
                            patch = subprocess.check_output(['git', 'diff', '--binary', reuse['sha'], 'HEAD', '--', 'crates/trames-vfs/src/pseudo.rs'])
                            previous_overlay = subprocess.check_output(['git', 'show', reuse['sha'] + ':crates/trames-vfs/src/overlay.rs'], text=True).split('#[cfg(test)]')[0]
                            current_overlay = Path('crates/trames-vfs/src/overlay.rs').read_text().split('#[cfg(test)]')[0]
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
                        allowed={'Cargo.toml','Cargo.lock','crates/trames-core/src/process.rs','crates/trames-vfs/src/pty.rs','crates/trames-ffi/src/lib.rs','crates/trames-ffi/src/session.rs','crates/trames-ffi/src/terminals.rs','crates/trames-ffi/include/trames.h'}
                        if set(paths)<=allowed:
                            native_scope='host-terminals'
                            print('Validate host PTY lifecycle, coordinator signals and original Alpine multiple shells; reuse unrelated CPU, GTK, GPU and networking checks.')
                    if plan.get('native_scope') in {'wayland-input','wayland-clipboard'} and plan.get('native_reuse'):
                        native_paths=subprocess.check_output(['git','diff','--name-only',plan['native_reuse']['sha'],'HEAD','--','crates','Cargo.toml','Cargo.lock','vendor','.cargo','rust-toolchain.toml'],text=True).splitlines()
                        if native_paths and all(path.startswith('crates/trames-wayland/') for path in native_paths):
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
