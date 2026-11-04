"""Capture actual original title SB output in isolated, headless DOS sessions.

Uses the installed DOSBox-Automation API. Only evidence copies are mounted.
This records emulated hardware output, not a WAV made by decoding RES.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import time
import urllib.error
import urllib.request
import wave

ROOT = Path(__file__).resolve().parents[3]
PROFILES = {'ru': ('pkr/RUS', 'PKR'), 'en': ('pkr/ENG', 'PKE'),
            'old-gold': ('Old_Gold_3D', 'OG')}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Api:
    def __init__(self, port, token):
        self.url = f'http://127.0.0.1:{port}/api/v1/'
        self.token = token

    def call(self, path, data=None, method='GET'):
        body = None if data is None else json.dumps(data).encode()
        req = urllib.request.Request(self.url + path, data=body, method=method,
            headers={'Authorization': 'Bearer ' + self.token, 'Content-Type': 'application/json'})
        with urllib.request.urlopen(req, timeout=5) as response:
            raw = response.read()
        return json.loads(raw)

    def read(self, address, size):
        req = urllib.request.Request(self.url + f'memory/{address}/{size}',
                                    headers={'Authorization': 'Bearer ' + self.token})
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.read()

    def write(self, address, data):
        req = urllib.request.Request(self.url + f'memory/{address}', data=data, method='PUT',
            headers={'Authorization': 'Bearer ' + self.token, 'Content-Type': 'application/octet-stream'})
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.read()


def run(profile, seconds, port, hold_og=False, label=None):
    folder, name = PROFILES[profile]
    source = ROOT / 'originals' / folder
    before = {p.name: sha(p) for p in source.iterdir() if p.is_file()}
    label = label or ('held-v2' if hold_og else 'title-device')
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', label):
        raise ValueError('Evidence label must be a directory name')
    out = ROOT / 'research/evidence/source-port/sp13-audio' / ('dos-' + label) / profile
    if (out / 'report.json').exists():
        raise RuntimeError('Capture already exists; preserve it or choose a new evidence directory')
    out.mkdir(parents=True, exist_ok=True)
    game = out / 'game'
    shutil.copytree(source, game, dirs_exist_ok=True)
    # An isolated helper delays launch so recording starts before the first DAC
    # write. BIOS wait AH86 receives two million microseconds, then exits to DOS.
    (game / 'SP13WAIT.COM').write_bytes(bytes.fromhex('b80086b91e00ba8084cd15b8004ccd21'))
    capture = out / 'capture'
    capture.mkdir(exist_ok=True)
    local = out / 'local'
    local.mkdir(exist_ok=True)
    config = out / 'dosbox.conf'
    config.write_text(f'''[sdl]
fullscreen=false
pause_when_inactive=false
mute_when_inactive=false
[dosbox]
machine=svga_s3
memsize=32
[cpu]
core=normal
cpu_cycles=60000
cpu_cycles_protected=60000
[mixer]
nosound=false
rate=48000
reverb=off
chorus=off
compressor=off
[sblaster]
sbtype=sb16
sb_filter=off
sb_filter_always_on=false
[capture]
capture_dir={capture.as_posix()}
[webserver]
webserver_enabled=true
webserver_bind_address=127.0.0.1
webserver_port={port}
[autoexec]
mount c "{game.as_posix()}"
c:
SP13WAIT.COM
{name}.EXE -b
''', encoding='utf-8')
    exe = Path(os.environ['LOCALAPPDATA']) / 'Programs/DOSBox-Automation/0.85.1/dosbox-automation-0.85.1/dosbox.exe'
    token = hashlib.sha256(('sp13-isolated-' + profile).encode()).hexdigest()
    env = dict(os.environ, LOCALAPPDATA=str(local), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', DOSBOX_API_TOKEN=token)
    api = Api(port, token)
    patch = None
    trace = []
    capture_started = None

    def wait_record(duration):
        deadline = time.monotonic()+duration
        while time.monotonic() < deadline:
            if patch is not None:
                globals_ = api.read(base+0xa0, 12)
                pointer, ignored_count, next_pointer = struct.unpack('<III', globals_)
                linear = (pointer >> 16)*16+(pointer & 65535)
                next_linear = (next_pointer >> 16)*16+(next_pointer & 65535)
                count = struct.unpack('<I', api.read(base+0xd40, 4))[0]
                header = api.read(linear, 36) if pointer else bytes(36)
                start = struct.unpack_from('<I', header, 24)[0]
                start_linear = (start >> 16)*16+(start & 65535)
                trace.append(dict(capture_elapsed_seconds=time.monotonic()-capture_started,
                                  delivered_irq_count=count, active=bool(header[7]&0x80),
                                  sample_offset=next_linear-start_linear))
            time.sleep(min(.1, max(0, deadline-time.monotonic())))
    report = dict(profile=profile, emulator=str(exe), emulator_sha256=sha(exe),
                  source_sha256_before=before, command=name + '.EXE -b',
                  boundary='Actual title audio via native Sound Blaster path in DOSBox; not whole-game acoustic acceptance')
    with (out / 'stdout.log').open('w', encoding='utf-8') as stdout:
        process = subprocess.Popen([str(exe), '--noprimaryconf', '--nolocalconf', '-conf', str(config), '-noconsole'],
            env=env, cwd=out, stdout=stdout, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            for _ in range(80):
                if process.poll() is not None:
                    raise RuntimeError('DOSBox exited before API startup')
                try:
                    report['emulator_info'] = api.call('dosbox/info')
                    break
                except (OSError, ValueError):
                    time.sleep(.1)
            else:
                raise TimeoutError('DOSBox API not ready')
            time.sleep(1)
            report['capture_start'] = api.call('capture/audio/start', method='POST')
            capture_started = time.monotonic()
            report['launch'] = 'autoexec: ' + name + '.EXE -b'
            if hold_og:
                assert profile == 'old-gold', 'Consumer hold is researched for OG only'
                for _ in range(200):
                    memory = api.read(0, 0x100000)
                    anchor = b'Not same device\0'
                    if memory.count(anchor) == 1:
                        base = memory.find(anchor)-0x189d
                        address = 0x20c40+0xf9+(base-0x32550)
                        original = api.read(address, 5)
                        exported = (ROOT/'research/evidence/source-port/sp13-audio/ghidra/old-gold/startup.bin').read_bytes()[0xf9-0xf:0xfe-0xf]
                        assert original[:3] == exported[:3] == bytes.fromhex('9a5d02'), 'Native title stop call differs'
                        api.write(address, bytes.fromhex('ebfe909090'))
                        patch = (address, original)
                        report['consumer_hold'] = dict(ghidra='20c4:00f9', runtime_linear=hex(address),
                            original=original.hex(), replacement='ebfe909090',
                            boundary='Hold main consumer before stop/free; native SB IRQ, timer, PCM and resource flags remain unmodified')
                        break
                    time.sleep(.01)
                else:
                    raise TimeoutError('OG not loaded before consumer hold')
            wait_record(min(3, seconds))
            if profile == 'old-gold':
                memory = api.read(0, 0x100000)
                anchor = b'Not same device\0'
                assert memory.count(anchor) == 1, 'Ambiguous OG DGROUP anchor'
                base = memory.find(anchor) - 0x189d
                far = lambda at: (lambda p: (p >> 16)*16 + (p & 65535))(struct.unpack_from('<I', memory, at)[0])
                driver = far(base + 0xac)
                current = far(base + 0xa0)
                data = far(base + 0xa8)
                vector = far(8*4)
                report['og_runtime'] = dict(data_base=hex(base), driver_far=hex(struct.unpack_from('<I', memory, base+0xac)[0]),
                    driver_bytes=memory[driver:driver+12].hex(), irq0_linear=hex(vector),
                    irq0_normalized_ghidra_linear=hex(vector-(base-0x32550)),
                    current_sound_linear=hex(current), current_sound_header=memory[current:current+36].hex(),
                    next_sample_linear=hex(data), next_sample_bytes=memory[data:data+32].hex(),
                    dsp_write_port=hex(struct.unpack_from('<H', memory, base+0xd5e)[0]))
            wait_record(max(0, seconds-3))
            report['irq_trajectory'] = trace
            active = [point for point in trace if point['active']]
            if len(active)>2:
                first, last = active[0], active[-1]
                elapsed = last['capture_elapsed_seconds']-first['capture_elapsed_seconds']
                effective = (last['delivered_irq_count']-first['delivered_irq_count'])/elapsed
                report['irq_rate_measurement'] = dict(host_seconds=elapsed,
                    delivered_irqs=last['delivered_irq_count']-first['delivered_irq_count'],
                    effective_hz=effective, ideal_pit_hz=1193182/74,
                    relative_difference=effective/(1193182/74)-1,
                    boundary='Observed guest IRQ counter versus host monotonic interval, fixed 60000 cycles; HTTP/scheduling uncertainty remains')
            report['capture_stop'] = api.call('capture/audio/stop', method='POST')
            report['cpu_after'] = api.call('cpu/state')
            report['program_after'] = api.call('program/state')
            report['console_after'] = api.call('video/text')
        finally:
            if patch is not None and process.poll() is None:
                api.write(patch[0], patch[1])
                report['consumer_hold']['restored'] = api.read(patch[0], len(patch[1])) == patch[1]
                assert report['consumer_hold']['restored']
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
    report['source_sha256_after'] = {p.name: sha(p) for p in source.iterdir() if p.is_file()}
    assert before == report['source_sha256_after'], 'Original files changed'
    files = list(capture.glob('*.wav'))
    assert len(files) == 1, 'Expected one recorded WAV: ' + repr(files)
    wav = files[0]
    with wave.open(str(wav), 'rb') as stream:
        report['wav'] = dict(file=wav.relative_to(ROOT).as_posix(), sha256=sha(wav),
            channels=stream.getnchannels(), width=stream.getsampwidth(), rate=stream.getframerate(),
            frames=stream.getnframes(), seconds=stream.getnframes() / stream.getframerate())
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(profile, report['wav'], flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', choices=PROFILES, action='append')
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--port', type=int, default=18763)
    parser.add_argument('--hold-og', action='store_true')
    parser.add_argument('--label')
    args = parser.parse_args()
    for index, profile in enumerate(args.profile or PROFILES):
        run(profile, args.seconds, args.port + index, args.hold_og, args.label)


if __name__ == '__main__':
    main()
