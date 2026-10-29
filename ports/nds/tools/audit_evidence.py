"""N01: read-only audit of original samples and saved Ghidra exports.

No EXE interpretation, emulation or Ghidra project mutation is performed.
The detailed records stay in research; only the compact registry enters Git.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = ROOT / "research/evidence/nds/n01"


def sha(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8", newline="\n")


def inspect():
    report = {"schema": 1, "task": "N01", "python": platform.python_version(),
              "scope": "File integrity and availability; not semantic equivalence",
              "samples": [], "exports": {}, "projects": {}}
    for language in ("ru", "en"):
        inventory = json.loads((ROOT / f"analysis/originals/inventory-{language}.json")
                               .read_text(encoding="utf-8"))
        for expected in (inventory, inventory["resource"]):
            path = ROOT / expected["file"]
            actual = sha(path) if path.is_file() else None
            report["samples"].append({
                "path": expected["file"], "expected_sha256": expected["sha256"],
                "sha256": actual, "bytes": path.stat().st_size if path.is_file() else None,
                "matches_inventory": actual == expected["sha256"] and
                path.stat().st_size == expected["size"],
            })
        directory = ROOT / f"decompiled/{language}"
        index = directory / "functions.tsv"
        rows = []
        if index.is_file():
            with index.open(encoding="utf-8-sig", newline="") as stream:
                rows = list(csv.DictReader(stream, delimiter="\t"))
        missing = []
        statuses = {}
        for row in rows:
            statuses[row["status"]] = statuses.get(row["status"], 0) + 1
            path = (directory / row["file"]).resolve()
            if not path.is_relative_to(directory.resolve()):
                raise ValueError(f"Export index escapes its directory: {row['file']}")
            if not path.is_file() or path.stat().st_size == 0:
                missing.append(row["file"])
        report["exports"][language] = {
            "functions": len(rows), "statuses": statuses,
            "missing_or_empty_function_files": missing,
            "required_files": {name: (directory / name).is_file() and
                               (directory / name).stat().st_size > 0
                               for name in ("functions.tsv", "listing.asm", "decompiled.c",
                                            "with_types.c", "ghidra.log", "references.tsv")},
        }
        project = ROOT / f"research/ghidra/projects/{language}/Kremlin.gpr"
        storage = project.with_suffix(".rep")
        report["projects"][language] = {
            "gpr": project.relative_to(ROOT).as_posix(), "gpr_exists": project.is_file(),
            "rep_exists": storage.is_dir(),
            "nonempty_storage_files": sum(p.is_file() and p.stat().st_size > 0
                                          for p in storage.rglob("*")),
            "opened_in_this_audit": False,
        }
    recorded = json.loads((ROOT / "analysis/originals/export-hashes.json")
                          .read_text(encoding="utf-8"))
    report["export_hashes"] = [
        {"path": name, "expected_sha256": expected,
         "sha256": sha(ROOT / name) if (ROOT / name).is_file() else None}
        for name, expected in recorded.items()
    ]
    for item in report["export_hashes"]:
        item["matches_recorded_export"] = item["sha256"] == item["expected_sha256"]
    report["dos_references"] = []
    runs = ["reference/" + name for name in
            ("ru-01", "ru-02", "ru-03", "en-01", "en-02")]
    runs += ["time/dos-ru", "time/dos-en"]
    for run in runs:
        directory = ROOT / "research/evidence" / run
        reference = directory / "report.json"
        item = {"run": run, "report": reference.relative_to(ROOT).as_posix(),
                "available": reference.is_file(), "frames": []}
        if reference.is_file():
            recorded_run = json.loads(reference.read_text(encoding="utf-8"))
            item["report_sha256"] = sha(reference)
            language = recorded_run["language"]
            sample = next(s for s in report["samples"] if
                          s["path"].endswith("PKR.EXE" if language == "ru" else "PKE.EXE"))
            item["primary_exe"] = sample["path"]
            item["recorded_exe_matches_inventory"] = all(
                recorded_run.get(key) == sample["expected_sha256"]
                for key in ("exe_before", "exe_after", "copy_exe_after"))
            item["player_start"] = recorded_run.get("player_start")
            item["originals_unchanged_recorded"] = recorded_run.get("originals_unchanged")
            item["emulator_sha256_recorded"] = recorded_run.get("dosbox_sha256")
            for frame in recorded_run["frames"]:
                raw = directory / (frame["name"] + ".idx")
                picture = raw.with_suffix(".png")
                item["frames"].append({
                    "name": frame["name"], "raw_exists": raw.is_file(),
                    "raw_matches_report": raw.is_file() and sha(raw) == frame["sha256"],
                    "raw_bytes": raw.stat().st_size if raw.is_file() else None,
                    "png_exists": picture.is_file(),
                    "png_sha256": sha(picture) if picture.is_file() else None,
                })
        report["dos_references"].append(item)
    preservation = EVIDENCE / "door-mirror/preservation.json"
    report["preserved_door_mirror"] = {"available": preservation.is_file(), "files": []}
    if preservation.is_file():
        report["preserved_door_mirror"]["manifest_sha256"] = sha(preservation)
        for item in json.loads(preservation.read_text(encoding="utf-8"))["files"]:
            path = ROOT / item["preserved"]
            report["preserved_door_mirror"]["files"].append({
                "path": item["preserved"], "sha256": item["sha256"],
                "matches_preservation": path.is_file() and sha(path) == item["sha256"],
            })
    report["integrity_passed"] = (
        all(s["matches_inventory"] for s in report["samples"])
        and all(e["matches_recorded_export"] for e in report["export_hashes"])
        and all(e["functions"] > 0 and not e["missing_or_empty_function_files"]
                and all(e["required_files"].values()) for e in report["exports"].values())
        and all(p["gpr_exists"] and p["rep_exists"] and p["nonempty_storage_files"] > 0
                for p in report["projects"].values())
        and all(r["available"] and r["frames"] and r["recorded_exe_matches_inventory"] and
                r["originals_unchanged_recorded"] is True and
                all(f["raw_matches_report"] and f["raw_bytes"] == 64000 and f["png_exists"]
                    for f in r["frames"]) for r in report["dos_references"])
        and report["preserved_door_mirror"]["available"]
        and len(report["preserved_door_mirror"]["files"]) == 6
        and all(f["matches_preservation"] for f in report["preserved_door_mirror"]["files"]))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("before", "after"), required=True)
    args = parser.parse_args()
    report = inspect()
    if args.phase == "after":
        before = json.loads((EVIDENCE / "audit-before.json").read_text(encoding="utf-8"))
        report["originals_unchanged"] = (
            [(s["path"], s["sha256"]) for s in before["samples"]] ==
            [(s["path"], s["sha256"]) for s in report["samples"]])
    output = EVIDENCE / f"audit-{args.phase}.json"
    write_json(output, report)
    if args.phase == "after":
        compact = {"schema": 1, "task": "N01", "date": "2026-10-28",
                   "audit_command": "py -3.14 "
                                    "ports/nds/tools/audit_evidence.py --phase after",
                   "python": report["python"],
                   "report": output.relative_to(ROOT).as_posix(),
                   "report_sha256": sha(output),
                   "integrity_passed": report["integrity_passed"],
                   "originals_unchanged": report["originals_unchanged"],
                   "samples": report["samples"],
                   "function_counts": {lang: e["functions"]
                                       for lang, e in report["exports"].items()},
                   "recorded_export_hash_matches": sum(
                       e["matches_recorded_export"] for e in report["export_hashes"]),
                   "recorded_export_hash_total": len(report["export_hashes"]),
                   "dos_runs_available": len(report["dos_references"]),
                   "dos_index_frames_verified": sum(len(r["frames"])
                                                    for r in report["dos_references"]),
                   "door_mirror_preservation": {
                       "path": "research/evidence/nds/n01/door-mirror/preservation.json",
                       "sha256": report["preserved_door_mirror"].get("manifest_sha256"),
                       "verified_files": len(report["preserved_door_mirror"]["files"])},
                   "registry": "docs/nds/spec/N01-evidence.md",
                   "limitations": ["No new EXE analysis; projects checked on disk, not opened",
                                   "Export hash differences are reported, not silently accepted",
                                   "DOS coverage and open scenarios are in the registry",
                                   "No NDS ROM, SDK or hardware verification in N01"]}
        write_json(ROOT / "analysis/nds-n01-evidence.json", compact)
    print(json.dumps({"report": output.relative_to(ROOT).as_posix(),
                      "integrity_passed": report["integrity_passed"],
                      "export_matches": sum(e["matches_recorded_export"]
                                            for e in report["export_hashes"]),
                      "export_total": len(report["export_hashes"]),
                      "function_counts": {lang: e["functions"]
                                          for lang, e in report["exports"].items()}}))
    if not report["integrity_passed"] or report.get("originals_unchanged") is False:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
