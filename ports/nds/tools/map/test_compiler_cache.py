"""Exercise cache cleanup in real child processes without rebuilding proofs."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch
from uuid import uuid4

import compiler_cache

HELPER = Path(__file__).resolve().parent
WORKSPACE = HELPER.parents[3]
CHILD = r'''
import json,sys
from pathlib import Path
from compiler_cache import compiler_environment
root=Path(sys.argv[1])
env=compiler_environment(root)
cache=Path(env['ZIG_LOCAL_CACHE_DIR'])
(cache/'partial.o').write_bytes(b'partial compiler output')
(Path(env['ZIG_GLOBAL_CACHE_DIR'])/'shared').write_bytes(b'reusable')
(root/'proof.exe').write_bytes(b'retained proof')
(root/'result.json').write_text('{"passed":true}',encoding='utf-8')
print(json.dumps({'local':str(cache),'global':env['ZIG_GLOBAL_CACHE_DIR']}),flush=True)
if sys.argv[2]=='fail':raise RuntimeError('deliberate compile/check failure')
'''


class CompilerCacheTest(unittest.TestCase):
    def setUp(self):
        self.parent = WORKSPACE / 'build/nds/cache-cleanup-tests'
        self.root = self.parent / ('run-' + uuid4().hex)
        compiler_cache._plain_path(self.root, WORKSPACE)
        self.root.mkdir(parents=True)

    def tearDown(self):
        compiler_cache._plain_path(self.root, WORKSPACE)
        if self.root.parent != self.parent:
            raise ValueError('Unexpected cleanup test directory')
        shutil.rmtree(self.root)

    def child(self, mode='ok', keep=False):
        env = dict(os.environ, PYTHONPATH=str(HELPER))
        env.pop('NDS_KEEP_COMPILER_CACHE', None)
        if keep:
            env['NDS_KEEP_COMPILER_CACHE'] = '1'
        result = subprocess.run([sys.executable, '-B', '-X', 'utf8', '-c', CHILD,
                                 str(self.root), mode], env=env, text=True,
                                capture_output=True)
        return result, json.loads(result.stdout.splitlines()[0])

    def test_success_removes_local_only(self):
        result, paths = self.child()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(Path(paths['local']).exists())
        self.assertTrue((Path(paths['global']) / 'shared').is_file())
        self.assertEqual((self.root / 'proof.exe').read_bytes(), b'retained proof')
        self.assertTrue((self.root / 'result.json').is_file())

    def test_failure_preserves_exit_and_cleans_partial_cache(self):
        result, paths = self.child('fail')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('deliberate compile/check failure', result.stderr)
        self.assertFalse(Path(paths['local']).exists())
        self.assertTrue((self.root / 'proof.exe').is_file())

    def test_keep_environment_retains_local(self):
        result, paths = self.child(keep=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((Path(paths['local']) / 'partial.o').read_bytes(),
                         b'partial compiler output')

    def test_processes_use_distinct_local_cache_and_same_global(self):
        _, first = self.child(keep=True)
        _, second = self.child(keep=True)
        self.assertNotEqual(first['local'], second['local'])
        self.assertEqual(first['global'], second['global'])

    def test_cleanup_cannot_remove_other_output(self):
        target = self.root / 'build/nds/retained'
        target.mkdir(parents=True)
        with self.assertRaises(ValueError):
            compiler_cache._remove_cache(target, self.root)
        self.assertTrue(target.is_dir())

    def test_cleanup_rejects_reparse_ancestor(self):
        _, paths = self.child(keep=True)
        target = Path(paths['local'])
        original = compiler_cache._reparse
        with patch.object(compiler_cache, '_reparse',
                          side_effect=lambda p: p == target.parent or original(p)):
            with self.assertRaises(ValueError):
                compiler_cache._remove_cache(target, self.root)
        self.assertTrue(target.is_dir())

    def test_cleanup_validates_descendants_before_deleting(self):
        _, paths = self.child(keep=True)
        target = Path(paths['local'])
        nested = target / 'linked'
        nested.mkdir()
        original = compiler_cache._reparse
        with patch.object(compiler_cache, '_reparse',
                          side_effect=lambda p: p == nested or original(p)):
            with self.assertRaises(ValueError):
                compiler_cache._remove_cache(target, self.root)
        self.assertTrue((target / 'partial.o').is_file())


if __name__ == '__main__':
    unittest.main()
