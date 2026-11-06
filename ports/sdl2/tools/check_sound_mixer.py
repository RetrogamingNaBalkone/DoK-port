"""Measured software mixer boundary for every actual DOS waveform capture.

Digital samples/rates are independently proven by check_native_sound_pcm.py.
Here alignment, gain and DC fits diagnose the *emulator mixer*; they never
modify port output. A finite tolerance replaces an unsupported bit-exact claim.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

from check_assets import read_archive
from check_audio_reference import read, align, average, fit
from capture_sound_matrix import PROFILES
from native_sound import ROOT, OUT, sha


def expected_wave(profile, payload, period, seconds=12):
    source = payload[36:]
    if profile == 'old-gold':source = source.split(b'\0')[0]
    assert source
    clock = 1193182 if profile == 'old-gold' else 1000000
    cycle = len(source) + (1 if profile == 'old-gold' else 0)
    result = []
    for index in range(seconds * 48000):
        at = (index * clock // (period * 48000)) % cycle
        value = source[at] if at < len(source) else source[-1]
        result.append((value - 128) * 256)
    return result


def windows(reference, actual, aligned):
    ra = average(reference, 12)
    delay = (aligned['dos_start_frame'] - aligned['port_start_frame']) / 48000
    first = aligned['dos_start_frame'] / 48000 + .05
    last = min(len(actual) / 48000 - .15, first + 2.0)
    rows = []
    for window in range(16):
        seconds = first + (last - first) * window / 15
        at = int(seconds * 48000)
        d = average(actual[at:at + 4800], 12)
        if len(d) != 400:continue
        n, sy = len(d), sum(d)
        vy = sum(value * value for value in d) - sy * sy / n
        if vy < 100:continue
        center = int((seconds - delay) * 4000)
        coarse = []
        for position in range(max(0, center - 128), min(len(ra) - n, center + 128)):
            r = ra[position:position + n]
            sx = sum(r);vx = sum(value * value for value in r) - sx * sx / n
            if vx <= 0:continue
            correlation = (sum(x * y for x, y in zip(r, d)) - sx * sy / n) / math.sqrt(vx * vy)
            coarse.append((correlation, position))
        candidates = []
        # A sub-millisecond, repeating waveform can alias in the coarse 4kHz
        # phase search. Refine its eight best phases, not just one coarse peak.
        positions = set(position for _, peak in sorted(coarse,reverse=True)[:8]
                        for position in range(max(0,peak*12-12),min(len(reference)-4800,peak*12+12)+1))
        for position in positions:
            result = fit(average(reference[position:position + 4800], 12), d)
            candidates.append((result[0], position, result))
        assert candidates
        corr, position, result = max(candidates)
        rows.append(dict(dos_seconds=at / 48000, native_seconds=position / 48000,
                         lag_seconds=(at - position) / 48000, correlation=corr,
                         gain=result[1], dc=result[2], rms=result[3],
                         normalized_rms=result[3] / math.sqrt(vy / n)))
    assert len(rows) >= 8, len(rows)
    good = [row for row in rows if row['correlation'] >= .9]
    slope = None
    if len(good) > 2:
        x = [row['dos_seconds'] for row in good];y = [row['native_seconds'] for row in good]
        mx, my = statistics.mean(x), statistics.mean(y)
        slope = sum((a-mx)*(b-my) for a,b in zip(x,y)) / sum((a-mx)**2 for a in x)
    return dict(count=len(rows), windows=rows, median_ncc=statistics.median(row['correlation'] for row in rows),
                minimum_ncc=min(row['correlation'] for row in rows), median_gain=statistics.median(row['gain'] for row in rows),
                median_dc=statistics.median(row['dc'] for row in rows),
                median_normalized_rms=statistics.median(row['normalized_rms'] for row in rows),
                effective_native_per_dos_clock_ratio=slope)


def main():
    parser = argparse.ArgumentParser();parser.add_argument('--partial', action='store_true');args = parser.parse_args()
    digital = json.loads((OUT / 'native-pcm-report.json').read_text())
    periods = {(x['profile'], x['resource']): x['native_period'] for x in digital['sounds']}
    cache_path = OUT / 'mixer-report.json'
    old = json.loads(cache_path.read_text())['captures'] if cache_path.exists() else []
    cache = {(r['profile'], r['resource'], r['wav_sha256']):r for r in old}
    results = []
    for profile, (folder, _, resource) in PROFILES.items():
        sounds = [(name, raw) for name, raw in read_archive((ROOT / 'originals' / folder / resource).read_bytes()) if raw[2:5] == b'SVM']
        for index, (name, payload) in enumerate(sounds):
            case = OUT / 'dos-matrix' / profile / f'{index:02}-{name.hex()}'
            if not (case / 'report.json').exists():
                assert args.partial, str(case)
                continue
            capture = json.loads((case / 'report.json').read_text())
            key = profile, name.decode('cp866'), capture['wav']['sha256']
            if key in cache and cache[key].get('alignment_version')==2:
                results.append(cache[key]);continue
            reference = expected_wave(profile, payload, periods[(profile, name.decode('cp866'))])
            actual = read(ROOT / capture['wav']['file'])
            assert sha(ROOT / capture['wav']['file']) == capture['wav']['sha256']
            aligned = align(reference, actual)
            local = windows(reference, actual, aligned)
            result = dict(profile=profile, resource=name.decode('cp866'), wav_sha256=capture['wav']['sha256'],alignment_version=2,
                          payload_sha256=hashlib.sha256(payload).hexdigest(), alignment=aligned, local=local,
                          passes_waveform_tolerance=local['median_ncc'] >= .9)
            results.append(result)
            print(profile, result['resource'], 'NCC', local['median_ncc'], 'clock', local['effective_native_per_dos_clock_ratio'], flush=True)
            cache_path.write_text(json.dumps(dict(captures=results, partial=True), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    report = dict(count=len(results), captures=results, partial=args.partial,
                  bit_exact_mixer_equivalence=False,
                  tolerance='4 kHz block-average waveform, >=8 measured 100ms windows, median NCC >=0.9. Gain/DC/clock fits are diagnostics only; no compensation applied to port output.',
                  native_digital_exact=all(r['mismatches'] == 0 for r in digital['sounds']),
                  failed=[(r['profile'],r['resource']) for r in results if not r['passes_waveform_tolerance']])
    cache_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if not args.partial:assert len(results) == 56
    assert not report['failed'], report['failed']


if __name__ == '__main__':main()
