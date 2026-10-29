#!/usr/bin/env python3
"""N11 copies accepted N04/N07 fixture bytes; no trig or scene regeneration."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n11"
EVIDENCE = ROOT / "research/evidence/nds/n11"
INITIAL = EVIDENCE / "initial-dependency-integrity.json"
INITIAL_SHA256 = "cc581af689ff27e0eda5a44f376b5ccf625cadba5d2c0e02dc91a884adc873bf"
COPIES = (
    ("build/nds/n07/generated/render_trig.inc", "generated/render_trig.inc"),
    ("build/nds/n07/scene.dat", "scene.dat"),
    ("build/nds/n07/scene-manifest.json", "scene-manifest.json"),
    ("build/nds/n07/scene.dat", "fixture/scene.dat"),
    ("build/nds/assets/dok-assets.ndspak", "fixture/assets.ndp"),
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def artifact(path: Path) -> dict:
    return dict(path=path.relative_to(ROOT).as_posix(),
                bytes=path.stat().st_size, sha256=digest(path))


def safe_output(path: Path, root: Path) -> Path:
    path = path.absolute()
    if ".." in path.parts or path == root or not path.is_relative_to(root):
        raise ValueError("N11 fixture output escapes its fixed root")
    for item in (path, *path.parents):
        if item == ROOT:
            break
        if item.is_symlink() or item.is_junction():
            raise ValueError(f"N11 output crosses alias: {item}")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError(f"N11 output is not an unaliased regular file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify existing exact copies without writing any file")
    args = parser.parse_args()
    if digest(INITIAL) != INITIAL_SHA256:
        raise ValueError("Frozen N11 dependency snapshot changed")
    initial = json.loads(INITIAL.read_text("utf8"))
    if not initial.get("passed") or initial.get("checked") != 439:
        raise ValueError("Expected frozen N11 dependency snapshot with 439 pins")
    pins = {row["path"]: row for row in initial["files"]}
    report = dict(task="N11", passed=False, readiness_proven=False,
                  method="exact-byte copies of accepted N04/N07 inputs; no regeneration",
                  dependency_snapshot=artifact(INITIAL), copies=[])
    for source_name, output_name in COPIES:
        source = ROOT / source_name
        expected = pins[source_name]
        if artifact(source) != expected:
            raise ValueError(f"Accepted fixture source changed: {source_name}")
        output = OUT / output_name
        if not args.check:
            safe_output(output, OUT).write_bytes(source.read_bytes())
        actual = artifact(output)
        if actual["bytes"] != expected["bytes"] or actual["sha256"] != expected["sha256"]:
            raise ValueError(f"N11 fixture copy differs: {output_name}")
        report["copies"].append(dict(source=expected, output=actual))
    report["passed"] = True
    if not args.check:
        safe_output(EVIDENCE / "fixture-report.json", EVIDENCE).write_text(
            json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(f"N11 exact accepted fixture copies: PASS ({len(report['copies'])} files)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"N11 fixture FAIL: {type(error).__name__}: {error}", file=sys.stderr)
        raise SystemExit(1)
