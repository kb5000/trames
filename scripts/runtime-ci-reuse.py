#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reuse source-bound passed checks; execute failed checks and rebuild all inputs."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.request
import urllib.parse

PLAN = Path('.github/runtime-ci-reuse.json')
HOOK = '    defaults:\n      run:\n        shell: python3 scripts/runtime-ci-reuse.py {0}\n'
CHECKOUT = '        with:\n          fetch-depth: 0\n          ref: ${{ github.event.pull_request.head.sha || github.sha }}\n'
UNIT = '      - name: Validate source-bound runtime reuse\n        shell: bash\n        run: python3 tests/integration/runtime-ci-reuse.py\n'
ALLOWED = {
    str(PLAN), '.github/workflows/linux.yml', '.github/workflows/ios.yml',
    'scripts/runtime-ci-reuse.py', 'scripts/ios-ci-scope.py',
    'tests/integration/runtime-ci-reuse.py', 'tests/integration/allocator.py',
    'apps/ios/TramesTests/LocalNetworkTests.swift',
    'tests/integration/linux-compat.py', 'tests/integration/embedding-network.py',
}
REQUIRED = {
    'Run cargo test --workspace --all-features --locked',
    'Check region and task signal races with fully instrumented Rust standard library',
    'Test the pinned common runtime workspace',
    'Check original-source native allocator C with address and undefined behavior sanitizers',
    'Compile core and both network backends for iOS (no SDK link or device test)',
    'Execute ARM64 compute, memory and native worker tests under the independent oracle',
}


def git(*args):
    return subprocess.check_output(['git', *args], text=True)


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        redirected = super().redirect_request(request, response, code, message, headers, url)
        if redirected and urllib.parse.urlsplit(request.full_url).netloc != urllib.parse.urlsplit(url).netloc:
            redirected.remove_header('Authorization')
        return redirected


def get(path, raw=False):
    repo = os.environ['GITHUB_REPOSITORY']
    headers = {'Accept': 'application/vnd.github+json'}
    if os.environ.get('GH_TOKEN'):
        headers['Authorization'] = 'Bearer ' + os.environ['GH_TOKEN']
    request = urllib.request.Request('https://api.github.com/repos/' + repo + '/' + path, headers=headers)
    with urllib.request.build_opener(SafeRedirect).open(request, timeout=45) as response:
        data = response.read()
    return data.decode('utf-8-sig') if raw else json.loads(data)


def unchanged_inputs(config, changed, read_bytes, old_workflow, current_workflow):
    if any(p not in ALLOWED and not p.startswith('docs/') for p in changed):
        return False
    expected = config['changed_file_sha256']
    required = set(changed) - {str(PLAN)} - {p for p in changed if p.startswith('docs/')}
    if required != set(expected):
        return False
    if any(hashlib.sha256(read_bytes(p)).hexdigest() != expected[p] for p in required):
        return False
    # The only Linux workflow change is its shell wrapper. All original run
    # bodies, actions, environment, flags and ordering remain byte-identical.
    return (current_workflow.count(HOOK) == current_workflow.count(UNIT) == 1
            and current_workflow.count(CHECKOUT) == old_workflow.count(CHECKOUT) + 1
            and current_workflow.replace(HOOK, '').replace(CHECKOUT + UNIT, '', 1) == old_workflow)


def verified_config():
    if os.environ.get("TRAMES_FULL_CHECK") == "true" or os.environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch":
        return None
    if not PLAN.exists():
        return None
    config = json.loads(PLAN.read_text())
    base = config['source_sha']
    ancestry = subprocess.run(['git', 'merge-base', '--is-ancestor', base, 'HEAD'],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # Old evidence can reference rewritten, missing or unrelated history.
    # It is a cache miss; current validation must run in full.
    if ancestry.returncode != 0:
        return None
    changed = git('diff', '--name-only', base, 'HEAD').splitlines()
    if not unchanged_inputs(config, changed, lambda p: Path(p).read_bytes(),
                            git('show', base + ':.github/workflows/linux.yml'),
                            Path('.github/workflows/linux.yml').read_text()):
        return None
    return config


def runtime_steps(workflow):
    """Read the repository's literal runtime run blocks; unsupported forms miss.

    This is deliberately not a general YAML evaluator. Current flags/env/actions
    are separately required to match the original workflow byte-for-byte.
    """
    body = workflow.split('\n  runtime:\n', 1)[1]
    body = re.split(r'\n  [A-Za-z0-9_-]+:\n', body, maxsplit=1)[0]
    result = []
    for block in re.split(r'(?m)^      - ', body)[1:]:
        lines = block.splitlines()
        fields = [lines[0]] + [line[8:] if line.startswith('        ') else line for line in lines[1:]]
        name = next((line[6:].strip('"\'') for line in fields if line.startswith('name: ')), None)
        for i, line in enumerate(fields):
            if not line.startswith('run: '):
                continue
            value = line[5:]
            if value == '|':
                code = []
                for following in fields[i + 1:]:
                    if following and not following.startswith('  '):
                        break
                    code.append(following[2:] if following else '')
                value = '\n'.join(code)
            if value.strip():
                result.append((name or 'Run ' + value.splitlines()[0], value.strip()))
            break
    return result


def selected_script(script, steps, job, changed_tests):
    if any(path in script for path in changed_tests):
        return script, False
    names = [name for name, code in steps if code == script.strip()]
    if not names:
        return script, False
    conclusions = [s['conclusion'] for s in job['steps'] if s['name'] in names]
    if not conclusions or any(c != 'success' for c in conclusions):
        return script, False
    kept, removed = [], []
    for line in script.splitlines():
        text = line.strip()
        if not text or text.startswith('#'):
            kept.append(line)
        elif any(token in text for token in (';', '&&', '||', '|', '`', '$(', '<<', '>')):
            return script, False
        elif text.startswith(('cargo test ', 'cargo clippy ', 'cargo fmt ',
                              'python3 tests/differential/', 'python3 tests/integration/')) or text == 'bash scripts/check-native-allocator.sh':
            removed.append(line)
        elif text.startswith(('cargo build ', 'bash scripts/build-', 'bash scripts/prepare-')):
            kept.append(line)
        else:
            return script, False
    return '\n'.join(kept) + '\n', bool(removed)


def reference(config, kind):
    record = config[kind]
    run = get('actions/runs/' + str(record['run']))
    source = record.get('source_sha', config['source_sha'])
    assert run['head_sha'] == source
    # A later partial run may add passed checks after the original baseline.
    # Its production/guest/build inputs must still be identical to that baseline.
    changed = git('diff', '--name-only', config['source_sha'], source).splitlines()
    assert all(p in ALLOWED or p.startswith('docs/') for p in changed)
    assert run['head_repository']['full_name'] == os.environ['GITHUB_REPOSITORY']
    assert run['path'] == '.github/workflows/' + ('linux.yml' if kind == 'linux' else 'ios.yml')
    job = get('actions/jobs/' + str(record['job']))
    assert job['run_id'] == record['run'] and job['status'] == 'completed'
    return job


def linux_reference(config):
    cache = Path('target/runtime-reuse/reference.json')
    if cache.exists():
        value = json.loads(cache.read_text())
        if value['config_sha256'] == hashlib.sha256(PLAN.read_bytes()).hexdigest():
            return value['job']
    job = reference(config, 'linux')
    assert job['name'] == 'runtime'
    passed = {s['name'] for s in job['steps'] if s['conclusion'] == 'success'}
    assert REQUIRED <= passed, 'Reference lacks successful unchanged native checks'
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_text(json.dumps({'config_sha256': hashlib.sha256(PLAN.read_bytes()).hexdigest(), 'job': job}))
    return job


def test_summaries(log):
    lines = [re.sub(r'^\ufeff?\d{4}-\d\d-\d\dT\S+Z ', '', line) for line in log.splitlines()]
    summaries = []
    for i, line in enumerate(lines):
        if line != '{':
            continue
        end = next((j for j in range(i + 1, len(lines)) if lines[j] == '}'), None)
        if end is None:
            continue
        try:
            value = json.loads('\n'.join(lines[i:end + 1]))
        except ValueError:
            continue
        if isinstance(value, dict) and 'totalTestCount' in value:
            summaries.append(value)
    return summaries


def apple_evidence(job, summaries):
    passed = {s['name'] for s in job['steps'] if s['conclusion'] == 'success'}
    required = {'Parse frontend Swift sources', 'Validate affected native modules or the complete Apple core',
                'Link unsigned iPad simulator app', 'Exercise peer tabs and floating window gestures first'}
    if job['name'] != 'apple-build' or not required <= passed:
        return False
    critical = any(s['failedTests'] == 0 and s['passedTests'] == s['totalTestCount'] == 1 for s in summaries)
    expected = 'LocalNetworkTests/testRealAlpineDirectDNSAndHTTPSOnAppleRuntime()'
    suite = any(s['failedTests'] == 1 and s['passedTests'] == 69 and s['totalTestCount'] == 71
                and len(s['testFailures']) == 1
                and s['testFailures'][0]['testIdentifierString'] == expected for s in summaries)
    return critical and suite


def apple_outputs():
    if os.environ.get('TRAMES_VALIDATION_WORKFLOW', 'ios.yml') != 'ios.yml':
        return None
    config = verified_config()
    if config is None:
        return None
    job = reference(config, 'apple')
    log = get('actions/jobs/' + str(config['apple']['job']) + '/logs', raw=True)
    if not apple_evidence(job, test_summaries(log)):
        raise RuntimeError('Apple retry requires the exact single failed network case and passed unchanged checks')
    print('Reuse source-identical native checks, 69 passed simulator cases and workspace gesture; rerun only the failed LocalNetworkTests, then build/package the device app.')
    return dict(frontend_only='false', targeted='true', native_scope='release-ci-retry',
                skip_native='true', skip_linux_native='true', skip_linux_modules='true',
                frontend_reused='false', critical_reused='true',
                baseline_run=str(config['apple']['run']),
                ui_targets='["TramesTests/LocalNetworkTests"]')


def main(path):
    script = Path(path).read_text()
    selected, reused = script, False
    try:
        config = verified_config()
        if config:
            job = linux_reference(config)
            steps = runtime_steps(git('show', config['source_sha'] + ':.github/workflows/linux.yml'))
            reference_sha = config['linux'].get('source_sha', config['source_sha'])
            changed_tests = [p for p in git('diff', '--name-only', reference_sha, 'HEAD').splitlines()
                             if p.startswith('tests/')]
            selected, reused = selected_script(script, steps, job, changed_tests)
            if reused:
                print('Reuse passed unchanged check from Linux run', config['linux']['run'],
                      'source', config['source_sha'], '; rebuild commands remain active.', flush=True)
    except Exception as error:
        # Missing evidence or any unexpected source/configuration change means
        # ordinary full execution. Never convert an unavailable check to a pass.
        print('Check reuse unavailable; executing original step:', type(error).__name__, flush=True)
    if not reused:
        selected = script
    result = subprocess.run(['bash', '--noprofile', '--norc', '-e', '-o', 'pipefail'], input=selected, text=True)
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main(sys.argv[1])
