"""Compiler-output policy keeps source data and current checkpoints."""
import unittest
import prune_superseded_builds as prune


class SupersededBuildsTest(unittest.TestCase):
    def test_required_baselines_and_current_targets_are_retained(self):
        paths=['build/gba/select-menu-fix/assets-data.o',
               'build/gba/releases/gba-beta2/base/dok-diagnostic.elf',
               'build/gba/g22/base-control/base/renderer.o',
               'build/nds/n23/crashdiag22-ru/dok_nds_gameplay.elf',
               'build/nds/n23/audio21-ru/dok_nds_gameplay.nds']
        for path in paths:
            with self.subTest(path=path):
                self.assertFalse(prune.candidate(prune.ROOT/path))

    def test_prepared_inputs_are_retained(self):
        paths=['build/nds/assets/dok-assets.ndspak','build/nds/n20/scene.dat',
               'build/nds/n21/generated/frontend_hash.inc',
               'build/nds/n07/generated/render_trig.inc']
        for path in paths:
            with self.subTest(path=path):
                self.assertFalse(prune.candidate(prune.ROOT/path))

    def test_old_compiler_outputs_are_candidates(self):
        self.assertTrue(prune.candidate(prune.ROOT/'build/gba/old/main.o'))
        self.assertTrue(prune.candidate(prune.ROOT/'build/nds/n23/old/nitrofs/dok/assets.ndp'))
        self.assertFalse(prune.candidate(prune.ROOT/'research/evidence/nds/old/game.elf'))


if __name__=='__main__':unittest.main()
