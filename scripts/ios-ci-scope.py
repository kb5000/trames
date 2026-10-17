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
                    skip_native=False
                    if plan.get('native_reuse'):
                        reuse=plan['native_reuse'];same_native=not subprocess.check_output(['git','diff','--name-only',reuse['sha'],'HEAD','--','crates','Cargo.toml','Cargo.lock','vendor','.cargo','rust-toolchain.toml'],text=True).strip()
                        if same_native:
                            results=[]
                            for name,run_id in [('apple',reuse['apple_run']),('linux',reuse['linux_run'])]:
                                with urllib.request.urlopen(urllib.request.Request(f'https://api.github.com/repos/{repo}/actions/runs/{run_id}/jobs',headers=dict(request.headers)),timeout=30) as response:
                                    jobs=json.load(response)['jobs']
                                if name=='apple':
                                    results.append(any(step['name']=='Validate affected native modules or the complete Apple core' and step['conclusion']=='success' for job in jobs for step in job['steps']))
                                else:results.append(any(job['name']=='targeted-runtime' and job['conclusion']=='success' for job in jobs))
                            skip_native=all(results)
                            if skip_native:print('Reusing passed Apple and Linux native checks; Rust sources and dependencies are unchanged.')
                    print('Applying the source-hash-bound targeted validation plan; unchanged guest inputs are reused.')
                    with open(os.environ['GITHUB_OUTPUT'],'a') as output:
                        output.write('frontend_only=false\ntargeted=true\n')
                        output.write('skip_native='+str(skip_native).lower()+'\n')
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
