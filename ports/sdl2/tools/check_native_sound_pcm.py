"""Full original SB start/DMA refill/IRQ/stop execution, all 56 RES sounds."""
import array
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import wave

from check_assets import read_archive
from native_sound import NativeSound, OUT, ROOT, sha

PROFILES = [('old-gold', 'originals/Old_Gold_3D/OLD_GOLD.RES'),
            ('ru', 'originals/pkr/RUS/PKR.RES'), ('en', 'originals/pkr/ENG/PKE.RES')]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    originals = [p['sample'] for p in inventory] + [p for _, p in PROFILES]
    before = {p: sha(ROOT / p) for p in originals}
    command = [str(ROOT / 'build/sdl2/windows/dok_audio_probe.exe'), str(OUT / 'port-pcm'),
               *[str(ROOT / p) for _, p in PROFILES]]
    probe = subprocess.run(command, capture_output=True, text=True, timeout=90)
    (OUT / 'pcm-probe.log').write_text(probe.stdout + probe.stderr)
    assert probe.returncode == 0, probe.stderr
    report = []
    for profile, res in PROFILES:
        count = 0
        for name, raw in read_archive((ROOT / res).read_bytes()):
            if raw[2:5] != b'SVM':
                continue
            samples = raw[36:]
            frequency = struct.unpack_from('<I', raw, 16)[0]
            native = NativeSound(profile)
            native.install(1, samples, frequency)
            actual = native.render_source()
            expected = samples.split(b'\0')[0] if profile == 'old-gold' else samples
            assert actual == expected, (profile, name, len(actual), len(expected))
            if profile == 'old-gold':
                pit = [value for port, size, value in native.io if port == 0x40]
                assert len(pit) >= 2 and pit[1] == 0
                period, clock = pit[0] or 65536, 1193182
            else:
                assert native.dsp[:3] == [0x40, native.dsp[1], 0xd1]
                period, clock = (-native.dsp[1]) & 255 or 256, 1000000
            filename = 'title.wav' if name == 'ЗАСТАВКА_ЗВУК'.encode('cp866') else f'sound-{count}.wav'
            with wave.open(str(OUT / 'port-pcm' / profile / filename), 'rb') as stream:
                assert stream.getframerate() == 48000 and stream.getsampwidth() == 2 and stream.getnchannels() == 1
                pcm = array.array('h', stream.readframes(stream.getnframes()))
            mismatches = 0
            for index, value in enumerate(pcm):
                source = index * clock // (period * 48000)
                wanted = (actual[source] - 128) * 256 if source < len(actual) else 0
                mismatches += value != wanted
            assert mismatches == 0, (profile, name, mismatches)
            item = dict(profile=profile, resource=name.decode('cp866'), source_samples=len(samples),
                        emitted_samples=len(actual), dma_packets=len(native.packets), instructions=native.instructions,
                        original_executed_ranges=native.loaded, external_platform_services=native.platform,
                        native_period=period, source_clock=clock, pcm_frames=len(pcm), mismatches=mismatches,
                        pcm_sha256=hashlib.sha256(actual).hexdigest())
            report.append(item)
            count += 1
            print(profile, name.decode('cp866'), len(actual), 'native samples', native.instructions, 'instructions PASS', flush=True)
    assert len(report) == 56
    after = {p: sha(ROOT / p) for p in originals}
    assert before == after
    (OUT / 'native-pcm-report.json').write_text(json.dumps(dict(
        count=len(report), sounds=report, sha256_before=before, sha256_after=after,
        boundary='Actual original Sound wrappers/admission/full SB driver/DMA copying/refill/IRQ; only port-readiness and DOS vector services modeled. Digital output before mixer; inactive DAC -> SDL silence is intentional.'
    ), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
