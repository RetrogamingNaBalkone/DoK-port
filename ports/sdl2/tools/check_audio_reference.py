"""Compare actual DOS title recordings with rendered port PCM, after alignment.

Pure Python; no optional scientific dependencies. DOSBox's mixer/resampler and
gain differ from the port's nearest hold, so correlation is reported separately
from the native instruction-policy oracle. No bit-exact analog claim is made.
"""
import array
import argparse
import cmath
import hashlib
import json
import math
import statistics
from pathlib import Path
import wave

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp13-audio'


def read(path):
    with wave.open(str(path), 'rb') as stream:
        assert stream.getsampwidth() == 2 and stream.getframerate() == 48000
        channels = stream.getnchannels()
        samples = array.array('h', stream.readframes(stream.getnframes()))
    return [sum(samples[i:i + channels]) / channels for i in range(0, len(samples), channels)]


def fft(values, inverse=False):
    n = len(values)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            values[i], values[j] = values[j], values[i]
    length = 2
    while length <= n:
        root = cmath.exp((2j if inverse else -2j) * math.pi / length)
        for start in range(0, n, length):
            factor = 1 + 0j
            for at in range(start, start + length // 2):
                a, b = values[at], values[at + length // 2] * factor
                values[at], values[at + length // 2] = a + b, a - b
                factor *= root
        length *= 2
    if inverse:
        for i in range(n):
            values[i] /= n


def average(values, step):
    return [sum(values[i:i + step]) / step for i in range(0, len(values) - step + 1, step)]


def fit(reference, actual):
    n = len(actual)
    sx, sy = sum(reference), sum(actual)
    sxx, syy = sum(x*x for x in reference), sum(y*y for y in actual)
    sxy = sum(x*y for x, y in zip(reference, actual))
    covariance = sxy - sx*sy/n
    vx, vy = sxx - sx*sx/n, syy - sy*sy/n
    correlation = covariance / math.sqrt(vx*vy) if vx > 0 and vy > 0 else 0
    gain = covariance / vx if vx > 0 else 0
    offset = (sy - gain*sx)/n
    residual = math.sqrt(sum((y - gain*x - offset)**2 for x, y in zip(reference, actual))/n)
    return correlation, gain, offset, residual


def align(reference, actual):
    # Limit to a clearly non-silent three-second excerpt. Preserve its offset in
    # the original WAV and search the full port title for the matching segment.
    blocks = [sum(abs(x) for x in actual[i:i+480]) / 480 for i in range(0, len(actual)-479, 480)]
    peak = max(blocks)
    assert peak > 1, 'Silent DOS capture'
    first = next(i for i, level in enumerate(blocks) if level > max(2, peak*.04)) * 480
    last = (len(blocks) - next(i for i, level in enumerate(reversed(blocks)) if level > max(2, peak*.04))) * 480
    start = first + min(4800, (last-first)//10)
    length = min(144000, last-start)
    assert length >= 24000, 'Insufficient non-silent captured audio'
    actual = actual[start:start+length]
    r, d = average(reference, 96), average(actual, 96)
    n = len(d)
    size = 1
    while size < len(r)+n-1:
        size *= 2
    a = list(map(complex, r)) + [0j]*(size-len(r))
    b = list(map(complex, reversed(d))) + [0j]*(size-n)
    fft(a); fft(b)
    for i in range(size):
        a[i] *= b[i]
    fft(a, True)
    prefix, squares = [0.0], [0.0]
    for value in r:
        prefix.append(prefix[-1]+value)
        squares.append(squares[-1]+value*value)
    sy, vy = sum(d), sum(y*y for y in d)-sum(d)**2/n
    best = (-2.0, 0)
    for at in range(len(r)-n+1):
        sx = prefix[at+n]-prefix[at]
        vx = squares[at+n]-squares[at]-sx*sx/n
        corr = (a[at+n-1].real-sx*sy/n)/math.sqrt(vx*vy) if vx > 0 else 0
        if corr > best[0]:
            best = corr, at
    coarse = best[1]*96
    actual_fine = average(actual, 12)
    candidates = []
    for at in range(max(0, coarse-192), min(len(reference)-length, coarse+192)+1):
        result = fit(average(reference[at:at+length], 12), actual_fine)
        candidates.append((result[0], at, result))
    score, position, result = max(candidates)
    return dict(dos_start_frame=start, port_start_frame=position, compared_frames=length,
        compared_seconds=length/48000, coarse_correlation=best[0], correlation=result[0],
        fitted_gain=result[1], fitted_dc=result[2], rms_residual=result[3],
        comparison='4kHz low-pass block averages; alignment searched, gain/DC fitted')


def local_windows(reference, actual, global_alignment):
    ra = average(reference, 12)
    prefix, squares = [0.0], [0.0]
    for value in ra:
        prefix.append(prefix[-1]+value)
        squares.append(squares[-1]+value*value)
    delay = (global_alignment['dos_start_frame']-global_alignment['port_start_frame'])/48000
    first = global_alignment['dos_start_frame']/48000+.05
    last = min(len(actual)/48000-.2, delay+len(reference)/48000-.3)
    rows = []
    for window in range(16):
        seconds = first+(last-first)*window/15
        start = int(seconds*48000)
        d = average(actual[start:start+4800], 12)
        n, sy = len(d), sum(d)
        vy = sum(y*y for y in d)-sy*sy/n
        if vy < 1:
            continue
        center = int((seconds-delay)*4000)
        best = (-2.0, 0)
        for at in range(max(0, center-1000), min(len(ra)-n, center+1000)):
            sx = prefix[at+n]-prefix[at]
            vx = squares[at+n]-squares[at]-sx*sx/n
            covariance = sum(ra[at+i]*d[i] for i in range(n))-sx*sy/n
            corr = covariance/math.sqrt(vx*vy) if vx > 0 else 0
            if corr > best[0]:
                best = corr, at
        # Refine the fractional position to one 48kHz sample, independently for
        # each 100ms window. This exposes drift rather than fitting one global
        # shift to a recording with a different clock.
        candidates = []
        for at in range(max(0, best[1]*12-12), min(len(reference)-4800, best[1]*12+12)+1):
            result = fit(average(reference[at:at+4800], 12), d)
            candidates.append((result[0], at, result))
        corr, at, result = max(candidates)
        rows.append(dict(dos_seconds=start/48000, port_seconds=at/48000,
                         lag_seconds=(start-at)/48000, correlation=corr,
                         fitted_gain=result[1], fitted_dc=result[2], rms_residual=result[3]))
    good = [row for row in rows if row['correlation']>.9]
    x, y = [row['dos_seconds'] for row in good], [row['port_seconds'] for row in good]
    sx, sy = sum(x), sum(y)
    slope = ((sum(a*b for a, b in zip(x, y))-sx*sy/len(x)) /
             (sum(a*a for a in x)-sx*sx/len(x))) if len(x)>1 else 0
    return dict(windows=rows, window_seconds=.1, median_correlation=statistics.median(row['correlation'] for row in rows),
                min_correlation=min(row['correlation'] for row in rows), matching_windows=len(good),
                total_windows=len(rows), fitted_clock_ratio=slope,
                first_lag_seconds=rows[0]['lag_seconds'], last_lag_seconds=rows[-1]['lag_seconds'],
                boundary='Local gain/DC and alignment fits diagnose waveform and clock separately; no output compensation applied')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--strict', action='store_true', help='Fail unless one global shift matches every title excerpt above .9 NCC')
    args = parser.parse_args()
    report = dict(profiles={}, boundary='Three title excerpts recorded from actual DOS SB emulation, compared with port PCM; no whole-game/all-event or analog hardware equivalence')
    for profile in ['ru', 'en', 'old-gold']:
        capture = 'dos-held-trace' if profile == 'old-gold' else 'dos-title'
        dos_report = json.loads((OUT / capture / profile / 'report.json').read_text())
        dos = ROOT / dos_report['wav']['file']
        port = OUT / 'pcm' / profile / 'title.wav'
        reference, actual = read(port), read(dos)
        result = align(reference, actual)
        result['local'] = local_windows(reference, actual, result)
        if profile == 'old-gold':
            result['runtime_irq_rate'] = dos_report.get('irq_rate_measurement')
            result['sb_irq_verified'] = dos_report['og_runtime']['irq0_normalized_ghidra_linear']=='0x2b487'
        result.update(dos_sha256=hashlib.sha256(dos.read_bytes()).hexdigest(),
                      port_sha256=hashlib.sha256(port.read_bytes()).hexdigest())
        report['profiles'][profile] = result
        print(profile, 'global NCC', result['correlation'], 'local median', result['local']['median_correlation'],
              'clock ratio', result['local']['fitted_clock_ratio'], flush=True)
    report['global_clock_equivalence'] = min(p['correlation'] for p in report['profiles'].values())>.9
    report['local_waveform_confirmation'] = all(p['local']['median_correlation']>.9 for p in report['profiles'].values())
    (OUT / 'wave-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    # A low score is an unresolved difference, never relabelled as bit-exact PASS.
    assert report['local_waveform_confirmation'], 'Local waveforms require further investigation'
    if args.strict:
        assert report['global_clock_equivalence'], 'One global shift does not remove the recorded hardware/mixer clock drift'


if __name__ == '__main__':
    main()
