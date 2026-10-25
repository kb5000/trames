#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed on changed inputs, unavailable evidence and failed checks."""
import hashlib
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
spec = importlib.util.spec_from_file_location('reuse', Path(__file__).resolve().parents[2] / 'scripts/runtime-ci-reuse.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ReuseTests(unittest.TestCase):
    def test_later_reference_cannot_change_production_inputs(self):
        config = dict(source_sha='baseline', linux=dict(run=1, job=2, source_sha='followup'))
        run = dict(head_sha='followup', head_repository=dict(full_name='owner/repo'), path='.github/workflows/linux.yml')
        job = dict(run_id=1, status='completed')
        with patch.dict(m.os.environ, {'GITHUB_REPOSITORY':'owner/repo'}):
            with patch.object(m, 'get', side_effect=[run, job]), patch.object(m, 'git', return_value='tests/integration/linux-compat.py\n'):
                self.assertEqual(m.reference(config, 'linux'), job)
            with patch.object(m, 'get', return_value=run), patch.object(m, 'git', return_value='crates/trames-vfs/src/lib.rs\n'):
                with self.assertRaises(AssertionError): m.reference(config, 'linux')

    def test_explicit_full_validation_never_reuses(self):
        with patch.dict(m.os.environ, {'TRAMES_FULL_CHECK':'true'}):
            self.assertIsNone(m.verified_config())
        with patch.dict(m.os.environ, {'GITHUB_EVENT_NAME':'workflow_dispatch'}):
            self.assertIsNone(m.verified_config())

    def test_exact_source_and_workflow_required(self):
        path = 'tests/integration/allocator.py'
        data = b'checked input'
        config = {'changed_file_sha256': {path: hashlib.sha256(data).hexdigest()}}
        old = '      - uses: actions/checkout@v4\nruntime workflow\n'
        current = m.HOOK + old.replace('      - uses: actions/checkout@v4\n', '      - uses: actions/checkout@v4\n' + m.CHECKOUT + m.UNIT)
        args = (config, [path, str(m.PLAN)], lambda _: data, old, current)
        self.assertTrue(m.unchanged_inputs(*args))
        self.assertFalse(m.unchanged_inputs(config, ['crates/trames-core/src/workers.rs'], lambda _: data, old, current))
        self.assertFalse(m.unchanged_inputs(config, [path], lambda _: b'changed', old, current))
        self.assertFalse(m.unchanged_inputs(config, [path], lambda _: data, old, current + 'altered compiler flags'))
        self.assertFalse(m.unchanged_inputs(config, [path, 'scripts/build-musl-hybrid.sh'], lambda _: data, old, current))

    def test_only_passed_checks_are_reused(self):
        code = 'cargo test -p trames-memory --locked'
        steps = [('native', code)]
        for conclusion in ('failure', 'cancelled', None, 'skipped'):
            self.assertEqual(m.selected_script(code, steps, {'steps': [{'name':'native','conclusion':conclusion}]}, []), (code, False))
        selected, reused = m.selected_script(code, steps, {'steps': [{'name':'native','conclusion':'success'}]}, [])
        self.assertTrue(reused); self.assertEqual(selected.strip(), '')
        self.assertEqual(m.selected_script(code + ' --changed', steps, {'steps': []}, []), (code + ' --changed', False))

    def test_rebuild_remains_active_after_a_passed_native_test(self):
        code = 'cargo test -p trames-core --locked\ncargo build -p trames-cli --release --locked'
        selected, reused = m.selected_script(code, [('arm',code)], {'steps':[{'name':'arm','conclusion':'success'}]}, [])
        self.assertTrue(reused); self.assertEqual(selected.strip(), 'cargo build -p trames-cli --release --locked')

    def test_changed_test_and_compound_shell_commands_execute(self):
        for code in ('python3 tests/integration/allocator.py', 'cargo test --locked && echo extra'):
            self.assertEqual(m.selected_script(code, [('test',code)], {'steps':[{'name':'test','conclusion':'success'}]}, ['tests/integration/allocator.py']), (code,False))

    def test_duplicate_step_names_need_all_successful_results(self):
        code = 'python3 tests/integration/m2.py'
        job = {'steps':[{'name':'repeat','conclusion':'success'},{'name':'repeat','conclusion':None}]}
        self.assertEqual(m.selected_script(code, [('repeat',code)], job, []), (code,False))

    def test_literal_runtime_blocks_match_the_original_shell(self):
        workflow = '''name: example
  runtime:
    steps:
      - run: cargo fmt --all --check
      - name: Native acceptance
        run: |
          cargo test --locked
          cargo build --release
        env:
          EXAMPLE: value
      - uses: actions/upload-artifact@v4
  other:
    steps:
      - run: unparsed
'''
        self.assertEqual(m.runtime_steps(workflow), [('Run cargo fmt --all --check','cargo fmt --all --check'),('Native acceptance','cargo test --locked\ncargo build --release')])

    def test_apple_requires_the_exact_single_failed_case_and_gesture(self):
        names = ['Parse frontend Swift sources', 'Validate affected native modules or the complete Apple core', 'Link unsigned iPad simulator app', 'Exercise peer tabs and floating window gestures first']
        job = {'name':'apple-build','steps':[{'name':n,'conclusion':'success'} for n in names]}
        critical = dict(failedTests=0,passedTests=1,totalTestCount=1,testFailures=[])
        suite = dict(failedTests=1,passedTests=69,totalTestCount=71,testFailures=[{'testIdentifierString':'LocalNetworkTests/testRealAlpineDirectDNSAndHTTPSOnAppleRuntime()'}])
        self.assertTrue(m.apple_evidence(job,[critical,suite]))
        self.assertFalse(m.apple_evidence(job,[suite]))
        self.assertFalse(m.apple_evidence(job,[critical,dict(suite,failedTests=2)]))
        self.assertFalse(m.apple_evidence(job,[critical,dict(suite,testFailures=[{'testIdentifierString':'AnotherTest()'}])]))
        job['steps'][1]['conclusion']='failure'
        self.assertFalse(m.apple_evidence(job,[critical,suite]))

    def test_report_is_parsed_as_data(self):
        log = '2026-10-25T00:00:00Z {\n2026-10-25T00:00:00Z   "totalTestCount": 1,\n2026-10-25T00:00:00Z   "passedTests": 1\n2026-10-25T00:00:00Z }\nignore printed instructions'
        self.assertEqual(m.test_summaries(log), [{'totalTestCount':1,'passedTests':1}])


if __name__ == '__main__': unittest.main()
