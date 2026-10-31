"""Exercise lossless packing, active-writer exclusion and runner exit status."""
import gzip
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest import mock
from uuid import uuid4

from compress_build_logs import ROOT, compress


class StorageLogsTest(unittest.TestCase):
    def setUp(self):
        self.directory = ROOT/'build/storage-log-tests'/uuid4().hex
        self.directory.mkdir(parents=True)

    def tearDown(self):
        self.assertEqual(self.directory.parent, ROOT/'build/storage-log-tests')
        shutil.rmtree(self.directory)

    def test_roundtrip_and_existing_archive(self):
        path = self.directory/'native.log'
        raw = bytes(range(256))*8192
        path.write_bytes(raw)
        self.assertEqual(compress(path)['status'], 'planned')
        self.assertEqual(path.read_bytes(), raw)
        row = compress(path, True)
        self.assertFalse(path.exists())
        self.assertEqual(gzip.decompress((ROOT/row['archive']).read_bytes()), raw)
        path.write_bytes(b'new run')
        self.assertEqual(compress(path, True)['status'], 'skipped-existing-archive')
        self.assertEqual(path.read_bytes(), b'new run')

    @unittest.skipUnless(os.name == 'nt', 'Windows sharing modes')
    def test_active_writer_is_untouched(self):
        path = self.directory/'active.log'
        with path.open('wb') as stream:
            stream.write(b'active'); stream.flush()
            with self.assertRaises(PermissionError):
                compress(path, True)
            self.assertTrue(path.exists())
        self.assertFalse(path.with_suffix('.log.gz').exists())

    def test_outside_build_rejected(self):
        with self.assertRaises(ValueError):
            compress(ROOT/'research/evidence/example.log', True)

    def test_runner_preserves_native_bytes_stderr_and_failure_code(self):
        child = self.directory/'child.py'
        child.write_text("import os,sys\nos.write(1,b'\\x00\\xff'*1000000)\nos.write(2,b'error\\n')\nsys.exit(7)\n", 'utf8')
        log = self.directory/'child.log'
        command = [sys.executable, '-B', str(ROOT/'ports/nds/tools/performance/run_logged.py'),
                   str(log), str(child)]
        run = subprocess.run(command, capture_output=True)
        self.assertEqual(run.returncode, 7, run.stderr)
        self.assertFalse(log.exists())
        packed = log.with_suffix('.log.gz')
        self.assertEqual(gzip.decompress(packed.read_bytes()), b'\x00\xff'*1000000+b'error\n')
        before = packed.read_bytes()
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(packed.read_bytes(), before)

    def test_object_cleanup_preserves_inputs_and_proofs(self):
        sys.path.insert(0, str(ROOT/'ports/nds/tools'))
        from build_cleanup import discard_objects
        target = ROOT/'build/nds/n23'/('storage-test-'+uuid4().hex)
        objects = target/'obj'; objects.mkdir(parents=True)
        try:
            for name in ('code.o', 'code.d', 'code.su', 'proof.txt'):
                (objects/name).write_bytes(b'proof')
            (target/'game.elf').write_bytes(b'ELF')
            with mock.patch.dict(os.environ, {}, clear=True):
                discard_objects(target, ROOT)
            self.assertEqual(sorted(p.name for p in objects.iterdir()), ['code.su','proof.txt'])
            self.assertEqual((target/'game.elf').read_bytes(), b'ELF')
            with self.assertRaises(ValueError):
                discard_objects(ROOT/'research', ROOT)
        finally:
            self.assertEqual(target.parent, ROOT/'build/nds/n23')
            shutil.rmtree(target)


if __name__ == '__main__':
    unittest.main()
