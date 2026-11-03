"""Exercise output ownership, failure cleanup and retained baseline inputs."""
from pathlib import Path
import unittest

from experiment_cleanup import (clean_objects_after_build, record_object_output,
                                temporary_workspace, write_generated_bytes,
                                discard_frontend_base)

ROOT = Path(__file__).resolve().parents[1]


class ExperimentCleanupTest(unittest.TestCase):
    def setUp(self):
        self.workspace = temporary_workspace(ROOT/'build/maintenance-tests', 'experiment-')
        self.root = self.workspace.__enter__()
        self.base = self.root/'baseline'
        self.out = self.root/'experiment'
        self.base.mkdir()
        self.out.mkdir()
        (self.base/'input.o').write_bytes(b'baseline')

    def tearDown(self):
        self.workspace.__exit__(None, None, None)

    def build(self, fail=False, keep_objects=False):
        @clean_objects_after_build
        def run(base, out):
            write_generated_bytes(out/'bank.bin', b'bank')
            record_object_output(['zig','cc','-o',out/'code.o'])
            (out/'code.o').write_bytes(b'object')
            for name in ('game.elf','game.gba','source.c','report.json'):
                (out/name).write_bytes(b'proof')
            if fail:
                raise RuntimeError('compiler failed')
        run(self.base,self.out,keep_objects=keep_objects)

    def test_success_discards_only_owned_intermediates(self):
        (self.out/'unrelated.bin').write_bytes(b'caller input')
        self.build()
        self.assertFalse((self.out/'bank.bin').exists())
        self.assertFalse((self.out/'code.o').exists())
        self.assertEqual((self.base/'input.o').read_bytes(),b'baseline')
        self.assertEqual((self.out/'unrelated.bin').read_bytes(),b'caller input')
        self.assertTrue((self.out/'game.elf').exists())
        self.assertTrue((self.out/'game.gba').exists())
        self.assertTrue((self.out/'report.json').exists())

    def test_failure_also_discards_owned_outputs(self):
        with self.assertRaisesRegex(RuntimeError,'compiler failed'):
            self.build(fail=True)
        self.assertFalse((self.out/'bank.bin').exists())
        self.assertFalse((self.out/'code.o').exists())

    def test_keep_objects_preserves_banks_for_relinking(self):
        self.build(keep_objects=True)
        self.assertTrue((self.out/'bank.bin').exists())
        self.assertTrue((self.out/'code.o').exists())

    def test_standalone_generator_keeps_requested_output(self):
        write_generated_bytes(self.out/'standalone.bin', b'bank')
        self.assertEqual((self.out/'standalone.bin').read_bytes(), b'bank')

    def test_cannot_register_baseline_bank(self):
        @clean_objects_after_build
        def run(base,out):
            write_generated_bytes(base/'input.bin',b'bad')
        with self.assertRaises(ValueError):
            run(self.base,self.out)
        self.assertFalse((self.base/'input.bin').exists())

    def test_cannot_use_nested_baseline(self):
        @clean_objects_after_build
        def run(base,out):
            self.fail('build should not run')
        with self.assertRaises(ValueError):
            run(self.base,self.base/'output')

    def test_consumed_frontend_base_keeps_reports_and_sources(self):
        base=self.out/'base'
        base.mkdir()
        for name in ('code.o','render-columns.bin','dok-diagnostic.elf','dok-diagnostic.gba',
                     'build-report.json','assets-data.S','caller.bin'):
            (base/name).write_bytes(b'proof')
        discard_frontend_base(base)
        self.assertEqual(sorted(p.name for p in base.iterdir()),
                         ['assets-data.S','build-report.json','caller.bin'])


if __name__ == '__main__':
    unittest.main()
