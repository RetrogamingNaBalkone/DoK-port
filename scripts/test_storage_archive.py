"""Verify lossless archival, selective restore and protected path rejection."""
from pathlib import Path
import unittest
from unittest.mock import patch

import storage_archive as archive
import prune_test_storage as storage
from experiment_cleanup import temporary_workspace


class StorageArchiveTest(unittest.TestCase):
    def setUp(self):
        self.workspace=temporary_workspace(storage.ROOT/'build/maintenance-tests','zip-')
        self.root=self.workspace.__enter__()
        self.patches=[patch.object(m,'ROOT',self.root) for m in (archive,storage)]
        self.patches.append(patch.object(archive,'ARCHIVES',self.root/'research/experiments/storage-archives'))
        for p in self.patches:p.start()

    def tearDown(self):
        for p in reversed(self.patches):p.stop()
        self.workspace.__exit__(None,None,None)

    def test_pack_verify_delete_and_restore(self):
        source=self.root/'research/evidence/nds/n22/run.json'
        source.parent.mkdir(parents=True)
        payload=b'{"history":1234567890}\n'*100
        source.write_bytes(payload)
        destination=archive.ARCHIVES/'test.zip'
        result=archive.pack(destination,[source],apply=True)
        self.assertFalse(source.exists())
        self.assertGreater(result['saved_bytes'],0)
        self.assertTrue(archive.restore_path('research/evidence/nds/n22/run.json'))
        self.assertEqual(source.read_bytes(),payload)
        self.assertFalse(archive.restore_path('research/evidence/nds/n22/run.json'))

    def test_originals_are_not_targets(self):
        with self.assertRaises(ValueError):
            archive.target('originals/pkr/RUS/PKR.EXE')

    def test_existing_output_is_not_overwritten(self):
        path=self.root/'build/nds/test/report.json'
        path.parent.mkdir(parents=True)
        path.write_bytes(b'current')
        self.assertFalse(archive.restore_path('build/nds/test/report.json'))
        self.assertEqual(path.read_bytes(),b'current')

    def test_changed_original_aborts_before_removal(self):
        source=self.root/'research/evidence/nds/n22/report.json'
        source.parent.mkdir(parents=True)
        source.write_bytes(b'initial result')
        real_sha=archive.sha;calls=0
        def changed(path):
            nonlocal calls
            if path==source:
                calls+=1
                if calls==2:source.write_bytes(b'new result')
            return real_sha(path)
        with patch.object(archive,'sha',side_effect=changed):
            with self.assertRaises(ValueError):
                archive.pack(archive.ARCHIVES/'changed.zip',[source],apply=True)
        self.assertEqual(source.read_bytes(),b'new result')
        self.assertFalse((archive.ARCHIVES/'changed.zip').exists())


if __name__=='__main__':unittest.main()
