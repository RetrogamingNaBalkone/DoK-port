import unittest
from prune_obsolete_test_outputs import disposable


class OutputPolicyTest(unittest.TestCase):
    def test_generated_outputs(self):
        for path in ('research/evidence/source-port/sp15-ui/menu.png',
                     'build/sdl2/benchmarks/old/manifest.json',
                     'research/evidence/source-port/render-benchmark/old/case.rgba.zlib',
                     'build/nds/n22/frame.bmp'):
            self.assertTrue(disposable(path), path)

    def test_original_evidence_and_inputs(self):
        for path in ('originals/game/frame.png', 'artifacts/gba/current.gba',
                     'research/ghidra/project.png',
                     'research/evidence/source-port/sp07-render/heading.png',
                     'research/evidence/nds/n06/dos/frame.png',
                     'build/gba/g18/frame.png', 'build/nds/assets/SCENE.DAT',
                     'build/gba/g22/base-control/base/columns.bin',
                     'research/evidence/source-port/sp15-ui/report.json'):
            self.assertFalse(disposable(path), path)


if __name__ == '__main__':
    unittest.main()
