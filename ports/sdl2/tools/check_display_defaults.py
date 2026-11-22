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
        window_width, window_height = (640, 480) if (width, height) == (800, 600) else (800, 600)
        settings['video'].update(width=str(window_width), height=str(window_height),
                                 fullscreen_width='800', fullscreen_height='600', fullscreen='0')
        with path.open('w', encoding='utf-8') as file:
            settings.write(file)
        original = path.read_bytes()
        script = profile / 'fullscreen.txt'
        script.write_text('1 state startup\n' + ('2 key F11\n3 state after_f11\n' if portmaster else ''),
                          encoding='ascii')
        process = subprocess.run(
            [str(args.exe.resolve()), '--verbose', '--renderer', 'software',
             '--frames', '4', '--user-dir', str(profile), '--ui-script', str(script)],
            env=env, capture_output=True, timeout=60,
        )
        assert process.returncode == 0, process.stderr
        assert path.read_bytes() == original, 'Existing profile was changed'
        log = (profile / 'port.log').read_text(encoding='utf-8')
        assert 'Initial display resolution:' not in log
        actual = f'{width}x{height}' if portmaster else f'{window_width}x{window_height}'
        states = {line.split()[1]: line for line in log.splitlines() if line.startswith('STATE ')}
        assert f' size={actual} ' in states['startup'], log
        if portmaster:
            assert f' size={actual} ' in states['after_f11'], log
        report.append(f'{portmaster=} detected={width}x{height}; existing profile startup={actual}; saved settings preserved')
    (root / 'result.txt').write_text('\n'.join(report) + '\n', encoding='utf-8')
    print('Native defaults, CLI isolation and existing profiles: PASS')
