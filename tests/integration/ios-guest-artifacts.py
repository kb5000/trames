#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regress the two real upload-artifact layouts and reject mixed/corrupt inputs."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('guest_artifacts', Path(__file__).resolve().parents[2] / 'scripts/prepare-ios-guest-artifacts.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class GraphicsArtifactTests(unittest.TestCase):
    def fixture(self, nested):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        output = root / 'vulkan-guest' if nested else root
        output.mkdir(exist_ok=True)
        payload = b'original fixture bytes'
        (output / 'libtrames_vulkan_icd.so').write_bytes(payload)
        (output / 'manifest.json').write_text(json.dumps({'icd_sha256': hashlib.sha256(payload).hexdigest()}))
        (output / 'GPL-3.0.txt').write_text('fixture license')
        return root, output

    def test_flat_producer_is_unchanged(self):
        root, output = self.fixture(False)
        before = {p.name: p.read_bytes() for p in output.iterdir()}
        module.prepare(root)
        self.assertEqual(before, {p.name: p.read_bytes() for p in root.iterdir()})

    def test_multi_directory_producer_retains_paired_sources(self):
        root, output = self.fixture(True)
        # The real upload has both names. On default macOS filesystems the
        # root source directory collides with the nested executable by case.
        (output / 'Xwayland-native').write_bytes(b'original executable fixture')
        paired = root / 'xwayland-native/xwayland-native-source.tar.gz'
        paired.parent.mkdir()
        paired.write_bytes(b'paired original source fixture')
        module.prepare(root)
        self.assertEqual((root / paired.name).read_bytes(), b'paired original source fixture')
        self.assertEqual((root / 'Xwayland-native').read_bytes(), b'original executable fixture')
        self.assertFalse((root / 'vulkan-guest').exists())
        self.assertTrue(all((root / name).is_file() for name in module.REQUIRED))

    def test_mixed_layout_and_corrupt_binary_are_rejected(self):
        root, output = self.fixture(True)
        (root / 'manifest.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            module.prepare(root)
        (root / 'manifest.json').unlink()
        (output / 'libtrames_vulkan_icd.so').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            module.prepare(root)
        self.assertTrue((output / 'manifest.json').is_file())

    def test_destination_conflict_does_not_partially_move_payload(self):
        root, output = self.fixture(True)
        (output / 'acceptance').mkdir()
        (root / 'acceptance').mkdir()
        before = sorted(str(p.relative_to(root)) for p in root.rglob('*'))
        with self.assertRaisesRegex(ValueError, 'destination already exists: acceptance'):
            module.prepare(root)
        self.assertEqual(before, sorted(str(p.relative_to(root)) for p in root.rglob('*')))

    def test_unexpected_source_directory_content_is_retained_on_rejection(self):
        root, output = self.fixture(True)
        folder = root / 'xwayland-native'
        folder.mkdir()
        (folder / 'xwayland-native-source.tar.gz').write_bytes(b'paired source')
        (folder / 'unexpected').write_bytes(b'preserve')
        with self.assertRaisesRegex(ValueError, 'Ambiguous paired'):
            module.prepare(root)
        self.assertEqual((folder / 'unexpected').read_bytes(), b'preserve')
        self.assertTrue((output / 'manifest.json').is_file())

    def test_symlink_payload_is_rejected(self):
        root, output = self.fixture(True)
        payload = output / 'libtrames_vulkan_icd.so'
        target = root / 'outside'
        payload.rename(target)
        payload.symlink_to(target)
        with self.assertRaisesRegex(ValueError, 'Symlink'):
            module.prepare(root)


if __name__ == '__main__':
    unittest.main()
