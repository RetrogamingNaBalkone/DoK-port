"""Build isolated N07 profiling ROMs and reuse its independent ARM9 observer.

The accepted N07 outputs are inputs only. This does not advance gameplay tasks.
Run baseline BEFORE editing renderer, then optimized with the same inputs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import run_renderer_emulator as observer

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/perf"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(variant, task="n07", audit=False):
    target = OUT / variant
    target.mkdir(parents=True, exist_ok=True)
    (target / "generated").mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "build/nds/n07/generated/render_trig.inc",
                    target / "generated/render_trig.inc")
    msys = Path.home() / ".local/msys64"
    env = dict(os.environ, MSYSTEM="UCRT64", CHERE_INVOKING="1", MSYS2_PATH_TYPE="minimal")
    repo = ROOT.as_posix()
    repo = "/" + repo[0].lower() + repo[2:]
    quoted = "'" + repo.replace("'", "'\\''") + "'"
    prefix = "set -eo pipefail\nsource /opt/wonderful/bin/wf-env\n"
    def shell(script):
        run = subprocess.run([str(msys / "usr/bin/bash.exe"), "--login", "-s"],
                             input=prefix + script + "\n", text=True,
                             capture_output=True, env=env, cwd=ROOT)
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        return run.stdout
    packages = dict(line.split() for line in shell("wf-pacman -Q").splitlines())
    lock = json.loads((ROOT / "ports/nds/toolchain_versions.json").read_text("utf8"))
    assert all(packages.get(k) == v for k, v in lock["packages"].items()), "SDK lock mismatch"
    extra = " RENDER_AUDIT=1" if audit else ""
    output = shell(f"cd {quoted}\nmake -C ports/nds -f Makefile.{task} OUTDIR=../../build/nds/perf/{variant}{extra} all")
    (target / "build.log").write_text(output, "utf8")
    sources = [ROOT / "ports/nds/platform/render_probe/render_backend.c",
               ROOT / "ports/nds/platform/render_probe/render_backend.h",
               ROOT / "ports/nds/render/scene.c", Path(__file__)]
    manifest = dict(packages=packages, sources={p.relative_to(ROOT).as_posix(): digest(p) for p in sources},
                    artifacts={p.name: digest(p) for p in target.glob("dok_nds_*.*")
                               if p.suffix in (".nds", ".elf", ".map")})
    (target / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", "utf8")


def summarize(report, cases):
    hz = report["last_observation"]["timer_hz"]
    refresh = hz / report["last_observation"]["vblank_ticks"]
    results = []
    for case in cases:
        rows = [r for r in report["history_records"] if r["case_id"] == case["id"] and not r["flags"] & (1 | 32 | 64)]
        ticks = sum(r["total_ticks"] for r in rows)
        item = dict(name=case["name"], frames=len(rows),
                    virtual_fps=len(rows) * hz / ticks,
                    vblank_intervals=sorted(set(r["vblank_end"] - r["vblank_start"] for r in rows)),
                    prepare_ms=sum(r["prepare_ticks"] for r in rows) / len(rows) * 1000 / hz,
                    submit_ms=sum(r["submit_ticks"] for r in rows) / len(rows) * 1000 / hz,
                    geometry_wait_ms=sum(r["geometry_wait_ticks"] for r in rows) / len(rows) * 1000 / hz,
                    quads_min=min(r["submitted_quads"] for r in rows),
                    quads_max=max(r["submitted_quads"] for r in rows))
        results.append(item)
    return dict(refresh_hz=refresh, cases=results, passed=report["passed"],
                scope="Emulated ARM9 timer and VBlank; not physical Nintendo DS FPS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=("baseline", "optimized"), required=True)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    if not args.skip_build:
        build(args.variant)
    observer.OUT = OUT / args.variant
    sys.argv = [str(Path(observer.__file__)), "--core", str(args.core),
                "--packet", str(ROOT / "build/nds/n07/scene.dat"),
                "--manifest", str(ROOT / "build/nds/n07/scene-manifest.json")]
    started = time.perf_counter()
    code = observer.main()
    path = observer.OUT / "emulator/renderer/result.json"
    report = json.loads(path.read_text("utf8"))
    if not code:
        cases = json.loads((ROOT / "build/nds/n07/scene-manifest.json").read_text("utf8"))["cases"]
        summary = summarize(report, cases)
        summary["host_wall_seconds"] = time.perf_counter() - started
        summary["host_frontend_fps_including_observer"] = report["frontend_frames"] / summary["host_wall_seconds"]
        (observer.OUT / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", "utf8")
        print(json.dumps(summary, indent=2))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
