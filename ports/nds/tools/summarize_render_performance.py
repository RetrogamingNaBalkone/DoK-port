"""Verify paired profiles, archive evidence and publish the compact N07P result."""
from __future__ import annotations

from bisect import bisect_right
from collections import Counter
import json
import math
from pathlib import Path
import shutil

from benchmark_renderer import ROOT, OUT, digest

EVIDENCE = ROOT / "research/evidence/nds/perf"
REFRESH = 33513982 / 560190


def artifact(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=digest(path))


def load(path):
    return json.loads(path.read_text("utf8"))


def interval_metrics(values):
    ordered = sorted(values)
    def percentile(p):
        return ordered[max(0, math.ceil(len(ordered) * p)-1)]
    return dict(intervals=len(values), histogram=dict(sorted(Counter(values).items())),
                virtual_fps=len(values)*REFRESH/sum(values),
                median_vblank=percentile(.5), p95_vblank=percentile(.95),
                p99_vblank=percentile(.99), max_vblank=max(values),
                fraction_over_one=sum(v>1 for v in values)/len(values),
                fraction_over_two=sum(v>2 for v in values)/len(values))


def diagnostic_metrics(report):
    intervals, groups, per_case = [], [], {}
    current, previous = [], None
    for row in report["traces"]:
        if row["action"] != 3:
            continue
        same = (previous and row["status"] == previous["status"] == 2 and
                (row["case_id"], row["stage_sequence"]) == (previous["case_id"], previous["stage_sequence"]))
        if same:
            delta = row["vblank_count"] - previous["vblank_count"]
            assert delta > 0
            intervals.append(delta)
            per_case.setdefault(row["case_id"], []).append(delta)
            current.append(row["vblank_count"])
        else:
            if current:
                groups.append(current)
            current = [row["vblank_count"]] if row["status"] == 2 else []
        previous = row
    if current:
        groups.append(current)
    windows = [bisect_right(group, start+REFRESH)-bisect_right(group, start)
               for group in groups for start in group if start+REFRESH <= group[-1]]
    result = interval_metrics(intervals)
    result.update(cases={str(k): interval_metrics(v) for k,v in per_case.items()},
                  minimum_new_frames_in_one_second=min(windows), checked_one_second_windows=len(windows),
                  source_counts=report["trace_checks"]["counts"], frontend_frames=report["frontend_frames"],
                  scope="Active same-case/same-stage presentation-trace intervals; pauses/loading/transitions excluded. State-driven input route, not identical callback timelines.")
    return result


def main():
    summaries = {v:load(OUT/v/"summary.json") for v in ("baseline", "optimized")}
    reports = {v:load(OUT/v/"emulator/renderer/result.json") for v in summaries}
    for report in reports.values():
        assert report["passed"] and report["history"]["budget_passed"]
    rows = {v:[r for r in report["history_records"] if not r["flags"] & (1|32|64)]
            for v,report in reports.items()}
    assert len(rows["baseline"]) == len(rows["optimized"]) == 936
    semantic = ("case_id", "frame_id", "input_quads", "submitted_quads", "submitted_vertices",
                "hardware_polygons", "hardware_vertices", "texture_bytes", "palette_bytes",
                "camera_x", "camera_y", "camera_z", "sin_q15", "cos_q15")
    for before, after in zip(rows["baseline"],rows["optimized"],strict=True):
        assert all(before[k] == after[k] for k in semantic), "Paired geometry/camera changed"
    images = []
    for case in range(1,20):
        paths = [OUT/v/"emulator/renderer"/f"case-{case:02}-top.png" for v in summaries]
        assert digest(paths[0]) == digest(paths[1]), "Paired top PNG changed"
        images.append(dict(case=case, identical_png_sha256=digest(paths[0])))
    comparisons = []
    for before, after in zip(summaries["baseline"]["cases"],summaries["optimized"]["cases"],strict=True):
        cpu_before = before["prepare_ms"]+before["submit_ms"]+before["geometry_wait_ms"]
        cpu_after = after["prepare_ms"]+after["submit_ms"]+after["geometry_wait_ms"]
        comparisons.append(dict(name=before["name"], frames=before["frames"],
                                before_fps=before["virtual_fps"], after_fps=after["virtual_fps"],
                                before_work_ms=cpu_before, after_work_ms=cpu_after,
                                work_reduction_percent=(1-cpu_after/cpu_before)*100,
                                submit_reduction_percent=(1-after["submit_ms"]/before["submit_ms"])*100))
    old_path = ROOT/"build/nds/n11/emulator/result.json"
    old, new, audit = [load(p) for p in (old_path,OUT/"n11-optimized/emulator/result.json",OUT/"n11-audit/emulator/result.json")]
    assert all(r["passed"] for r in (old,new,audit))
    assert load(EVIDENCE/"admission-check.json")["passed"]
    for variant in ("n11-optimized", "n11-audit"):
        for case in range(1,5):
            for stage in ("start", "closed"):
                name = f"case{case}-{stage}-top.png"
                assert digest(old_path.parent/name) == digest(OUT/variant/"emulator"/name)
    archive_files = []
    for variant in ("baseline", "optimized"):
        target=EVIDENCE/variant
        target.mkdir(exist_ok=True)
        for source in [OUT/variant/"summary.json", OUT/variant/"build-manifest.json",
                       OUT/variant/"emulator/renderer/result.json",
                       *(OUT/variant/"emulator/renderer").glob("*-top.png")]:
            dest=target/source.name
            shutil.copyfile(source,dest)
            assert digest(source)==digest(dest)
            archive_files.append(artifact(dest))
    for variant in ("n11-optimized", "n11-audit"):
        target=EVIDENCE/variant
        target.mkdir(exist_ok=True)
        for source in [OUT/variant/"build-manifest.json",OUT/variant/"emulator/result.json",
                       OUT/variant/"emulator/snapshot-worlds.bin",
                       *(OUT/variant/"emulator").glob("*-top.png")]:
            dest=target/source.name
            shutil.copyfile(source,dest)
            assert digest(source)==digest(dest)
            archive_files.append(artifact(dest))
    (EVIDENCE/"archive-manifest.json").write_text(json.dumps(archive_files,indent=2)+"\n","utf8")
    metrics = {name:diagnostic_metrics(r) for name,r in (("before",old),("optimized",new),("audit",audit))}
    assert metrics["optimized"]["fraction_over_two"] <= .01
    assert metrics["optimized"]["minimum_new_frames_in_one_second"] >= REFRESH/2
    result = dict(schema_version=1, task="N07P", date="2026-10-29", passed=True,
                  status="ready-emulator", next_task="N12", automatic_continuation=False,
                  scope="Early geometry renderer and N11 diagnostic route; not complete gameplay or physical DS FPS",
                  refresh_hz=REFRESH, renderer_comparison=comparisons,
                  renderer_intervals={v:interval_metrics([r["vblank_end"]-r["vblank_start"] for r in values]) for v,values in rows.items()},
                  identical_geometry_frames=936, identical_renderer_top_pngs=len(images),
                  identical_closed_door_top_pngs_per_variant=8, diagnostic=metrics,
                  admission_checks=load(EVIDENCE/"admission-check.json"),
                  host_checks=artifact(EVIDENCE/"host-check.json"),
                  audit_host_checks=artifact(EVIDENCE/"host-check-audit.json"),
                  archive_manifest=artifact(EVIDENCE/"archive-manifest.json"),
                  artifacts=[artifact(OUT/v/("dok_nds_interactions."+ext))
                             for v in ("n11-optimized","n11-audit") for ext in ("nds","elf","map")],
                  architecture_decision="Continue to N12 when authorized; actor and combined LIGHT/water budgets remain unproven until N13P; physical acceptance N24",
                  limitations=["No actors, complete LIGHT/water/reflective effects, full game/audio/map in this prototype",
                               "Large N11 FPS improvement includes moving expensive world/queue hashing into an explicit audit build",
                               "N07P is not the N23/N24 full-game performance acceptance"])
    path=ROOT/"analysis/nds-render-performance.json"
    path.write_text(json.dumps(result,indent=2)+"\n","utf8")
    print(json.dumps({"passed":True,"renderer_frames":936,"identical_images":19,
                      "diagnostic_fps":{k:v["virtual_fps"] for k,v in metrics.items()},
                      "result":str(path)},indent=2))
    return 0


if __name__=="__main__":
    raise SystemExit(main())
