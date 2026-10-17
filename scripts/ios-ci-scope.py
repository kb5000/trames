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
