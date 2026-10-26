#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""A product runtime pin must participate in source-bound CI reuse decisions."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('scope', root / 'scripts/ios-ci-scope.py')
scope = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scope)
spec = importlib.util.spec_from_file_location('local_ci', root / 'scripts/run-local-ci.py')
local_ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(local_ci)

class RuntimeInputs(unittest.TestCase):
    def test_runtime_is_not_a_frontend_change_and_legacy_plan_cannot_reuse_it(self):
        self.assertFalse(scope.frontend_path('runtime'))
        self.assertFalse(scope.frontend_path('runtime-version.json'))
        original = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            os.chdir(directory)
            try:
                Path('runtime-version.json').write_text(json.dumps({'revision':'current'}))
                plan = Path('plan.json')
                for revision in [None, 'previous', 'current']:
                    plan.write_text(json.dumps({'runtime_revision':revision}))
                    self.assertEqual(scope.paired_runtime_plan(plan), revision == 'current')
            finally:
                os.chdir(original)

    def test_local_ci_hashes_nested_runtime_source_and_version_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            file = root / 'runtime/crates/example/src/lib.rs'
            file.parent.mkdir(parents=True)
            file.write_text('pub const VERSION: u32 = 1;')
            (root / 'runtime-version.json').write_text('{"revision":"one"}')
            before = local_ci.rust_inputs(root)
            file.write_text('pub const VERSION: u32 = 2;')
            after = local_ci.rust_inputs(root)
            self.assertNotEqual(before['runtime/crates/example/src/lib.rs'], after['runtime/crates/example/src/lib.rs'])
            (root / 'runtime-version.json').write_text('{"revision":"two"}')
            self.assertNotEqual(after['runtime-version.json'], local_ci.rust_inputs(root)['runtime-version.json'])

if __name__ == '__main__':
    unittest.main()
