"""Exercise launcher exit/cleanup and persistent settings without a handheld."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'portmaster'


@unittest.skipIf(os.name == 'nt', 'PortMaster launcher requires bash/Linux')
class LauncherTests(unittest.TestCase):
    def test_launch_failure_cleanup_and_profile_preservation(self):
        with tempfile.TemporaryDirectory(prefix='ogdok-launcher-') as directory:
            root = Path(directory)
            ports = root / 'ports with spaces'
            game = ports / 'ogdok'
            game.mkdir(parents=True)
            control = root / 'xdg/PortMaster'
            control.mkdir(parents=True)
            (control / 'control.txt').write_text('''CFW_NAME=mock
DEVICE_ARCH=aarch64
GPTOKEYB=/bin/true
get_controls() { sdl_controllerconfig=mock-mapping; }
pm_platform_helper() { printf 'platform\\n' >> "$MOCK_RECORD"; }
pm_finish() { printf 'cleanup\\n' >> "$MOCK_RECORD"; }
''')
            shutil.copyfile(SOURCE / 'OGDoK.sh', ports / 'OGDoK.sh')
            executable = game / 'OGDoK'
            executable.write_text('''#!/bin/bash
printf '%s\\n' "$@" > "$MOCK_ARGS"
printf '%s' "$SDL_GAMECONTROLLERCONFIG" > "$MOCK_MAPPING"
printf '%s' "$DOK_PORTMASTER" > "$MOCK_PLATFORM"
exit "${MOCK_EXIT:-0}"
''')
            executable.chmod(0o755)
            env = dict(os.environ, XDG_DATA_HOME=str(root / 'xdg'), MOCK_RECORD=str(root / 'record'),
                       MOCK_ARGS=str(root / 'args'), MOCK_MAPPING=str(root / 'mapping'), MOCK_PLATFORM=str(root / 'platform'))
            process = subprocess.run(['bash', str(ports / 'OGDoK.sh')], env=env, capture_output=True, timeout=15)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual((root / 'args').read_text().splitlines(), ['--user-dir', str(game / 'user')])
            self.assertEqual((root / 'mapping').read_text(), 'mock-mapping')
            settings = game / 'user/settings.ini'
            self.assertFalse(settings.exists(), 'Launcher must not seed fixed video settings')
            self.assertEqual((root / 'platform').read_text(), '1')
            settings.write_text('existing user settings')
            env['MOCK_EXIT'] = '7'
            process = subprocess.run(['bash', str(ports / 'OGDoK.sh')], env=env, capture_output=True, timeout=15)
            self.assertEqual(process.returncode, 7)
            self.assertEqual(settings.read_text(), 'existing user settings')
            self.assertEqual((root / 'record').read_text().splitlines(), ['platform', 'cleanup', 'platform', 'cleanup'])


if __name__ == '__main__':
    unittest.main()
