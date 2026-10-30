"""Compare actual emulator PCM with independently extracted original SVM bytes.

The output has passed through the DS mixer and the core's resampler. Correlation
therefore measures waveform agreement, not byte equality or physical DS audio.
No runtime sound implementation or generated audio package is imported.
"""
from __future__ import annotations

import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import wave

ROOT = Path(__file__).resolve().parents[3]


def original_clip(language: int, resource_index: int) -> dict:
    label = {1: "ru", 2: "en"}[language]
    inventory = json.loads((ROOT / f"analysis/originals/inventory-{label}.json").read_text("utf8"))
    source = ROOT / inventory["resource"]["file"]
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != inventory["resource"]["sha256"]:
        raise ValueError(f"Original RES hash changed: {source}")
    count = struct.unpack_from("<H", data)[0]
    if not 0 <= resource_index < count:
        raise ValueError("Original resource index outside RES directory")
    record = data[2 + 30 * resource_index:32 + 30 * resource_index]
    size, offset = struct.unpack_from("<II", record, 16)
    raw = data[offset:offset + size]
    if len(raw) != size or len(raw) < 36 or raw[2:5] != b"SVM":
        raise ValueError("Original resource is not a complete SVM block")
    rate, samples = struct.unpack_from("<II", raw, 16)
    if len(raw) != samples + 36 or not samples:
        raise ValueError("Original SVM sample bounds")
    return dict(language=language, resource_index=resource_index,
                name=record[:16].split(b"\0", 1)[0].decode("cp866"),
                nominal_rate=rate, samples=raw[36:], source_sha256=digest,
                pcm_sha256=hashlib.sha256(raw[36:]).hexdigest())


def coefficient(xs, ys):
    n = len(xs)
    if not n or len(ys) != n:
        return None
    sx, sy = sum(xs), sum(ys)
    vx = sum(x*x for x in xs) - sx*sx/n
    vy = sum(y*y for y in ys) - sy*sy/n
    if vx <= 0 or vy <= 0:
        return None
    return (sum(x*y for x, y in zip(xs, ys)) - sx*sy/n) / math.sqrt(vx*vy)


def compare_capture(path: Path, clip: dict, effective_rate: float,
                    repeats: float = 1, search_seconds: float = .25,
                    output_rate: float | None = None) -> dict:
    if effective_rate <= 0 or repeats <= 0:
        raise ValueError("Positive sample rate and repeat count required")
    with wave.open(str(path), "rb") as stream:
        if stream.getnchannels() != 2 or stream.getsampwidth() != 2:
            raise ValueError("Expected actual stereo PCM16 capture")
        wav_rate = stream.getframerate()
        values = array.array("h", stream.readframes(stream.getnframes()))
    # libretro reports a fractional rate (32728.498046875Hz in the pinned
    # core); WAV stores only an integer. Rounding it creates waveform drift
    # across the 43-second title. Preserve the measured AV value for analysis.
    output_rate = float(wav_rate) if output_rate is None else output_rate
    if output_rate <= 0:
        raise ValueError("Positive output rate required")
    if sys.byteorder != "little":
        values.byteswap()
    left, right = values[::2], values[1::2]
    mono = [(a+b)/2 for a, b in zip(left, right)]
    source = clip["samples"]
    total = int(len(source)*repeats)
    # Find the mixer/resampler delay using the first 250ms, sampled at about
    # 600 source positions per second; retain the independent original values.
    early = range(0, min(total, max(1, int(effective_rate*.25))),
                  max(1, int(effective_rate/600)))
    xs = [source[i % len(source)]-128 for i in early]
    positions = [i*output_rate/effective_rate for i in early]
    if not positions or len(mono) <= positions[-1]+1:
        raise ValueError("Capture too short for initial waveform alignment")
    limit = min(round(search_seconds*output_rate), len(mono)-math.ceil(positions[-1])-1)

    def fit(lag):
        ys = [mono[round(lag+p)] for p in positions]
        value = coefficient(xs, ys)
        return value if value is not None else -1

    score, delay = max((fit(lag), lag) for lag in range(0, limit+1, 2))
    # Half-output-sample phases improve comparisons when the source rate and
    # libretro output rate have no integer ratio. They do not alter the WAV.
    score, delay = max((fit(lag/2), lag/2)
                       for lag in range(max(0, round(delay*2)-6),
                                        min(limit*2, round(delay*2)+6)+1))
    step = max(1, total//24000)
    indices = list(range(0, total, step))
    valid = [i for i in indices if round(delay+i*output_rate/effective_rate) < len(mono)]
    full = coefficient([source[i % len(source)]-128 for i in valid],
                       [mono[round(delay+i*output_rate/effective_rate)] for i in valid])
    window_samples = max(1, round(effective_rate*.5))
    windows = []
    for start in range(0, total, window_samples):
        end = min(total, start+window_samples)
        idx = range(start, end, max(1, (end-start)//600))
        idx = [i for i in idx if round(delay+i*output_rate/effective_rate) < len(mono)]
        windows.append(dict(source_start=start, source_end=end,
                            coefficient=coefficient([source[i % len(source)]-128 for i in idx],
                              [mono[round(delay+i*output_rate/effective_rate)] for i in idx])))
    end_position = delay+total*output_rate/effective_rate
    return dict(resource_index=clip["resource_index"], name=clip["name"],
                language=clip["language"], nominal_rate=clip["nominal_rate"],
                effective_rate=effective_rate, source_samples=len(source), repeats=repeats,
                original_res_sha256=clip["source_sha256"], original_pcm_sha256=clip["pcm_sha256"],
                capture_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                output_rate=output_rate, wav_header_rate=wav_rate, output_frames=len(mono),
                peak=max(map(abs, values), default=0),
                rms=math.sqrt(sum(v*v for v in mono)/max(1, len(mono))),
                initial_coefficient=score, whole_coefficient=full,
                compared_positions=len(valid), expected_positions=len(indices),
                delay_frames=delay, delay_seconds=delay/output_rate,
                expected_duration_seconds=total/effective_rate,
                complete_source_covered=end_position <= len(mono)+1,
                channel_correlation=coefficient(left, right), windows=windows,
                scope="Actual emulated DS mixer/core-resampled waveform; not byte equality, listening, Sound Blaster equivalence or physical DS capture")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--language", type=int, choices=(1, 2), default=1)
    parser.add_argument("--resource", type=int, required=True)
    parser.add_argument("--rate", type=float, required=True)
    parser.add_argument("--repeats", type=float, default=1)
    parser.add_argument("--output-rate", type=float)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    result = compare_capture(args.wav, original_clip(args.language, args.resource), args.rate, args.repeats,
                             output_rate=args.output_rate)
    output = json.dumps(result, ensure_ascii=False, indent=2)+"\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, "utf8")
    print(output)


if __name__ == "__main__":
    main()
