#!/usr/bin/env python3
"""N06 host camera/layout gates and the separate ARM9 bitmap-reference viewer.

No package reconversion or previous-task rebuilds. Linux is linked, not run;
the viewer emulator run is a separate gate and is not an N07 3D renderer.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n06/host"
EVIDENCE = ROOT / "research/evidence/nds/n06"
PACKAGE_SHA = "2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def safe_output(path: Path, root: Path) -> Path:
    path = path.absolute()
    if ".." in path.parts or not path.is_relative_to(root):
        raise ValueError("N06 output escapes its fixed root")
    for ancestor in [path, *path.parents]:
        if ancestor == ROOT:
            break
        if ancestor.is_symlink() or (hasattr(ancestor, "is_junction") and ancestor.is_junction()):
            raise ValueError("N06 output crosses a symlink or junction")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError("N06 output must be an unaliased regular file")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    parser.add_argument("--zig", type=Path, default=local / "Programs/Zig/0.13.0/zig.exe")
    parser.add_argument("--host-only", action="store_true", help="skip the ARM9 SDK build only")
    args = parser.parse_args()
    report_path = safe_output(EVIDENCE / "host-check.json", EVIDENCE)
    report = dict(schema_version=1, task="N06", passed=False, python=sys.version,
                  platform=platform.platform(), scope="host_only" if args.host_only else "host_and_arm9_build",
                  commands=[], sources=[], artifacts=[], preserved=[],
                  limitations=["Linux x86_64-musl binary is compiled and linked, not executed.",
                               "N06 viewer is a bitmap-reference display, not the N07 3D renderer.",
                               "Emulator viewer execution is a separate gate; physical DS/DS Lite and FPS remain N24."])
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")

    def run(argv: list[str], timeout: int = 300) -> str:
        command = dict(argv=argv, cwd=str(ROOT))
        report["commands"].append(command)
        print("Running:", subprocess.list2cmdline(argv), flush=True)
        start = time.monotonic()
        try:
            completed = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True,
                                       encoding="utf8", errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired:
            command.update(timed_out=True, seconds=time.monotonic() - start)
            raise
        command.update(exit_code=completed.returncode, seconds=time.monotonic() - start,
                       stdout=completed.stdout, stderr=completed.stderr)
        if completed.stdout:
            print(completed.stdout, end="", flush=True)
        if completed.stderr:
            print(completed.stderr, end="", file=sys.stderr, flush=True)
        if completed.returncode != 0:
            raise RuntimeError(f"Command failed with exit {completed.returncode}: {argv[0]}")
        return completed.stdout

    def artifact(path: Path) -> dict:
        return dict(path=relative(path), bytes=path.stat().st_size, sha256=digest(path))

    try:
        package = ROOT / "build/nds/assets/dok-assets.ndspak"
        if digest(package) != PACKAGE_SHA:
            raise ValueError("The accepted N04 package changed")
        report["preserved"].append(artifact(package))
        # Pin previous accepted primary artifacts and source files, not just a
        # current before/after pair which could accept an already altered input.
        for task in ("n03-architecture", "n04-assets", "n05-balance"):
            summary = json.loads((ROOT / f"analysis/nds-{task}.json").read_text("utf8"))
            for row in summary.get("artifacts", []):
                path = ROOT / row["path"]
                if path.stat().st_size != row["bytes"] or digest(path) != row["sha256"]:
                    raise ValueError(f"Previous task artifact changed: {row['path']}")
                report["preserved"].append(artifact(path))
            manifest_pin = summary.get("source_manifest")
            if isinstance(manifest_pin, str):
                manifest_pin = dict(path=manifest_pin, sha256=summary["source_manifest_sha256"])
            if isinstance(manifest_pin, dict):
                manifest_path = ROOT / manifest_pin["path"]
                if digest(manifest_path) != manifest_pin["sha256"]:
                    raise ValueError(f"Previous source manifest changed: {manifest_pin['path']}")
                report["preserved"].append(artifact(manifest_path))
                manifest = json.loads(manifest_path.read_text("utf8"))
                rows = manifest if isinstance(manifest, list) else manifest.get("files", manifest.get("sources", []))
                if not rows:
                    raise ValueError(f"Source manifest is empty: {manifest_pin['path']}")
                for row in rows:
                    path = ROOT / row["path"]
                    if digest(path) != row["sha256"]:
                        raise ValueError(f"Previous accepted source changed: {row['path']}")
                    report["preserved"].append(artifact(path))
            if task == "n05-balance":
                runtime_pin = summary["emulator"]["report"]
                runtime_path = ROOT / runtime_pin["path"]
                if digest(runtime_path) != runtime_pin["sha256"]:
                    raise ValueError("N05 accepted emulator report changed")
                report["preserved"].append(artifact(runtime_path))
                for golden in json.loads(runtime_path.read_text("utf8"))["goldens"]:
                    snapshot = Path(golden["path"])
                    if digest(snapshot) != golden["sha256"]:
                        raise ValueError(f"N05 canonical snapshot changed: {relative(snapshot)}")
                    report["preserved"].append(artifact(snapshot))
        source_names = ["ports/nds/include/dok/projection.h", "ports/nds/core/projection.c",
                        "ports/nds/tests/test_projection.c", "ports/nds/tests/projection_compile.c",
                        "ports/nds/tools/audit_camera.py", "ports/nds/tools/layout_ui.py",
                        "ports/nds/tools/compose_visuals.py", "ports/nds/tools/check_visuals.py",
                        "ports/nds/tools/audit_visual_dos.py", "ports/nds/tools/capture_visual_dos.py",
                        "ports/nds/tools/capture_clean_visual_dos.py",
                        "ports/nds/tools/run_visual_emulator.py"]
        if not args.host_only:
            source_names += ["ports/nds/Makefile.n06", "ports/nds/tools/build_visual.ps1",
                             "ports/nds/platform/visual/main.c", "ports/nds/platform/visual/visual_probe.h"]
        report["sources"] = [artifact(ROOT / name) for name in source_names]
        version = run([str(args.zig), "version"]).strip()
        if version != "0.13.0":
            raise ValueError(f"Expected shared Zig 0.13.0, got {version}")
        report["zig"] = dict(path=str(args.zig), version=version, sha256=digest(args.zig))
        run([sys.executable, "-X", "utf8", "ports/nds/tools/audit_camera.py"])
        camera_path = EVIDENCE / "camera-audit.json"
        camera = json.loads(camera_path.read_text("utf8"))
        if not camera.get("passed") or len(camera.get("original_evidence", [])) != 2:
            raise ValueError("The original camera audit did not pass both languages")
        report["camera_audit"] = artifact(camera_path)
        flags = ["-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-O2", "-g",
                 "-I", "ports/nds/include"]
        sources = ["ports/nds/core/projection.c", "ports/nds/tests/test_projection.c"]
        native = safe_output(OUT / "test_projection.exe", OUT)
        linux = safe_output(OUT / "test_projection-linux-x86_64", OUT)
        for path in (native, linux):
            path.unlink(missing_ok=True)
        run([str(args.zig), "cc", *flags, *sources, "-o", str(native)])
        report["projection_stdout"] = run([str(native), str(EVIDENCE / "projection-vectors.tsv"),
                                            str(EVIDENCE / "projection-map-vectors.tsv")])
        run([str(args.zig), "cc", *flags, "-target", "x86_64-linux-musl", "-static", *sources, "-o", str(linux)])
        report["artifacts"] += [artifact(native), artifact(linux)]
        for name, target in (("native", []), ("linux-x86_64", ["-target", "x86_64-linux-musl"])):
            output = safe_output(OUT / f"projection-compile-{name}.o", OUT)
            output.unlink(missing_ok=True)
            run([str(args.zig), "cc", *flags, *target, "-c", "ports/nds/tests/projection_compile.c", "-o", str(output)])
            report["artifacts"].append(artifact(output))
        run([sys.executable, "-X", "utf8", "ports/nds/tools/layout_ui.py"])
        run([sys.executable, "-X", "utf8", "ports/nds/tools/audit_visual_dos.py"])
        run([sys.executable, "-X", "utf8", "ports/nds/tools/compose_visuals.py"])
        for name in ("ui-layout-report.json", "visual-report.json"):
            path = EVIDENCE / name
            result = json.loads(path.read_text("utf8"))
            if not result.get("passed"):
                raise ValueError(f"N06 host evidence failed: {name}")
            report[name.removesuffix(".json").replace("-", "_")] = artifact(path)
        dos_manifest = EVIDENCE / "dos/manifest.json"
        dos = json.loads(dos_manifest.read_text("utf8"))
        if (dos.get("status") != "passed" or dos.get("session_count") != 12 or
                dos.get("frame_count") != 48 or dos.get("clean_session_count") != 6 or
                dos.get("popup_session_count") != 6):
            raise ValueError("Original DOS popup/clean visual sources did not pass all 12 sessions/48 frames")
        report["dos_manifest"] = artifact(dos_manifest)
        if not args.host_only:
            run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                 "ports/nds/tools/build_visual.ps1", "-Rebuild"])
            build_path = ROOT / "build/nds/n06/build-report.json"
            build = json.loads(build_path.read_text("utf-8-sig"))
            if not build.get("toolchain_lock_verified") or build.get("toolchain_lock_sha256") != digest(ROOT / "ports/nds/toolchain_versions.json"):
                raise ValueError("ARM9 builder did not verify the pinned SDK")
            expected = {f"build/nds/n06/dok_nds_visual.{ext}" for ext in ("nds", "elf", "map")}
            if {row["path"] for row in build.get("artifacts", [])} != expected:
                raise ValueError("Unexpected N06 ARM9 artifact set")
            for row in build["artifacts"]:
                if artifact(ROOT / row["path"]) != row:
                    raise ValueError(f"ARM9 artifact changed: {row['path']}")
            report["arm9_build"] = dict(report=artifact(build_path), artifacts=build["artifacts"])
        for row in report["sources"] + report["preserved"]:
            if artifact(ROOT / row["path"]) != row:
                raise ValueError(f"Input changed during the N06 gate: {row['path']}")
        report["passed"] = True
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
        print(report["error"], file=sys.stderr, flush=True)
    finally:
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
        print(f"N06 host/build passed={report['passed']}; {report_path}", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
