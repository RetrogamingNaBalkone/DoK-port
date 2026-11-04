"""Record deterministic render fixtures and compare runs. Python standard library only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def png(path, width, height, rgba):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def git(*args):
    p = subprocess.run(["git", *args], cwd=ROOT, capture_output=True)
    return p.stdout.decode("utf-8", errors="replace").strip() if p.returncode == 0 else "unavailable"


def record(args):
    executable = args.exe.resolve()
    if not executable.is_file():
        raise ValueError(f"Build dok_render_bench first: {executable}")
    if args.output.exists():
        raise ValueError("Output already exists; baselines are never overwritten")
    resources = [p.resolve() for p in args.res]
    for path in resources:
        if not path.is_file():
            raise ValueError(f"Missing RES: {path}")
    if args.samples < 1 or args.warmup < 1:
        raise ValueError("Samples and warmup must be positive")
    args.output.mkdir(parents=True)
    source_hashes = {str(p): digest(p) for p in resources}
    source_files = sorted((ROOT / "ports/sdl2").rglob("*"))
    source_digest = hashlib.sha256()
    for path in source_files:
        if path.is_file() and path.suffix in (".cpp", ".hpp", ".inc", ".py", ".txt", ".cmake", ".ps1"):
            source_digest.update(path.relative_to(ROOT).as_posix().encode())
            source_digest.update(path.read_bytes())
    report = {"schema": 1, "complete": False, "scope": "Frozen presentation fixtures; no simulation/audio/present timing",
              "host": {"platform": platform.platform(), "machine": platform.machine(), "processor": platform.processor(),
                       "cpu_count": os.cpu_count(), "label": args.label},
              "git_head": git("rev-parse", "HEAD"), "git_status": git("status", "--short"),
              "source_sha256": source_digest.hexdigest(), "exe": str(executable), "exe_sha256": digest(executable),
              "resources": source_hashes, "jobs": []}
    write_json(args.output / "manifest.json", report)
    modes = []
    if args.renderer in ("software", "both"):
        sizes = [(1024, 768)] if args.matrix == "target" else ([(320, 200)] if args.matrix == "quick" else [(320, 200), (640, 400), (640, 480), (800, 600), (1024, 768)])
        modes += [("software", w, h, "nearest") for w, h in sizes]
    if args.renderer in ("gl", "both"):
        sizes = [(1024, 768)] if args.matrix == "target" else ([(640, 480)] if args.matrix == "quick" else [(480, 320), (640, 480)])
        modes += [("gl", w, h, f) for w, h in sizes for f in ("nearest", "linear")]
    datasets = set()
    for source_index, source in enumerate(resources):
        dataset = None
        for backend, w, h, filtering in modes:
            folder = f"res{source_index}-{backend}-{w}x{h}-{filtering}"
            command = [str(executable), str(source), str((args.output / folder).resolve()), args.level,
                       backend, str(w), str(h), filtering, args.suite, str(args.warmup), str(args.samples)]
            if args.profile_submission:
                command.append("profile")
            print(f"Recording {source.name}: {backend} {w}x{h} {filtering}", flush=True)
            with (args.output / (folder + ".log")).open("w", encoding="utf-8") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            native = json.loads((args.output / folder / "native.json").read_text(encoding="utf-8"))
            dataset = native["dataset"]
            if native["build_type"].lower() != "release":
                raise ValueError("Benchmark requires a Release build")
            job = {"key": f"{dataset}-{backend}-{w}x{h}-{filtering}", "folder": folder,
                   "resource_sha256": source_hashes[str(source)], "command": command, "native": native, "captures": {}}
            for case in native["cases"]:
                extensions = ["rgba", "png"] + (["idx"] if backend == "software" else [])
                for extension in extensions:
                    name = case["id"] + "." + extension
                    path = args.output / folder / name
                    if extension != "png":
                        raw = path.read_bytes()
                        if extension == "rgba":
                            # Native PNG uses stored DEFLATE blocks; compact the preview losslessly.
                            png(path.with_suffix(".png"), w, h, raw)
                        packed = zlib.compress(raw)
                        packed_path = path.with_name(path.name + ".zlib")
                        packed_path.write_bytes(packed)
                        if zlib.decompress(packed_path.read_bytes()) != raw:
                            raise ValueError("Capture compression round-trip failed")
                        path.unlink()  # Only the just-created, verified duplicate in this job.
                        path = packed_path
                        name += ".zlib"
                    job["captures"][name] = digest(path)
            report["jobs"].append(job)
            write_json(args.output / "manifest.json", report)
        if dataset in datasets:
            raise ValueError(f"Duplicate dataset: {dataset}")
        datasets.add(dataset)
    if any(digest(Path(p)) != sha for p, sha in source_hashes.items()):
        raise ValueError("Original resource changed during run")
    report["complete"] = True
    write_json(args.output / "manifest.json", report)
    print(f"Recorded {sum(len(j['native']['cases']) for j in report['jobs'])} cases: {args.output / 'manifest.json'}")


def load_run(root):
    data = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    if data.get("schema") != 1 or not data.get("complete") or not data.get("jobs"):
        raise ValueError(f"Incomplete/unsupported run: {root}")
    jobs = {}
    for job in data["jobs"]:
        if job["key"] in jobs:
            raise ValueError("Duplicate job")
        jobs[job["key"]] = job
        native = job["native"]
        cases = native["cases"]
        if not cases or len({c["id"] for c in cases}) != len(cases):
            raise ValueError("Empty or duplicate case list")
        required = {c["id"] + ext for c in cases for ext in ([".rgba.zlib", ".png", ".idx.zlib"] if native["backend"] == "software" else [".rgba.zlib", ".png"])}
        if set(job["captures"]) != required:
            raise ValueError("Capture inventory does not match cases")
        for name, expected in job["captures"].items():
            path = (root / job["folder"] / name).resolve()
            if not path.is_relative_to(root.resolve()):
                raise ValueError("Capture escapes run directory")
            if not path.is_file() or digest(path) != expected:
                raise ValueError(f"Missing/modified capture: {path}")
            channels = 4 if name.endswith(".rgba.zlib") else 1 if name.endswith(".idx.zlib") else None
            if channels and len(zlib.decompress(path.read_bytes())) != native["width"] * native["height"] * channels:
                raise ValueError(f"Wrong capture dimensions: {path}")
    return data, jobs


def pixel_difference(a, b, width, height, channels):
    if len(a) != width * height * channels or len(b) != len(a):
        raise ValueError("Pixel buffer dimensions differ")
    changed = 0
    bounds = [width, height, -1, -1]
    maximum = total = 0
    mask = bytearray(width * height * 4)
    for i in range(width * height):
        offset = i * channels
        aa, bb = a[offset:offset + channels], b[offset:offset + channels]
        if aa != bb:
            changed += 1
            x, y = i % width, i // width
            bounds = [min(bounds[0], x), min(bounds[1], y), max(bounds[2], x), max(bounds[3], y)]
            mask[i * 4:i * 4 + 4] = b"\xff\x00\xff\xff"
        else:
            mask[i * 4:i * 4 + 4] = b"\x00\x00\x00\xff"
        delta = [abs(x - y) for x, y in zip(aa, bb)]
        maximum = max(maximum, max(delta))
        total += sum(delta)
    return {"changed_pixels": changed, "total_pixels": width * height,
            "bbox_inclusive": bounds if changed else None, "max_channel_delta": maximum,
            "mean_channel_delta": total / len(a)}, mask


def compare(args):
    baseline, old = load_run(args.baseline)
    current, new = load_run(args.current)
    if set(old) != set(new):
        raise ValueError("Job sets differ; use identical resources and matrix")
    if args.output.exists():
        raise ValueError("Comparison output already exists")
    # Validate all compatibility before emitting a potentially misleading partial comparison.
    config = ("schema", "fixture_version", "dataset", "backend", "width", "height", "filter", "suite")
    state = ("id", "pose", "offset", "ticks", "door", "phase", "volley", "arena", "shooters", "active_projectiles", "impacts", "presentation_state_fnv64")
    for key in old:
        a, b = old[key], new[key]
        if a["resource_sha256"] != b["resource_sha256"]:
            raise ValueError(f"Resource mismatch: {key}")
        if any(a["native"][k] != b["native"][k] for k in config):
            raise ValueError(f"Configuration mismatch: {key}")
        if bool(a["native"].get("profile_submission", False)) != bool(b["native"].get("profile_submission", False)):
            raise ValueError(f"Timing instrumentation differs: {key}; compare ordinary runs separately")
        ca, cb = a["native"]["cases"], b["native"]["cases"]
        if [{k: c[k] for k in state} for c in ca] != [{k: c[k] for k in state} for c in cb]:
            raise ValueError(f"Fixture state/order mismatch: {key}")
        if a["native"]["backend"] == "gl" and any(a["native"][k] != b["native"][k] for k in ("gl_vendor", "gl_renderer", "gl_version")):
            raise ValueError(f"GL environment differs: {key}; establish a separate baseline on this driver")
    args.output.mkdir(parents=True)
    report = {"schema": 1, "baseline": str(args.baseline.resolve()), "current": str(args.current.resolve()),
              "same_host_metadata": baseline["host"] == current["host"], "pixel_exact": True,
              "baseline_manifest_sha256": digest(args.baseline / "manifest.json"),
              "current_manifest_sha256": digest(args.current / "manifest.json"),
              "timing_note": "Per-case ratios are descriptive, not device predictions or statistical significance", "cases": []}
    write_json(args.output / "baseline-manifest.json", baseline)
    write_json(args.output / "current-manifest.json", current)
    for key in old:
        a, b = old[key], new[key]
        w, h = a["native"]["width"], a["native"]["height"]
        for ca, cb in zip(a["native"]["cases"], b["native"]["cases"]):
            item = {"job": key, "case": ca["id"], "pixels": {}, "timings": {}}
            for ext, channels in ([('rgba', 4), ('idx', 1)] if a["native"]["backend"] == "software" else [('rgba', 4)]):
                name = ca["id"] + "." + ext + ".zlib"
                if a["captures"][name] == b["captures"][name]:
                    item["pixels"][ext] = {"changed_pixels": 0, "total_pixels": w * h}
                    continue
                pa = zlib.decompress((args.baseline / a["folder"] / name).read_bytes())
                pb = zlib.decompress((args.current / b["folder"] / name).read_bytes())
                stats, mask = pixel_difference(pa, pb, w, h, channels)
                item["pixels"][ext] = stats
                if stats["changed_pixels"]:
                    report["pixel_exact"] = False
                    filename = f"diff-{len(report['cases']):05d}-{ext}.png"
                    png(args.output / filename, w, h, mask)
                    stats["diff"] = filename
            for stage in ca["timings_ms"]:
                previous, now = ca["timings_ms"][stage], cb["timings_ms"][stage]
                item["timings"][stage] = {"before_p50_ms": previous["p50"], "after_p50_ms": now["p50"],
                                           "before_p95_ms": previous["p95"], "after_p95_ms": now["p95"],
                                           "p50_ratio": now["p50"] / previous["p50"] if previous["p50"] else None}
            report["cases"].append(item)
    write_json(args.output / "comparison.json", report)
    different = sum(any(p["changed_pixels"] for p in c["pixels"].values()) for c in report["cases"])
    print(f"{len(report['cases'])} cases; {different} changed; report: {args.output / 'comparison.json'}")
    if not report["same_host_metadata"]:
        print("Host metadata differs: timing ratios are not a controlled before/after measurement.")
    for case in sorted(report["cases"], key=lambda c: c["timings"]["total"]["after_p95_ms"], reverse=True)[:5]:
        t = case["timings"]["total"]
        print(f"  {case['job']} / {case['case']}: p50 {t['before_p50_ms']:.3f} -> {t['after_p50_ms']:.3f} ms; "
              f"p95 {t['before_p95_ms']:.3f} -> {t['after_p95_ms']:.3f} ms")
    return 1 if different else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    rec = sub.add_parser("record")
    rec.add_argument("--exe", type=Path, default=ROOT / "build/sdl2/windows/dok_render_bench.exe")
    rec.add_argument("--res", type=Path, action="append", required=True)
    rec.add_argument("--output", type=Path, required=True)
    rec.add_argument("--renderer", choices=("software", "gl", "both"), default="both")
    rec.add_argument("--matrix", choices=("quick", "full", "target"), default="quick", help="target: both backends at 1024x768")
    rec.add_argument("--suite", choices=("quick", "full", "stress"), default="quick")
    rec.add_argument("--level", default="all")
    rec.add_argument("--warmup", type=int, default=10)
    rec.add_argument("--samples", type=int, default=120)
    rec.add_argument("--label", default="", help="Device/firmware/clock/power profile; recorded verbatim")
    rec.add_argument("--profile-submission", action="store_true", help="Extra per-polygon timers in native log; not comparable to ordinary timing runs")
    cmp = sub.add_parser("compare")
    cmp.add_argument("baseline", type=Path)
    cmp.add_argument("current", type=Path)
    cmp.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.action == "record":
            record(args)
            return 0
        return compare(args)
    except (ValueError, OSError, KeyError, zlib.error, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
