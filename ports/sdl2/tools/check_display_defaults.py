"""Verify native first-run defaults, CLI isolation and saved settings priority."""
import argparse
import configparser
import os
from pathlib import Path
import subprocess

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--expected', help='Expected SDL desktop mode, e.g. 1024x768')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    root = args.out.resolve()
    report = []
    for portmaster in [False, True]:
        profile = root / ('portmaster' if portmaster else 'desktop')
        profile.mkdir()
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', DOK_SOFTWARE_SDL_PRESENTATION='1',
                   DOK_PORTMASTER='1' if portmaster else '0')
        # This override must not replace the detected persistent defaults.
        command = [str(args.exe.resolve()), '--hidden', '--verbose', '--renderer', 'software',
                   '--size', '640x480', '--frames', '2', '--user-dir', str(profile)]
        process = subprocess.run(command, env=env, capture_output=True, timeout=60)
        if process.returncode:
            raise RuntimeError(process.stderr.decode('utf-8', errors='replace'))
        settings = configparser.ConfigParser()
        path = profile / 'settings.ini'
        settings.read(path, encoding='utf-8')
        width, height = settings.getint('video', 'width'), settings.getint('video', 'height')
        log = (profile / 'port.log').read_text(encoding='utf-8')
        assert f'Initial display resolution: {width}x{height}' in log, log
        if args.expected:
            assert f'{width}x{height}' == args.expected
        assert settings.getint('video', 'fullscreen_width') == width
        assert settings.getint('video', 'fullscreen_height') == height
        assert settings.getboolean('video', 'fullscreen') == portmaster
        settings['video'].update(width='800', height='600', fullscreen_width='800', fullscreen_height='600', fullscreen='0')
        with path.open('w', encoding='utf-8') as file:
            settings.write(file)
        original = path.read_bytes()
        process = subprocess.run(command, env=env, capture_output=True, timeout=60)
        assert process.returncode == 0, process.stderr
        assert path.read_bytes() == original, 'Existing profile was changed'
        assert 'Initial display resolution:' not in (profile / 'port.log').read_text(encoding='utf-8')
        report.append(f'{portmaster=} detected={width}x{height}; saved settings preserved')
    (root / 'result.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
    print('Native defaults, CLI isolation and existing profiles: PASS')
