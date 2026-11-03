"""Check cache ownership across nested processes and failing builds."""
import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

import build_cache as cache
from experiment_cleanup import temporary_workspace

ROOT=Path(__file__).resolve().parents[1]


class BuildCacheTest(unittest.TestCase):
    def setUp(self):
        self.workspace=temporary_workspace(ROOT/'build/maintenance-tests','cache-')
        self.root=self.workspace.__enter__()

    def tearDown(self):
        self.workspace.__exit__(None,None,None)

    def test_child_reuses_cache_without_registering_deletion(self):
        with patch.dict(os.environ,{},clear=True), patch.object(cache,'_sessions',{}), \
                patch.object(cache.atexit,'register') as register:
            env=cache.compiler_environment(self.root)
            session=Path(env[cache.MARKER])
            self.assertTrue(session.is_dir())
            self.assertEqual(register.call_count,1)
            with patch.dict(os.environ,env,clear=True):
                child=cache.compiler_environment(self.root)
            self.assertEqual(child[cache.MARKER],str(session))
            self.assertEqual(register.call_count,1)
            cache.remove_session(self.root,session)
            self.assertFalse(session.exists())

    def test_refuses_foreign_inherited_directory(self):
        with patch.dict(os.environ,{cache.MARKER:str(self.root/'wrong')},clear=True):
            with self.assertRaises(ValueError):
                cache.compiler_environment(self.root)

    def test_failure_exit_removes_owned_cache(self):
        record=self.root/'cache.json'
        source=('import sys,json; from pathlib import Path; '
                f'sys.path.insert(0,{str(ROOT/"scripts")!r}); '
                'from build_cache import compiler_environment; '
                f'env=compiler_environment(Path({str(self.root)!r})); '
                f'Path({str(record)!r}).write_text(json.dumps(env)); '
                'raise RuntimeError("expected compiler failure")')
        env=dict(os.environ)
        env.pop(cache.MARKER,None)
        env.pop('DOK_KEEP_COMPILER_CACHE',None)
        result=subprocess.run([sys.executable,'-B','-c',source],env=env,capture_output=True)
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(Path(json.loads(record.read_text())[cache.MARKER]).exists())

    def test_retain_cache_is_explicit(self):
        with patch.dict(os.environ,{'DOK_KEEP_COMPILER_CACHE':'1'},clear=True), \
                patch.object(cache,'_sessions',{}), patch.object(cache.atexit,'register') as register:
            env=cache.compiler_environment(self.root)
            register.assert_not_called()
            cache.remove_session(self.root,Path(env[cache.MARKER]))


if __name__=='__main__':
    unittest.main()
