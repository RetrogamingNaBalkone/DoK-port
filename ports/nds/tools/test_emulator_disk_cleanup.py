"""Test disk retention/cleanup without loading an emulator core or ROM."""
from __future__ import annotations

import ast
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch
from uuid import uuid4

from emulator_disk_cleanup import _safe

TOOLS = Path(__file__).resolve().parent
WORKSPACE = TOOLS.parents[2]
CHILD = r'''
import json,shutil,sys
from pathlib import Path
from emulator_disk_cleanup import register_disk_cleanup
from frontend_fat_oracle import volume_files
from make_assets_fixture import write_image
root=Path(sys.argv[1]);mode=sys.argv[2]
directory=root/'build/nds'/('restart' if mode=='restart' else 'test')
directory.mkdir(parents=True,exist_ok=True)
image=directory/'save/melonDS DS/dldi_sd_card.bin'
image.parent.mkdir(parents=True,exist_ok=True)
payload=b'DOKS\x01\x00\x10\x00'+bytes([2,0,0,0,1,2,3,4])
def fixture():
    write_image(image,[('settings.dat',b'SETTINGSDAT',payload)])
if mode=='preexisting':fixture()
class Owner:
    def __init__(self):
        self.directory=directory;self.loaded=True;self.initialized=True
        self._keep_disk_for_restart=mode=='producer'
    def close(self):
        if mode=='close-fails':raise RuntimeError('core close failed')
        self.loaded=False;self.initialized=False
        with (directory/'closed.txt').open('a',encoding='utf-8') as f:f.write('closed\n')
owner=Owner();session=register_disk_cleanup(owner,root)
if mode=='restart':shutil.copyfile(sys.argv[3],image)
elif mode!='preexisting':fixture()
image.with_name(image.name+'.idx').write_bytes(b'index')
if mode=='unknown':
    write_image(image,[('unique.dat',b'UNIQUE  DAT',b'unique research')])
if mode=='duplicate':
    other=Owner();assert register_disk_cleanup(other,root) is session
if mode=='malformed':image.write_bytes(b'incomplete failure fixture')
(directory/'failure.png').write_bytes(b'preserved capture')
print(json.dumps({'directory':str(directory),'image':str(image)}),flush=True)
if mode=='fail':raise RuntimeError('original runner failure')
if mode not in ('close-fails','duplicate'):
    owner.close()
    assert image.exists() # Post-close audits must still see the image.
    if mode!='malformed':volume_files(image)
    (directory/'result.json').write_text('{"passed":true}',encoding='utf-8')
'''


class DiskCleanupTest(unittest.TestCase):
    def setUp(self):
        self.parent = WORKSPACE / 'build/nds/disk-cleanup-tests'
        self.root = self.parent / ('run-' + uuid4().hex)
        _safe(self.root, WORKSPACE)
        self.root.mkdir(parents=True)

    def tearDown(self):
        _safe(self.root, WORKSPACE)
        if self.root.parent != self.parent:
            raise ValueError('Unexpected test cleanup directory')
        shutil.rmtree(self.root)

    def child(self, mode='ok', keep=False, source=None):
        env = dict(os.environ, PYTHONPATH=str(TOOLS))
        env.pop('NDS_KEEP_EMULATOR_DISK', None)
        if keep:
            env['NDS_KEEP_EMULATOR_DISK'] = '1'
        args = [sys.executable, '-B', '-X', 'utf8', '-c', CHILD, str(self.root), mode]
        if source:
            args.append(str(source))
        result = subprocess.run(args, env=env, capture_output=True, text=True)
        paths = json.loads(result.stdout.splitlines()[0])
        return result, Path(paths['directory']), Path(paths['image'])

    def metadata(self, directory):
        return json.loads((directory / 'disk-retention.json').read_text(encoding='utf-8'))

    def test_cleanup_after_close_and_post_close_audit(self):
        result, directory, image = self.child()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(image.exists())
        self.assertFalse(image.with_name(image.name + '.idx').exists())
        self.assertTrue((directory / 'result.json').is_file())
        self.assertEqual((directory / 'failure.png').read_bytes(), b'preserved capture')
        report = self.metadata(directory)
        self.assertEqual(report['status'], 'deleted')
        self.assertEqual(len(report['sha256']), 64)
        settings = next(r for r in report['files'] if r['path'] == '/DOK/SETTINGS.DAT')
        self.assertEqual(settings['bytes'], 16)
        self.assertEqual(len(bytes.fromhex(settings['hex'])), 16)

    def test_original_failure_survives_cleanup_and_core_is_closed(self):
        result, directory, image = self.child('fail')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('original runner failure', result.stderr)
        self.assertTrue((directory / 'closed.txt').is_file())
        self.assertFalse(image.exists())

    def test_close_failure_never_removes_mounted_disk(self):
        result, directory, image = self.child('close-fails')
        self.assertEqual(result.returncode, 0)
        self.assertTrue(image.is_file())
        self.assertIn('core close failed', result.stderr)
        self.assertFalse((directory / 'disk-retention.json').exists())

    def test_keep_environment_retains_image_and_index(self):
        result, directory, image = self.child(keep=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(image.is_file())
        self.assertTrue(image.with_name(image.name + '.idx').is_file())
        self.assertEqual(self.metadata(directory)['reason'], 'NDS_KEEP_EMULATOR_DISK=1')

    def test_settings_producer_is_input_to_next_restart_process(self):
        result, directory, source = self.child('producer')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(source.is_file())
        self.assertEqual(self.metadata(directory)['reason'],
                         'settings-producer-input-for-downstream-restart')
        restarted, copied_directory, copied = self.child('restart', source=source)
        self.assertEqual(restarted.returncode, 0, restarted.stderr)
        self.assertTrue(source.is_file(), 'Caller-provided restart input must survive')
        self.assertFalse(copied.exists())
        self.assertEqual(self.metadata(copied_directory)['status'], 'deleted')

    def test_multiple_owners_share_one_cleanup_and_metadata(self):
        result, directory, image = self.child('duplicate')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((directory / 'closed.txt').read_text().splitlines(), ['closed', 'closed'])
        self.assertFalse(image.exists())
        self.assertEqual(self.metadata(directory)['status'], 'deleted')

    def test_unchanged_preexisting_image_is_not_owned(self):
        result, directory, image = self.child('preexisting')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(image.is_file())
        self.assertEqual(self.metadata(directory)['reason'],
                         'unchanged-preexisting-disk-not-created-by-this-run')

    def test_unknown_payload_is_retained(self):
        result, directory, image = self.child('unknown')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(image.is_file())
        self.assertEqual(self.metadata(directory)['reason'], 'unknown-disk-files')

    def test_incomplete_failure_fixture_is_retained(self):
        result, directory, image = self.child('malformed')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(image.is_file())
        self.assertEqual(self.metadata(directory)['reason'], 'unrecognised-or-failed-fixture')

    def test_outside_output_and_reparse_ancestor_are_rejected(self):
        with self.assertRaises(ValueError):
            _safe(self.root / 'outside', self.root)
        target = self.root / 'build/nds/output/save/melonDS DS/dldi_sd_card.bin'
        original = Path.is_junction
        with patch.object(Path, 'is_junction', lambda p: p == target.parent or original(p)):
            with self.assertRaises(ValueError):
                _safe(target, self.root)

    def test_base_registration_precedes_core_load_and_close_keeps_disk(self):
        source = (TOOLS / 'run_diag_emulator.py').read_text(encoding='utf-8')
        tree = ast.parse(source)
        frontend = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'Frontend')
        init = next(n for n in frontend.body if isinstance(n, ast.FunctionDef) and n.name == '__init__')
        text = ast.get_source_segment(source, init)
        self.assertLess(text.index('register_disk_cleanup('), text.index('C.CDLL('))
        close = next(n for n in frontend.body if isinstance(n, ast.FunctionDef) and n.name == 'close')
        self.assertNotIn('unlink', ast.get_source_segment(source, close))
        frontend_source = (TOOLS / 'run_frontend_emulator.py').read_text(encoding='utf-8')
        self.assertIn('frontend._keep_disk_for_restart = args.mode == "settings"', frontend_source)


if __name__ == '__main__':
    unittest.main()
