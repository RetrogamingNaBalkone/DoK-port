"""Actual DOS SB/mixer captures for every original SVM waveform.

An isolated RES fixture redirects only the title binding to an unchanged original
SVM payload. EXEs and source originals are never changed. OG's main consumer is
temporarily held before its stop call; IRQ/DAC/PCM/timer code is untouched.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time
import wave

from capture_audio_reference import Api
from check_assets import read_archive
from native_sound import OUT, ROOT, sha

PROFILES = {'old-gold': ('Old_Gold_3D', 'OG', 'OLD_GOLD.RES'),
            'ru': ('pkr/RUS', 'PKR', 'PKR.RES'), 'en': ('pkr/ENG', 'PKE', 'PKE.RES')}
TITLE = 'ЗАСТАВКА_ЗВУК'.encode('cp866')


def rebind_title(original, payload):
    if payload==dict(read_archive(original))[TITLE]:return original
    old_title=dict(read_archive(original))[TITLE]
    # The protected-mode startup consumes its title while loading graphics.
    # Keep its original allocation/length and repeat the source waveform, so
    # even a short effect survives startup. All repeated PCM bytes are original.
    if old_title[:2]!=b'\x4d\x01':
        length=len(old_title)-36;source=payload[36:];header=bytearray(payload[:36]);struct.pack_into('<I',header,20,length)
        repeated=bytes(header)+(source*((length+len(source)-1)//len(source)))[:length]
        result=bytearray(original);count=struct.unpack_from('<H',original)[0]
        for index in range(count):
            at=2+index*30
            if original[at:at+16].split(b'\0')[0]==TITLE:
                size,offset=struct.unpack_from('<II',original,at+16);assert size==len(repeated);result[offset:offset+size]=repeated;return bytes(result)
    count = struct.unpack_from('<H', original)[0]
    result=bytearray(original)
    for index in range(count):
        at=2+index*30
        name=original[at:at+16].split(b'\0')[0]
        if name==TITLE:
            struct.pack_into('<II',result,at+16,len(payload),len(original));result.extend(payload);break
    else:raise RuntimeError('No title binding')
    result=bytes(result)
    old = dict(read_archive(original)); new = dict(read_archive(result))
    assert old.keys() == new.keys() and new[TITLE] == payload
    assert all(old[name] == new[name] for name in old if name != TITLE)
    return result


def capture(profile, resource_index, name, payload, port):
    folder, exe_name, res_name = PROFILES[profile]
    source = ROOT / 'originals' / folder
    original = (source / res_name).read_bytes()
    root = OUT / 'dos-matrix' / profile
    case = root / f'{resource_index:02}-{name.hex()}'
    report_path = case / 'report.json'
    if report_path.exists():
        report = json.loads(report_path.read_text())
        if not report.get('final_program',{}).get('canonical_name',''):
            report_path.rename(case/'invalid-no-native-program.json')
            return capture(profile,resource_index,name,payload,port)
        assert report['payload_sha256'] == hashlib.sha256(payload).hexdigest()
        assert sha(ROOT / report['wav']['file']) == report['wav']['sha256']
        return report
    case.mkdir(parents=True, exist_ok=True)
    game = root / 'game'; game.mkdir(exist_ok=True)
    for path in source.iterdir():
        if path.is_file():
            shutil.copy2(path, game / path.name)
    fixture = rebind_title(original, payload)
    (game / res_name).write_bytes(fixture)
    (game / 'SP13WAIT.COM').write_bytes(bytes.fromhex('b80086b91e00ba8084cd15b8004ccd21'))
    capture_dir = case / 'capture-valid' / str(time.time_ns()); capture_dir.mkdir(parents=True,exist_ok=True)
    local = case / 'local'; local.mkdir(exist_ok=True)
    config = root / 'dosbox.conf'
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
capture_dir={capture_dir.as_posix()}
[webserver]
webserver_enabled=true
webserver_bind_address=127.0.0.1
webserver_port={port}
[autoexec]
mount c "{game.as_posix()}"
c:
SP13WAIT.COM
{exe_name}.EXE -b
''', encoding='utf-8')
    emulator = Path(os.environ['LOCALAPPDATA']) / 'Programs/DOSBox-Automation/0.85.1/dosbox-automation-0.85.1/dosbox.exe'
    token = hashlib.sha256(f'sp13-matrix-{profile}-{resource_index}'.encode()).hexdigest()
    env = dict(os.environ, LOCALAPPDATA=str(local), SDL_VIDEODRIVER='dummy',
               SDL_AUDIODRIVER='dummy', DOSBOX_API_TOKEN=token)
    api = Api(port, token)
    report = dict(profile=profile, resource=name.decode('cp866'), resource_hex=name.hex(),
                  payload_sha256=hashlib.sha256(payload).hexdigest(), fixture_res_sha256=hashlib.sha256(fixture).hexdigest(),
                  original_exe_sha256=sha(source / (exe_name + '.EXE')), emulator_sha256=sha(emulator),
                  audio_algorithms_unchanged=True, executable_file_unchanged=True, binding_fixture='Only title SVM payload changed: original source PCM bytes; NE repeats waveform to original title length during startup, OG exact source SVM')
    patch = None
    with (case / 'dosbox.log').open('w', encoding='utf-8') as log:
        process = subprocess.Popen([str(emulator), '--noprimaryconf', '--nolocalconf', '-conf', str(config), '-noconsole'],
            env=env, cwd=case, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError('DOSBox exited before startup')
                try:
                    report['emulator'] = api.call('dosbox/info'); break
                except (OSError, ValueError):
                    time.sleep(.05)
            else:
                raise TimeoutError('API did not start')
            api.call('capture/audio/start', method='POST')
            started = time.monotonic()
            if profile == 'old-gold':
                for _ in range(250):
                    memory = api.read(0, 0x100000)
                    anchor = b'Not same device\0'
                    report['anchor_count']=memory.count(anchor)
                    if memory.count(anchor) == 1:
                        base = memory.find(anchor) - 0x189d
                        address = 0x20c40 + 0xf9 + base - 0x32550
                        expected = api.read(address, 5)
                        assert expected[:3] == bytes.fromhex('9a5d02')
                        api.write(address, bytes.fromhex('ebfe909090'));patch = address, expected
                        report['consumer_hold'] = dict(address_system='Runtime real-mode linear, Ghidra MZ20c4:00f9', address=address,
                                                       original=expected.hex(), replacement='ebfe909090')
                        break
                    time.sleep(.02)
                else:
                    raise TimeoutError('OG anchor not loaded')
            if profile!='old-gold':
                for _ in range(600):
                    status=api.call('video/text')
                    if not status['is_text_mode']:break
                    time.sleep(.05)
                else:raise TimeoutError('Native title did not enter graphics')
                started=time.monotonic()
            samples = payload[36:]
            frequency = struct.unpack_from('<I', payload, 16)[0]
            effective_samples = len(samples.split(b'\0')[0]) if profile == 'old-gold' else len(samples)
            duration = min(9.0, max(4.5 if profile=='old-gold' else 2.0, effective_samples / max(1, frequency) + (2.7 if profile=='old-gold' else .5)))
            while time.monotonic() - started < duration:
                time.sleep(.05)
            report['capture_stop'] = api.call('capture/audio/stop', method='POST')
            report['cpu'] = api.call('cpu/state')
        finally:
            try:
                report['final_program']=api.call('program/state');report['final_console']=api.call('video/text');report['final_cpu']=api.call('cpu/state')
            except Exception as error:report['diagnostic_error']=repr(error)
            (case/'attempt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            if patch is not None and process.poll() is None:
                api.write(patch[0], patch[1])
                report['consumer_hold']['restored'] = api.read(patch[0], len(patch[1])) == patch[1]
                assert report['consumer_hold']['restored']
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            (game / res_name).write_bytes(original)
    files = list(capture_dir.glob('*.wav'))
    assert len(files) == 1, files
    with wave.open(str(files[0]), 'rb') as stream:
        report['wav'] = dict(file=files[0].relative_to(ROOT).as_posix(), sha256=sha(files[0]), rate=stream.getframerate(),
                             channels=stream.getnchannels(), width=stream.getsampwidth(), frames=stream.getnframes())
    assert report['final_program']['canonical_name'].upper().endswith(exe_name+'.EXE'),report['final_console']
    import array
    with wave.open(str(files[0]),'rb') as stream:pcm=array.array('h',stream.readframes(stream.getnframes()))
    assert max(pcm)-min(pcm)>16,'Silent DOS capture'
    report['captured_peak_to_peak']=max(pcm)-min(pcm)
    assert report['wav']['rate'] == 48000 and report['wav']['width'] == 2 and report['wav']['frames'] > 48000
    assert sha(game / (exe_name + '.EXE')) == report['original_exe_sha256']
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser();parser.add_argument('--profile', action='append', choices=PROFILES)
    parser.add_argument('--limit', type=int);parser.add_argument('--port', type=int, default=28841)
    args = parser.parse_args()
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    files = [ROOT / p['sample'] for p in inventory] + [ROOT / 'originals' / f / r for f, _, r in PROFILES.values()]
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in files}
    reports = []
    for profile in args.profile or PROFILES:
        folder, _, res = PROFILES[profile]
        sounds = [(name, raw) for name, raw in read_archive((ROOT / 'originals' / folder / res).read_bytes()) if raw[2:5] == b'SVM']
        for index, (name, raw) in enumerate(sounds[:args.limit] if args.limit else sounds):
            report = capture(profile, index, name, raw, args.port)
            reports.append(report)
            print(profile, index, name.decode('cp866'), 'captured', report['wav']['frames'], flush=True)
    after = {p: sha(ROOT / p) for p in before}
    assert before == after
    (OUT / 'dos-matrix-report.json').write_text(json.dumps(dict(captures=reports, count=len(reports), original_sha256_before=before,
        original_sha256_after=after, scope='Actual DOS original resource loader/SB driver/emulated mixer, original SVM waveform via controlled title binding'),
        ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
