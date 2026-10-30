"""Check the native deletion driver's boundaries before using it on real data."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import unittest
from unittest.mock import patch
from uuid import uuid4

import prune_test_storage as storage


class PruneTests(unittest.TestCase):
    def setUp(self):
        self.directory = storage.ROOT / 'build/storage-cleanup-tests' / ('run-' + uuid4().hex)
        storage.local(self.directory)
        self.directory.mkdir(parents=True)
        self.first = self.directory / 'zig-cache/first.o'
        self.second = self.directory / 'zig-cache/second.o'
        self.first.parent.mkdir()
        self.first.write_bytes(b'first')
        self.second.write_bytes(b'second')
        self.manifest = self.directory / 'plan.json'

    def tearDown(self):
        storage.local(self.directory)
        self.assertEqual(self.directory.parent, storage.ROOT / 'build/storage-cleanup-tests')
        shutil.rmtree(self.directory)

    def record(self, path):
        return dict(path=storage.relative(path), bytes=path.stat().st_size, sha256=storage.sha(path))

    def run_driver(self, records, apply=False):
        storage.write(self.manifest, dict(schema=1, workspace=str(storage.ROOT),
            delete=records, bytes=sum(p['bytes'] for p in records)))
        command = ['powershell', '-NoProfile', '-File',
            str(storage.ROOT / 'scripts/prune-test-storage.ps1'),
            '-Manifest', storage.relative(self.manifest)]
        if apply:
            command.append('-Apply')
        return subprocess.run(command, cwd=storage.ROOT, capture_output=True, text=True)

    def test_dry_run_then_apply_only_manifest_file(self):
        records = [self.record(self.first)]
        result = self.run_driver(records)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(self.first.is_file())
        result = self.run_driver(records, apply=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(self.first.exists())
        self.assertEqual(self.second.read_bytes(), b'second')

    def test_changed_bytes_abort_before_any_deletion(self):
        records = [self.record(self.first), self.record(self.second)]
        records[1]['sha256'] = '0' * 64
        result = self.run_driver(records, apply=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.first.is_file())
        self.assertTrue(self.second.is_file())

    def test_unapproved_output_abort_before_any_deletion(self):
        ordinary = self.directory / 'original-proof.bin'
        ordinary.write_bytes(b'keep')
        result = self.run_driver([self.record(self.first), self.record(ordinary)], apply=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.first.is_file())
        self.assertTrue(ordinary.is_file())

    def test_traversal_rejected(self):
        record = self.record(self.first)
        record['path'] = 'build/../' + record['path']
        result = self.run_driver([record], apply=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(self.first.is_file())

    def test_python_boundary_and_reparse_rejected(self):
        with self.assertRaises(ValueError):
            storage.local(storage.ROOT.parent / 'outside')
        with patch.object(Path, 'is_junction', return_value=True):
            with self.assertRaises(ValueError):
                storage.local(self.first)


if __name__ == '__main__':
    unittest.main()
