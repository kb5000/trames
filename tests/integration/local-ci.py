#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise local CI failure/resume and source-bound result reuse."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'local_ci', Path(__file__).resolve().parents[2] / 'scripts/run-local-ci.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class LocalCiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def put(self, path, text):
        file = self.root / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(text)
        return file

    def key(self, script, env=None):
        return m.input_key(self.root, {'run': script}, env or {}, {})[0]

    def test_guest_result_tracks_imports_runner_oracle_and_environment(self):
        self.put('tests/integration/case.py', 'from helper import run\n')
        helper = self.put('tests/integration/helper.py', 'run = 1\n')
        runner = self.put('target/release/trames-run', 'runner-v1')
        oracle = self.put('target/qemu-oracle/bin/qemu-aarch64', 'oracle-v1')
        command = 'python3 tests/integration/case.py'
        baseline = self.key(command)
        self.put('docs/notes.md', 'documentation changes')
        self.assertEqual(baseline, self.key(command))
        for file in (helper, runner, oracle):
            original = file.read_text()
            file.write_text(original + 'changed')
            self.assertNotEqual(baseline, self.key(command), file)
            file.write_text(original)
        self.assertNotEqual(baseline, self.key(command, {'TRAMES_CPUS': '4'}))
        runner.unlink()
        self.assertNotEqual(baseline, self.key(command))

    def test_native_result_tracks_source_and_build_product(self):
        source = self.put('crates/example/src/lib.rs', 'pub fn example() {}')
        command = 'cargo build --workspace --release --locked'
        before = self.key(command)
        product = self.put('target/release/trames-run', 'binary')
        after = self.key(command)
        self.assertNotEqual(before, after)
        source.write_text('pub fn changed() {}')
        self.assertNotEqual(after, self.key(command))
        product.unlink()
        self.assertNotEqual(after, self.key(command))

    def test_compiled_fixtures_and_environment_file_contents_are_inputs(self):
        self.put('tests/integration/case.py', "name = 'example'\n")
        fixture = self.put('tests/fixtures/bin/example', 'guest-v1')
        musl = self.put('target/musl-hybrid/libc.so', 'musl-v1')
        env = {'TRAMES_TEST_MUSL': str(musl)}
        command = 'python3 tests/integration/case.py'
        baseline = self.key(command, env)
        self.put('tests/fixtures/bin/unrelated', 'later build product')
        self.assertEqual(baseline, self.key(command, env))
        fixture.write_text('guest-v2')
        self.assertNotEqual(baseline, self.key(command, env))
        fixture.write_text('guest-v1')
        musl.write_text('musl-v2')
        self.assertNotEqual(baseline, self.key(command, env))

    def test_source_strings_are_not_assumed_to_be_valid_paths(self):
        self.put('tests/integration/case.py', 'code = ' + repr('x' * 290) + '\nzero = "\\0"\n')
        self.key('python3 tests/integration/case.py')

    def test_failed_steps_retry_passed_steps_resume_and_setup_replays(self):
        script = self.put('tests/integration/case.sh',
                          'echo run >> count\nprintf "%s" "$TRAMES_TEST_MODE" > observed\n')
        workflow = {'jobs': {'runtime': {'steps': [
            {'name': 'setup', 'run': 'echo TRAMES_TEST_MODE=ready >> "$GITHUB_ENV"'},
            {'name': 'check', 'run': 'bash tests/integration/case.sh'},
            {'name': 'failure', 'run': 'test -e allowed'},
        ]}}}
        self.put('workflow.json', json.dumps(workflow))

        def run(extra=()):
            args = ['local-ci', '--root', str(self.root), '--workflow', 'workflow.json',
                    '--output', str(self.root / 'evidence'), '--resume', *extra]
            with patch('sys.argv', args), patch.object(m, 'load_workflow', return_value=workflow), \
                    patch.object(m.subprocess, 'check_output', return_value='pinned-toolchain'), \
                    patch.object(m.sys, 'platform', 'darwin'), \
                    patch.object(m.platform, 'platform', return_value='local-test'):
                return m.main()

        self.assertEqual(run(), 1)
        self.assertEqual(run(), 1)
        self.assertEqual((self.root / 'count').read_text(), 'run\n')
        self.assertEqual((self.root / 'observed').read_text(), 'ready')
        self.put('allowed', '')
        self.assertEqual(run(), 0)
        summary = json.loads((self.root / 'evidence/summary.json').read_text())
        self.assertTrue(summary['completed'])
        self.assertTrue(summary['steps'][1]['reused'])
        self.assertEqual(run(['--start', '2']), 0)
        self.assertEqual((self.root / 'observed').read_text(), 'ready')
        summary = json.loads((self.root / 'evidence/summary.json').read_text())
        self.assertTrue(summary['steps'][1]['reused'])
        script.write_text(script.read_text() + '# changed test input\n')
        self.assertEqual(run(), 0)
        self.assertEqual((self.root / 'count').read_text(), 'run\nrun\n')


if __name__ == '__main__':
    unittest.main()
