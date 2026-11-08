"""Behaviour checks for package rejection and complete-frame summaries."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / 'tools'


def module(name):
    spec = importlib.util.spec_from_file_location(name, TOOLS / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class UnixToolsTests(unittest.TestCase):
    def test_portmaster_install_layout_is_not_source_layout(self):
        validator = module('check_unix_package').validate_portmaster_layout
        files = {'OGDoK.sh': b'', 'ogdok/OGDoK': b'', 'ogdok/ogdok.md': b'',
                 'ogdok/port.json': json.dumps({'name': 'ogdok.zip', 'items': ['OGDoK.sh', 'ogdok/']}).encode(),
                 'ogdok/gameinfo.xml': b'<gameList><game><path>./OGDoK.sh</path></game></gameList>'}
        validator(files)
        for name in ['port.json', 'gameinfo.xml']:
            wrong = dict(files)
            wrong[name] = wrong.pop('ogdok/' + name)
            with self.subTest(name=name), self.assertRaises(ValueError):
                validator(wrong)
        extra = dict(files, **{'README.md': b''})
        with self.assertRaises(ValueError):
            validator(extra)

    def test_summary_warmup_and_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'frames.csv'
            path.write_text('frame,work_ms,present_ms,total_ms\n0,100,100,200\n1,2,3,5\n2,4,6,10\n', encoding='utf-8')
            report = module('summarize_frames').summarize(path, 1)
            self.assertEqual(report['samples'], 2)
            self.assertEqual(report['total_ms']['p99'], 10)
            self.assertAlmostEqual(report['fps'], 2000 / 15)
            with self.assertRaises(ValueError):
                module('summarize_frames').summarize(path, 3)

    def test_reject_nonfinite_capture(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'frames.csv'
            path.write_text('frame,work_ms,present_ms,total_ms\n0,nan,1,1\n', encoding='utf-8')
            with self.assertRaises(ValueError):
                module('summarize_frames').summarize(path, 0)

    def test_reject_wrong_elf_before_creating_package(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'OGDoK'
            path.write_bytes(b'MZ' + b'\0' * 30)
            with self.assertRaises(ValueError):
                module('package_unix').elf_machine(path)
            header = bytearray(b'\x7fELF\x02\x01' + b'\0' * 14)
            struct.pack_into('<H', header, 18, 183)
            path.write_bytes(header)
            self.assertEqual(module('package_unix').elf_machine(path), 183)
            process = subprocess.run([sys.executable, str(TOOLS / 'package_unix.py'),
                'linux-x86_64', '--build', directory, '--name', 'rejected-test'], capture_output=True)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn(b'architecture', process.stderr)


if __name__ == '__main__':
    unittest.main()
