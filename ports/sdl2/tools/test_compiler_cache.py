import os
from pathlib import Path
import unittest
from unittest.mock import patch

from compiler_cache import ROOT, MARKER, compiler_cache
from experiment_cleanup import temporary_workspace


class CompilerCacheTest(unittest.TestCase):
    def test_nested_failure_cleanup_and_environment(self):
        with temporary_workspace(ROOT/'build/sdl2/storage-tests','cache-') as root:
            with patch.dict(os.environ, {}, clear=True):
                os.environ['ZIG_GLOBAL_CACHE_DIR'] = 'previous'
                with self.assertRaisesRegex(RuntimeError, 'expected'):
                    with compiler_cache(root) as path:
                        (path/'data').write_bytes(b'cached')
                        with compiler_cache(root) as child:
                            self.assertEqual(path, child)
                        self.assertTrue(path.exists())
                        raise RuntimeError('expected')
                self.assertFalse(path.exists())
                self.assertEqual(os.environ['ZIG_GLOBAL_CACHE_DIR'], 'previous')
                self.assertNotIn(MARKER, os.environ)
                self.assertNotIn('ZIG_LOCAL_CACHE_DIR', os.environ)

    def test_foreign_inherited_path_rejected(self):
        with patch.dict(os.environ, {MARKER: str(ROOT/'originals')}):
            with self.assertRaises(ValueError):
                with compiler_cache():
                    self.fail('Foreign path accepted')

    def test_keep_is_explicit(self):
        with temporary_workspace(ROOT/'build/sdl2/storage-tests','cache-') as root:
            with patch.dict(os.environ, {'DOK_KEEP_COMPILER_CACHE':'1'}):
                with compiler_cache(root) as path:
                    pass
                self.assertTrue(path.exists())


if __name__ == '__main__':
    unittest.main()
