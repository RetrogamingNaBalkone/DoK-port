"""Offline GPU feasibility evidence; reads existing N13P reports, runs no ROM/EXE."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]
SDK = Path(os.environ.get("USERPROFILE", "C:/Users/dspev")) / ".local/msys64/opt/wonderful/thirdparty/blocksds/core"
OUT = ROOT / "research/evidence/nds/gpu-static-20261029/report.json"


def pin(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
            "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def load(path: Path) -> dict:
    return json.loads(path.read_text("utf8"))


def main() -> None:
    compact_path = ROOT / "analysis/nds-n13p-performance.json"
    compact = load(compact_path)
    fixture_pin_path = ROOT / "research/evidence/nds/n13p/fixture.json"
    fixture_pin = load(fixture_pin_path)["artifact"]
    fixture_path = ROOT / fixture_pin["path"]
    assert pin(fixture_path)["sha256"] == fixture_pin["sha256"]
    data = fixture_path.read_bytes()
    assert data[:8] == b"N07SCN1\0"
    version, count, stride, level_bytes, total, reserved = struct.unpack_from("<6I", data, 8)
    assert (version, stride, level_bytes, total, reserved) == (1, 64, 10240, len(data), 0)
    assert len(data) == 64 + count * stride + 3 * level_bytes
    assert hashlib.sha256(data[64:]).digest() == data[32:64]
    cases = [struct.unpack_from("<16I", data, 64 + i * stride) for i in range(count)]
    normal_ids = {c[0] for c in cases if c[11] not in (12, 13, 14, 17, 18)}
    inputs = [pin(compact_path), pin(fixture_pin_path), pin(fixture_path)]
    variants = {}
    for label in ("fog-ru", "fog-en"):
        accepted = compact["variants"][label]
        path = ROOT / accepted["report"]["path"]
        assert pin(path) == accepted["report"], f"Changed accepted report: {path}"
        inputs.append(pin(path))
        report = load(path)
        assert report["passed"] and report["fat_unchanged"]
        hz = report["last_observation"]["timer_hz"]
        rows = [r for r in report["history_records"]
                if r["flags"] & 8 and not r["flags"] & 1 and r["case_id"] in normal_ids]
        assert len(rows) == accepted["metrics"]["intervals"] == 1849
        fields = ("prepare_ticks", "submit_ticks", "io_upload_ticks", "geometry_wait_ticks")
        means = {f.removesuffix("_ticks") + "_ms": sum(r[f] for r in rows) / len(rows) * 1000 / hz for f in fields}
        # Peak records are per-case maxima and CAN contain cold/stress work.
        frame_rows = {r["frame_id"]: r for r in report["history_records"] if r["flags"] & 8}
        peaks = [{"case_id": p[0], "frame_id": p[1], "normal_case": p[0] in normal_ids,
                  "loading": bool(frame_rows[p[1]]["flags"] & 1),
                  "geometry_ms": p[2] * 1000 / hz, "sprites_ms": p[3] * 1000 / hz,
                  "effects_ms": p[4] * 1000 / hz, "prepare_ms": p[5] * 1000 / hz,
                  "cpu_read_bytes": p[6], "generated_bytes": p[7]}
                 for p in report["preparation_peaks"]]
        worst = max(rows, key=lambda r: sum(r[f] for f in fields[:3]))
        variants[label] = {"accepted_virtual_fps": accepted["metrics"]["virtual_fps"],
                           "normal_nonloading_rows": len(rows), "means": means,
                           "worst_component_frame": {k: worst[k] for k in ("case_id", "frame_id", *fields)},
                           "worst_component_ms": sum(worst[f] for f in fields[:3]) * 1000 / hz,
                           "normal_case_preparation_peaks": [p for p in peaks if p["normal_case"]],
                           "max_geometry_wait_ms": max(r[fields[3]] for r in rows) * 1000 / hz,
                           "max_quads": accepted["max_quads"], "max_vertices": accepted["max_vertices"],
                           "max_polygons": accepted["max_polygons"],
                           "vertex_storage_fraction": accepted["max_vertices"] / 6144,
                           "polygon_storage_fraction": accepted["max_polygons"] / 2048,
                           "max_texture_bytes": accepted["max_texture_bytes"],
                           "free_texture_capacity_at_reported_max": 393216 - accepted["max_texture_bytes"]}
        assert abs(variants[label]["worst_component_ms"] - accepted["max_measured_work_ms"]) < 1e-9
    phase_path = ROOT / "research/evidence/nds/n13/phase-pair-report.json"
    phase = load(phase_path)
    assert phase["passed"]
    inputs.append(pin(phase_path))
    water_headers = []
    for level in phase["levels"]:
        for header in level["native_phase_headers"]:
            width, height = struct.unpack_from("<2H", bytes.fromhex(header["header_hex"]))
            water_headers.append(dict(level=level["level"], width=width, height=height,
                                      flags=header["flags"], divisor=header["divisor"], phase=header["phase"]))
    sources = [*sorted((ROOT / "ports/nds/render").glob("*.c")),
               ROOT / "ports/nds/platform/render_probe/render_backend.c",
               ROOT / "ports/nds/platform/effects_probe/main.c",
               ROOT / "ports/nds/platform/effects_probe/effects_prepare.inc",
               ROOT / "ports/nds/platform/effects_probe/effects_runtime.c",
               ROOT / "ports/nds/include/dok/render_scene.h",
               ROOT / "ports/nds/include/dok/memory_budget.h",
               ROOT / "ports/nds/tools/render_metrics.py",
               ROOT / "ports/nds/tools/summarize_effects_performance.py",
               Path(__file__),
               SDK / "libs/libnds/include/nds/arm9/videoGL.h",
               SDK / "libs/libnds/include/nds/arm9/background.h",
               SDK / "docs/content/tutorial/intermediate/3d_graphics/index.md"]
    source_pins = [pin(p) for p in sources]
    report = dict(scope="Static source/SDK inspection plus offline reduction of existing emulator reports; no new execution or speedup measurement",
                  date="2026-10-29", variants=variants, water_headers=water_headers,
                  source_pins=source_pins, input_pins=inputs,
                  caveats=["Means use 1849 normal nonloading rows; 17 cold presentations are excluded",
                           "Per-case preparation peaks may include loading; not steady-state means",
                           "Storage headroom does not prove raster throughput",
                           "Source tree has pre-existing uncommitted N14-N16 changes; pins identify inspected bytes",
                           "No original EXE was opened or analyzed; source addresses are NDS file lines only"])
    for path in (OUT, *OUT.parents):
        if path == ROOT:
            break
        assert not path.is_symlink() and not path.is_junction(), f"Linked output: {path}"
    if OUT.exists():
        assert OUT.is_file() and OUT.stat().st_nlink == 1
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("Static GPU audit: accepted RU/EN report hashes verified; no ROM/EXE execution")
    for label, value in variants.items():
        print(label, json.dumps({"means": value["means"], "worst_ms": value["worst_component_ms"],
                                "max_GE_wait_ms": value["max_geometry_wait_ms"]}))
    print(OUT.relative_to(ROOT).as_posix())


if __name__ == "__main__":
    main()
