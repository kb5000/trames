#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify unchanged guest/UI inputs; rebuild the changed host runtime for iPad."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile
import urllib.request


def unchanged(source, paths):
    subprocess.run(['git', 'diff', '--exit-code', source, 'HEAD', '--', *paths], check=True)


def verify(config):
    repo = os.environ['GITHUB_REPOSITORY']
    headers = {'Authorization': 'Bearer ' + os.environ['GH_TOKEN'],
               'Accept': 'application/vnd.github+json'}

    def get(path):
        request = urllib.request.Request(f'https://api.github.com/repos/{repo}/{path}', headers=headers)
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)

    def run(record, workflow, job):
        prefix = f"actions/runs/{record['run']}"
        item = get(prefix)
        assert item['head_sha'] == record['sha'] and item['path'] == '.github/workflows/' + workflow
        assert item['head_repository']['full_name'] == repo
        jobs = get(prefix + '/jobs?per_page=100')['jobs']
        completed = next(j for j in jobs if j['name'] == job and j['conclusion'] == 'success')
        return {s['name'] for s in completed['steps'] if s['conclusion'] == 'success'}

    def artifact(record, name):
        items = get(f"actions/runs/{record['run']}/artifacts?per_page=100")['artifacts']
        assert any(a['name'] == name and not a['expired'] for a in items), name

    guest = config['guest']
    unchanged(guest['sha'], ['accelerator/musl', 'accelerator/guest', 'runtime/crates/trames-native/upstream',
                            'scripts/build-musl-hybrid.sh', 'scripts/prepare-musl-hybrid.py', 'LICENSE'])
    run(guest, 'ios.yml', 'guest-musl')
    artifact(guest, 'full-musl-library-and-source')
    artifact(guest, 'vulkan-compute-driver-and-native-source')
    graphics = config['graphics_inputs']
    unchanged(graphics['sha'], ['accelerator', 'include', 'scripts/build-vulkan-guest.sh',
                               'scripts/vulkan-upstream.json', 'scripts/build-zink-egl.py',
                               'scripts/build-xwayland-native.py', 'scripts/build-xwayland-launcher.sh'])
    completed = run(graphics, 'xwayland-dialog.yml', 'linux')
    assert 'Prepare only original signed mousepad dependencies and rebuilt ICD' in completed
    artifact(graphics, 'xwayland-dialog-inputs')
    validation = config['graphics_validation']
    run(validation, 'xwayland-dialog.yml', 'linux')
    run(validation, 'xwayland-dialog.yml', 'metal')
    # Reuse only guest/native-driver algorithms. All changed Rust is rebuilt
    # and tested below; no host runtime acceptance is claimed from these runs.
    unchanged(validation['sha'], ['patches/moltenvk',
                                  'scripts/prepare-vulkan.py', 'scripts/build-vulkan-native-tf.py',
                                  'runtime/crates/trames-vulkan/native'])
    # Pin the COMPLETE source verifier from the starting repository revision.
    # This includes every verification branch, build flag and patch selection;
    # no AST/body filtering or exception for future verifier edits is allowed.
    path = 'scripts/build-moltenvk.py'
    unchanged('52c02331103d145e351fdb7377af80c389e85235', [path])
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == '13a1d3b3984f26176fd241e1402bd2838f3ab3ee7f8e53ca41ee797be1cea2e4'
    frontend = config['frontend']
    unchanged(frontend['sha'], ['apps/ios', 'include', 'scripts/package-ios.py',
                               'scripts/finalize-ios-app.py', ':(exclude)apps/ios/project.yml'])
    old = subprocess.check_output(['git', 'show', frontend['sha'] + ':apps/ios/project.yml'], text=True)
    strip = lambda text: '\n'.join(line for line in text.splitlines()
                                  if not line.strip().startswith(('MARKETING_VERSION:', 'CURRENT_PROJECT_VERSION:')))
    assert strip(old) == strip(Path('apps/ios/project.yml').read_text())
    passed = run(frontend, 'ios.yml', 'apple-build')
    assert {'Exercise peer tabs and floating window gestures first',
            'Export simulator test summary', 'Link unsigned iPad device app'} <= passed
    print('Verified source-identical guest, paired sources and UI; test and rebuild changed Apple runtime, then device IPA.')


def install(config):
    archive = Path('target/worker-ipa-inputs/inputs.tar.gz')
    with tarfile.open(archive) as source:
        manifest = json.load(source.extractfile('inputs.json'))
        assert manifest['source'] == config['graphics_inputs']['sha']
        root = Path('target/vulkan-guest')
        for entry in source:
            path = Path(entry.name)
            if not path.parts or path.parts[0] != 'guest' or not entry.isfile():
                continue
            assert '..' not in path.parts and not path.is_absolute()
            content = source.extractfile(entry).read()
            assert hashlib.sha256(content).hexdigest() == manifest['sha256'][entry.name], entry.name
            target = root.joinpath(*path.parts[1:])
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
    for name, digest in manifest['sha256'].items():
        path = Path(name)
        if path.parts[0] == 'guest':
            assert hashlib.sha256(root.joinpath(*path.parts[1:]).read_bytes()).hexdigest() == digest, name
    print('Installed exact hash-verified guest graphics and sources; no guest recompilation.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    config = json.loads(Path('.github/ios-validation-plan.json').read_text())['worker_handoff']
    (install if args.install else verify)(config)
