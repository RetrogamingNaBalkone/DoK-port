"""SP13 native policy, SDL state, full PCM, and actual DOS recording checks.

Captures are retained evidence inputs; see capture_audio_reference.py to create
new recordings. This command does not launch or alter an original DOS session.
"""
import array
import hashlib
import json
import os
from pathlib import Path
import struct
import re
import subprocess
import sys
import wave
from check_assets import read_archive
from check_audio_oracle import native_period

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp13-audio'
RES = [('old-gold', 'originals/Old_Gold_3D/OLD_GOLD.RES'),
       ('ru', 'originals/pkr/RUS/PKR.RES'), ('en', 'originals/pkr/ENG/PKE.RES')]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    originals = [ROOT / p['sample'] for p in inventory]+[ROOT/path for profile, path in RES]
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in originals}
    report = dict(commands=[], pcm=[], original_sha256_before=before)
    commands = [[str(ROOT/'build/sdl2/windows/dok_audio_tests.exe')],
                [str(ROOT/'build/sdl2/windows/dok_audio_probe.exe'), str(OUT/'pcm'), *[str(ROOT/p) for profile, p in RES]],
                [sys.executable, '-B', str(ROOT/'ports/sdl2/tools/check_audio_oracle.py')],
                [sys.executable, '-B', str(ROOT/'ports/sdl2/tools/check_audio_reference.py')]]
    for index, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (OUT/f'check-{index}.log').write_text(result.stdout+result.stderr, encoding='utf-8')
        report['commands'].append(dict(command=command, exit_code=result.returncode))
        print(result.stdout.strip(), flush=True)
        if result.returncode:
            raise RuntimeError('Audio check failed; inspect '+str(OUT/f'check-{index}.log'))
    for profile, path in RES:
        count = 0
        for name, raw in read_archive((ROOT/path).read_bytes()):
            if raw[2:5] != b'SVM':
                continue
            filename = 'title.wav' if name == bytes.fromhex('8780919280828a805f8782938a') else f'sound-{count}.wav'
            wav = OUT/'pcm'/profile/filename
            with wave.open(str(wav), 'rb') as stream:
                assert (stream.getframerate(), stream.getnchannels(), stream.getsampwidth()) == (48000, 1, 2)
                actual = array.array('h', stream.readframes(stream.getnframes()))
            frequency = struct.unpack_from('<I', raw, 16)[0]
            period = native_period(profile, frequency)
            clock = 1193182 if profile == 'old-gold' else 1000000
            samples = raw[36:]
            if profile == 'old-gold':
                samples = samples.split(b'\0')[0]
            mismatches = 0
            for i, value in enumerate(actual):
                source = i*clock//(period*48000)
                expected = (samples[source]-128)*256 if source < len(samples) else 0
                mismatches += value != expected
            report['pcm'].append(dict(profile=profile, resource_hex=name.hex(), output_frames=len(actual),
                native_period=period, mismatches=mismatches, wav_sha256=sha(wav)))
            assert mismatches == 0, profile+' '+name.hex()
            count += 1
    report['app_music'] = []
    script = OUT/'music-ui.txt'
    script.write_text('1 key Return\n3 key Return\n7 state music\n')
    for profile, resource in RES:
        directory = OUT/'app-music'/profile
        command = [str(ROOT/'build/sdl2/windows/OGDoK.exe'), '--res', str(ROOT/resource),
                   '--renderer', 'software', '--locale', 'en', '--verbose', '--user-dir', str(directory),
                   '--ui-script', str(script), '--frames', '10']
        result = subprocess.run(command, capture_output=True, text=True,
                                env=dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy'))
        log = (directory/'port.log').read_text(encoding='utf-8')
        music = re.findall(r'^MUSIC dataset=(\d+) loop=(\d+) active=(\d+)$', log, re.M)
        expected = '1'
        assert result.returncode == 0 and music and all(loop == expected and active == '1' for dataset, loop, active in music), log
        assert len(music) == 1, 'Menu transitions must preserve the running title music'
        report['app_music'].append(dict(profile=profile, command=command, exit_code=result.returncode, observed=music))
    report['original_sha256_after'] = {p: sha(ROOT/p) for p in before}
    assert before == report['original_sha256_after']
    report['boundary'] = 'Native instruction policy/rates, full raw digital PCM, actual SDL pause/state, actual DOS title waveforms with documented mixer clock differences; not bit-exact analog hardware or every gameplay event'
    (OUT/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    native = json.loads((OUT/'oracle-report.json').read_text())
    waves = json.loads((OUT/'wave-comparison.json').read_text())
    summary = dict(schema=1, scope=report['boundary'], native_og_vectors=native['count'],
        native_og_mismatches=native['mismatches'], native_rate_vectors=native['rate_count'],
        native_rate_mismatches=native['rate_mismatches'], pcm_sequences=len(report['pcm']),
        pcm_mismatches=sum(p['mismatches'] for p in report['pcm']), app_music_processes=len(report['app_music']),
        local_waveform_confirmation=waves['local_waveform_confirmation'],
        global_clock_equivalence=waves['global_clock_equivalence'],
        local_median_ncc={p:d['local']['median_correlation'] for p,d in waves['profiles'].items()},
        originals_unchanged=before==report['original_sha256_after'],
        full_reports={name:str((OUT/name).relative_to(ROOT)).replace('\\','/') for name in ['report.json','oracle-report.json','wave-comparison.json']},
        report_sha256={name:sha(OUT/name) for name in ['report.json','oracle-report.json','wave-comparison.json']})
    (ROOT/'analysis/source-port/sp13-audio-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('SP13:', len(report['pcm']), 'full PCM sequences; all original hashes unchanged', flush=True)


if __name__ == '__main__':
    main()
