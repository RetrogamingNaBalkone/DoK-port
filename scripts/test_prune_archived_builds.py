"""Verify archive byte checks, restore and generated-bank boundaries."""
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import prune_archived_builds as prune
import prune_test_storage as storage
from experiment_cleanup import temporary_workspace


class ArchivedBuildsTest(unittest.TestCase):
    def setUp(self):
        self.workspace=temporary_workspace(storage.ROOT/'build/maintenance-tests','archive-')
        self.root=self.workspace.__enter__()
        self.patches=[patch.object(module,'ROOT',self.root) for module in (prune,storage)]
        for item in self.patches:
            item.start()
        self.git=patch.object(prune.subprocess,'check_output',return_value=b'')
        self.git_mock=self.git.start()
        self.output=self.root/'build/nds/n22/old/report.json'
        self.archive=self.root/'research/evidence/nds/old/report.json'
        for path in (self.output,self.archive):
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(b'{"proof":true}')
        self.manifest=self.root/'research/evidence/maintenance/plan.json'

    def tearDown(self):
        self.git.stop()
        for item in reversed(self.patches):
            item.stop()
        self.workspace.__exit__(None,None,None)

    def test_remove_and_restore_exact_archive_copy(self):
        prune.plan(self.manifest,0)
        report=json.loads(self.manifest.read_text('utf8'))
        self.assertEqual(report['files'],1)
        prune.apply(self.manifest)
        self.assertFalse(self.output.exists())
        prune.restore(self.manifest)
        self.assertEqual(self.output.read_bytes(),self.archive.read_bytes())

    def test_changed_archive_prevents_any_deletion(self):
        prune.plan(self.manifest,0)
        self.archive.write_bytes(b'changed archive')
        with self.assertRaises(ValueError):
            prune.apply(self.manifest)
        self.assertTrue(self.output.exists())

    def test_changed_build_prevents_deletion(self):
        prune.plan(self.manifest,0)
        self.output.write_bytes(b'new result')
        with self.assertRaises(ValueError):
            prune.apply(self.manifest)
        self.assertEqual(self.output.read_bytes(),b'new result')

    def test_current_nds_and_tracked_files_are_protected(self):
        active=self.root/'build/nds/n23/current/report.json'
        active.parent.mkdir(parents=True)
        active.write_bytes(self.archive.read_bytes())
        self.git_mock.return_value=b'build/nds/n22/old/report.json\0'
        prune.plan(self.manifest,0)
        self.assertEqual(json.loads(self.manifest.read_text('utf8'))['files'],0)

    def test_only_assembly_referenced_banks_are_disposable(self):
        directory=self.root/'build/gba/old/hardware'
        directory.mkdir(parents=True)
        bank=directory/'prelit.bin'
        bank.write_bytes(b'generated')
        (directory/'caller-input.bin').write_bytes(b'keep')
        (directory/'prototype-build.json').write_text('{"variants":{"test":{}}}',encoding='utf8')
        (directory/'tables.S').write_text(f'.incbin "{bank.as_posix()}"\n',encoding='utf8')
        prune.plan(self.manifest,0)
        prune.apply(self.manifest)
        self.assertFalse(bank.exists())
        self.assertTrue((directory/'caller-input.bin').exists())

    def test_manifest_cannot_delete_outside_build(self):
        row=dict(path='originals/sample.EXE',reason='archived-copy',archive='research/evidence/nds/old/report.json',
                 bytes=self.archive.stat().st_size,sha256=storage.sha(self.archive))
        with self.assertRaises(ValueError):
            prune.validate(row)


if __name__ == '__main__':
    unittest.main()
