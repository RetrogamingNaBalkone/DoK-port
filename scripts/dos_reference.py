"""Capture unmodified DOS games with an isolated DOSBox Staging HTTP session.

Input is explicitly injected into the game's researched keyboard-event state;
it does not exercise the emulated keyboard controller or original IRQ handler.
Frames are VGA mode-13 palette indices, displayed as grayscale, not true color.
Only isolated build/reference copies are writable by the emulated DOS machine.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import urllib.request

from verify_gba import png

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = b'OVERFLOW\0UNDERFLOW\0TLOSS\0PLOSS'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_sources(language, source, exe_name):
    inventory = json.loads((ROOT / f'analysis/inventory-{language}.json').read_text(encoding='utf-8'))
    before = {item.name: sha(item) for item in source.iterdir() if item.is_file()}
    res_name = Path(inventory['resource']['file']).name
    if before[exe_name] != inventory['sha256']:
        raise RuntimeError(f'{exe_name} does not match the researched inventory')
    if before[res_name] != inventory['resource']['sha256']:
        raise RuntimeError(f'{res_name} does not match the researched inventory')
    return before


class Api:
    def __init__(self, port):
        self.url = f'http://127.0.0.1:{port}/api/v1/'

    def get(self, path):
        with urllib.request.urlopen(self.url + path, timeout=3) as response:
            return response.read()

    def read(self, address, length):
        return self.get(f'memory/{address}/{length}')

    def write(self, address, data):
        request = urllib.request.Request(self.url + f'memory/{address}', data=data,
                                        method='PUT', headers={'Content-Type': 'application/octet-stream'})
        with urllib.request.urlopen(request, timeout=3) as response:
            return response.read()


def wait_for(operation, timeout=30):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            result = operation()
            if result:
                return result
        except (OSError, ValueError) as error:
            last = error
        time.sleep(.1)
    raise TimeoutError(f'Guest did not reach state: {last}')


def locate_data(api, delta):
    memory = api.read(0, 0x1000000)
    if memory.count(ANCHOR) != 1:
        return None
    base = memory.find(ANCHOR) - 0x1082 - delta
    width, height = struct.unpack_from('<HH', memory, base + 0xc1fc + delta)
    return base if (width, height) == (320, 200) else None


def capture(api, directory, name, stable=True):
    pixels = api.read(0xa0000, 64000)
    matches = 0
    for _ in range(50 if stable else 0):
        time.sleep(.05)
        next_pixels = api.read(0xa0000, 64000)
        matches = matches + 1 if pixels == next_pixels else 0
        pixels = next_pixels
        if matches >= 2:
            break
    (directory / (name + '.idx')).write_bytes(pixels)
    png(directory / (name + '.png'), 320, 200,
        b''.join(bytes((value, value, value)) for value in pixels))
    return {'name': name, 'sha256': hashlib.sha256(pixels).hexdigest(),
            'stable_three_samples': matches >= 2, 'palette': 'grayscale index visualization',
            'physical_address': '0xa0000', 'width': 320, 'height': 200}


def key(api, base, delta, scan, seconds=.12):
    """Inject the documented make state, event count and last scancode."""
    key_base = base + 0x2155 + delta
    pending = struct.unpack('<H', api.read(key_base - 3, 2))[0]
    api.write(key_base + scan, b'\x03')
    api.write(key_base - 3, struct.pack('<HB', (pending + 1) & 65535, scan))
    time.sleep(seconds)
    state = api.read(key_base + scan, 1)[0]
    api.write(key_base + scan, bytes([state & ~1]))


def player(api, address):
    data = api.read(address, 0xde)
    return {'linear_address': hex(address), 'native_type': struct.unpack_from('<H', data, 0x12)[0],
            'xyz': list(struct.unpack_from('<iii', data, 0x26)),
            'heading': struct.unpack_from('<h', data, 0x32)[0],
            'health_energy': list(struct.unpack_from('<hh', data, 0x5e)),
            'lives_score': list(struct.unpack_from('<hh', data, 0xd6))}


def find_player(memory, language):
    matches = []
    pattern = struct.pack('<iii', 15120, -300, 22800 if language == 'ru' else 23280)
    offset = 0
    while True:
        offset = memory.find(pattern, offset)
        if offset < 0:
            break
        base = offset - 0x26
        if base >= 0 and struct.unpack_from('<H', memory, base + 0x12)[0] == 1:
            matches.append(base)
        offset += 1
    return matches[0] if len(matches) == 1 else None


def trial(language, directory, dosbox, port, steps, actions, time_probe=False):
    directory = directory.resolve()
    directory.mkdir(parents=True, exist_ok=False)
    source = ROOT / 'originals/pkr' / ('RUS' if language == 'ru' else 'ENG')
    exe_name = 'PKR.EXE' if language == 'ru' else 'PKE.EXE'
    delta = 0 if language == 'ru' else 0xb0
    before = validate_sources(language, source, exe_name)
    game = directory / 'game'
    shutil.copytree(source, game)
    config = f'''[sdl]
fullscreen = off
[dosbox]
machine = svga_s3
memsize = 32
[cpu]
core = normal
cputype = pentium
cpu_cycles = 60000
cpu_cycles_protected = 60000
[mixer]
nosound = true
[sblaster]
sbtype = sb16
sbbase = 220
irq = 7
dma = 1
hdma = 5
[webserver]
webserver_enabled = on
webserver_bind_address = 127.0.0.1
webserver_port = {port}
[autoexec]
mount -u c
mount c "{game}"
c:
{exe_name}
'''
    cfg = directory / 'dosbox.conf'
    cfg.write_text(config, encoding='ascii')
    api = Api(port)
    try:
        api.get('dosbox/info')
    except OSError:
        pass
    else:
        raise RuntimeError('Refusing occupied HTTP port')
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    report = {'language': language, 'exe_before': before[exe_name],
              'source_file_hashes_before': before, 'inventory_exe_and_res_matched': True,
              'dosbox_sha256': sha(dosbox),
              'command': [str(dosbox), '--noprimaryconf', '--nolocalconf', '-conf', str(cfg), '-noconsole'],
              'input_method': 'game keyboard state injection; no keyboard controller/IRQ validation',
              'frames': [], 'events': [], 'configuration': config}
    with (directory / 'stdout.log').open('wb') as out, (directory / 'stderr.log').open('wb') as err:
        proc = subprocess.Popen(report['command'], cwd=directory, env=env, stdout=out, stderr=err,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            report['emulator'] = wait_for(lambda: json.loads(api.get('dosbox/info')))
            base = wait_for(lambda: locate_data(api, delta))
            report['runtime_linear_data_base'] = hex(base)
            report['keyboard_data_offsets'] = {'states': hex(0x2155 + delta),
                                               'pending_count': hex(0x2152 + delta),
                                               'last_scancode': hex(0x2154 + delta)}
            time.sleep(1)
            report['frames'].append(capture(api, directory, 'boot'))
            for index in range(steps):
                key(api, base, delta, 0x1c)
                report['events'].append({'event': 'enter', 'scan': '0x1c', 'held_wall_seconds': .12,
                                         'following_wait_wall_seconds': 1.5})
                time.sleep(1.5)
                report['frames'].append(capture(api, directory, f'enter-{index+1:02}'))
                (directory / f'data-{index+1:02}.bin').write_bytes(api.read(base, 65536))
            memory = api.read(0, 0x1000000)
            (directory / 'level1-memory.bin').write_bytes(memory)
            hero = find_player(memory, language)
            if hero is None:
                raise RuntimeError('No unique LEVEL1 hero matching the verified initial state')
            report['player_start'] = player(api, hero)
            report['frames'].append(capture(api, directory, 'level1-start'))
            if not report['frames'][-1]['stable_three_samples']:
                raise RuntimeError('LEVEL1 initial framebuffer did not stabilize')
            if time_probe:
                samples = []
                for _ in range(21):
                    started = time.monotonic()
                    data = api.read(base, 0x2200)
                    samples.append(dict(wall_seconds=started,
                        irq_count=struct.unpack_from('<I', data, 0xe70+delta)[0],
                        pit_period=struct.unpack_from('<I', data, 0xe78+delta)[0],
                        scheduler_elapsed=struct.unpack_from('<I', data, 0x20cc+delta)[0],
                        owner_count=struct.unpack_from('<H', data, 0x3e8+(0 if language=='ru' else 8))[0]))
                    time.sleep(.1)
                origin = samples[0]['wall_seconds']
                for sample in samples:
                    sample['wall_seconds'] -= origin
                report['time_probe'] = samples
            if actions:
                for name, scan, duration in [('turn-right', 0x4d, .35), ('forward', 0x48, .6),
                                              ('use-attempt', 0x39, .12), ('fire-attempt', 0x1d, .4)]:
                    before_action = player(api, hero) if hero is not None else None
                    key(api, base, delta, scan, duration)
                    time.sleep(.5)
                    event = {'event': name, 'scan': hex(scan), 'held_wall_seconds': duration,
                             'following_wait_wall_seconds': .5, 'player_before': before_action,
                             'player_after': player(api, hero) if hero is not None else None}
                    report['events'].append(event)
                    report['frames'].append(capture(api, directory, name))
            report['cpu_final'] = json.loads(api.get('cpu/state'))
            report['status'] = 'complete'
        except Exception as error:
            report['status'] = 'failed'
            report['error'] = repr(error)
            raise
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            after = {item.name: sha(item) for item in source.iterdir() if item.is_file()}
            report['originals_unchanged'] = before == after
            report['source_file_hashes_after'] = after
            report['copy_file_hashes_after'] = {item.name: sha(item) for item in game.iterdir() if item.is_file()}
            report['exe_after'] = after[exe_name]
            report['copy_exe_after'] = sha(game / exe_name)
            runtime_names = (exe_name, exe_name.replace('.EXE', '.RES'), 'RTM.EXE', 'DPMI16BI.OVL')
            report['runtime_copies_unchanged'] = all(
                report['copy_file_hashes_after'].get(name) == before[name] for name in runtime_names)
            if not report['originals_unchanged'] or not report['runtime_copies_unchanged']:
                report['status'] = 'failed'
                report['error'] = 'Source or copied runtime files changed'
            (directory / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
            assert before == after, 'Original inputs changed'
            assert report['runtime_copies_unchanged'], 'Copied runtime files changed'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('language', choices=('ru', 'en'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dosbox', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/DOSBox-Staging/wasdify-webserver-20261024/dosbox.exe')
    parser.add_argument('--port', type=int, default=8137)
    parser.add_argument('--enters', type=int, default=6)
    parser.add_argument('--actions', action='store_true')
    parser.add_argument('--time-probe', action='store_true')
    args = parser.parse_args()
    report = trial(args.language, args.output, args.dosbox, args.port, args.enters, args.actions, args.time_probe)
    print(json.dumps({'output': str(args.output), 'player_start': report.get('player_start'),
                      'frames': report['frames'], 'originals_unchanged': report['originals_unchanged']}, indent=2))


if __name__ == '__main__':
    main()
