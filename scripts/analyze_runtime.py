"""Independently verify compact runtime conversion against all six static worlds.

Full resource names, messages, maps and object state remain in ignored build/.
analysis/ receives counts, hashes and verification results only. This checks the
stage-6 reconstruction contract, not complete equivalence to the DOS executable.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess

from analyze_levels import reference, W, L, C, I, A, S, T
from analyze_resources import parse
from analyze_worlds import startup
from inventory import EXPECTED

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path.read_bytes()),
            "size": path.stat().st_size}


def check(actual, expected, context):
    if actual != expected:
        if isinstance(actual, (int, bool, type(None))) and isinstance(expected, (int, bool, type(None))):
            raise AssertionError(f"{context}: actual={actual!r}, expected={expected!r}")
        raise AssertionError(f"{context}: values differ (full private outputs remain under research/evidence/runtime)")


def load_oracles(language):
    """Recompute from original RES; do not trust stale research/evidence/worlds outputs."""
    exe_name, exe_hash = EXPECTED[language]
    exe = ROOT / "originals/pkr" / exe_name
    check(sha(exe.read_bytes()), exe_hash, f"{language} EXE hash")
    inventory = json.loads((ROOT / f"analysis/inventory-{language}.json").read_text(encoding="utf-8"))
    archive_path = ROOT / inventory["resource"]["file"]
    archive = archive_path.read_bytes()
    check(sha(archive), inventory["resource"]["sha256"], f"{language} RES hash")
    entries = parse(archive)
    resources = {e["name_hex"]: archive[e["offset"]:e["offset"] + e["size"]]
                 for e in reversed(entries)}
    for resource_index in (18, 19, 20):
        entry = entries[resource_index]
        source = reference(resources[entry["name_hex"]])
        native, states = startup(source, resources)
        existing = ROOT / f"research/evidence/worlds/{language}/{resource_index:04d}.json"
        check(json.loads(existing.read_text(encoding="utf-8")), native,
              f"{language} {entry['name']} independent stage6 oracle")
        yield archive_path, entry, source, native, states, entries
    check(sha(exe.read_bytes()), exe_hash, f"{language} EXE unchanged")
    check(sha(archive_path.read_bytes()), inventory["resource"]["sha256"], f"{language} RES unchanged")


ACTOR_FIELDS = {
    "x": (0x26, 4), "y": (0x2a, 4), "z": (0x2e, 4),
    "base_speed": (0x38, 4), "step_distance": (0x3c, 4),
    "accumulator_a": (0x90, 4), "accumulator_b": (0x94, 4),
    "motion_flags": (0x1c, 4), "heading": (0x32, 2), "angle_34": (0x34, 2),
    "mode": (0x36, 2), "layer": (0x5a, 2), "direction": (0x7c, 2),
    "step": (0x7e, 2), "ready": (0x80, 2), "command": (0x82, 2),
    "command_84": (0x84, 2), "parameter_98": (0x98, 2),
    "bound_9e": (0x9e, 2), "bound_a0": (0xa0, 2),
    "glyph": (0x5c, 1), "previous_glyph": (0x5d, 1)
}


def verify_model(actual, source, native, states, context):
    check(actual["settings"], source["settings"], context + " settings")
    check(actual["palette"], source["palette"], context + " palette")
    check(actual["light"], source["light"], context + " light")
    glyph_index = [0] * 256
    for i, obj in enumerate(source["objects"]):
        glyph_index[obj["glyph"]] = i + 1
    check(actual["glyph_index"], glyph_index, context + " one-object-per-glyph table")
    check(actual["maps"], native["maps"], context + " MMap/TMap/FCMap/visited")
    check(actual["player_index"], native["player_index"], context + " player index")
    check(actual["pickup_99_total"], native["pickup_99_total"], context + " category99 sum")
    check(len(actual["objects"]), len(source["objects"]), context + " definition count")
    result = {"definitions": len(states), "actors": 0, "placed_actor_cells": 0,
              "unique_placed_actors": 0, "unplaced_actors": 0, "repeated_actor_definitions": 0,
              "native_known_bytes": 0, "runtime_known_bytes": 0, "missing_known_bytes": 0,
              "numeric_fields": 0, "resource_fields": 0, "text_fields": 0,
              "classes": dict(sorted(Counter(o["class"] for o in source["objects"]).items()))}
    missing_offsets = Counter()
    actor_indices = []
    resource_names = {source["palette"], source["light"]}
    for index, (obj, definition, old, state) in enumerate(zip(actual["objects"], source["objects"], native["objects"], states)):
        tag = f"{context} object {index} class {definition['class']}"
        for key, old_key in (("glyph", "glyph"), ("class", "class"), ("flags", "flags"),
                             ("sprite_flags", "sprite_flags"), ("width_parameter", "width"),
                             ("height_parameter", "height"), ("marker", "marker"),
                             ("sprite", "sprite"), ("animation", "animation"), ("sound", "sound")):
            check(obj[key], definition[old_key], tag + " " + key)
        check(obj["placements"], old["placements"], tag + " placement count")
        expected_fields = [{"offset": offset, "kind": kind,
                            ("number" if kind in (W, L, C) else "hex"): value}
                           for kind, offset, value in definition["fields"]]
        check(obj["fields"], expected_fields, tag + " all class fields/resources/text")
        for kind, _, value in definition["fields"]:
            if kind in (W, L, C):
                result["numeric_fields"] += 1
            elif kind == T:
                result["text_fields"] += 1
            else:
                result["resource_fields"] += 1
                if value:
                    resource_names.add(value)
        resource_names.update(value for value in (definition["sprite"], definition["animation"], definition["sound"]) if value)
        data, known = bytes.fromhex(obj["bytes"]), bytes.fromhex(obj["known"])
        original, original_known = bytes.fromhex(old["bytes"]), bytes.fromhex(old["known"])
        check(len(data), len(original), tag + " diagnostic bytes length")
        check(len(known), len(original_known), tag + " diagnostic mask length")
        check(known, original_known, tag + " complete stage6 known-mask preservation")
        for offset, value in enumerate(data):
            have = bool(known[offset // 8] & (1 << (offset % 8)))
            expected_have = bool(original_known[offset // 8] & (1 << (offset % 8)))
            if have:
                check(expected_have, True, tag + f" byte {offset:#x} newly asserted known")
                check(value, original[offset], tag + f" native-known byte {offset:#x}")
                result["runtime_known_bytes"] += 1
            if expected_have:
                result["native_known_bytes"] += 1
                if not have:
                    result["missing_known_bytes"] += 1
                    missing_offsets[f"class{definition['class']}:{offset:#04x}"] += 1
        actor = obj["actor"]
        if definition["class"] in (1, 5, 9):
            check(obj["actor_index"], result["actors"], tag + " stable actor index")
            actor_indices.append(obj["actor_index"])
            check(actor is not None, True, tag + " actor exists even when unplaced")
            expected_mask = (1 if 0x26 in state.values else 0) | (2 if 0x2a in state.values else 0) | (4 if 0x2e in state.values else 0) | (8 if 0x9e in state.values and 0xa0 in state.values else 0)
            check(actor["known"], expected_mask, tag + " unknown-position mask")
            for key, (offset, width) in ACTOR_FIELDS.items():
                if key not in actor:
                    continue
                if all(offset + b in state.values for b in range(width)):
                    check(actor[key], state.get(offset, width, sign=key not in ("motion_flags", "glyph", "previous_glyph")), tag + " actor " + key)
                elif key in ("x", "y", "z", "bound_9e", "bound_a0"):
                    # Storage zeroes are permitted only with the corresponding known bit cleared.
                    bit = {"x": 1, "y": 2, "z": 4, "bound_9e": 8, "bound_a0": 8}[key]
                    check(actor["known"] & bit, 0, tag + " unknown " + key)
            result["actors"] += 1
            result["placed_actor_cells"] += old["placements"]
            result["unique_placed_actors"] += bool(old["placements"])
            result["unplaced_actors"] += not old["placements"]
            result["repeated_actor_definitions"] += old["placements"] > 1
        else:
            check(actor, None, tag + " non-actor has no actor storage")
            check(obj["actor_index"], 65535, tag + " non-actor sentinel")
    check(len(set(actor_indices)), len(actor_indices), context + " no actor aliasing between definitions")
    result["unique_resource_names"] = len(resource_names)
    result["missing_known_offsets"] = dict(sorted(missing_offsets.items()))
    result["maps"] = {name: {"cells": len(bytes.fromhex(data)), "sha256": sha(bytes.fromhex(data))}
                      for name, data in zip(("MMap", "TMap", "FCMap", "visited"), actual["maps"])}
    result["memory"] = actual["memory"]
    check(actual["memory"]["object_count"], len(states), context + " stored definition count")
    check(actual["memory"]["actor_count"], result["actors"], context + " stored actor count")
    check(actual["memory"]["field_count"], sum(len(o["fields"]) for o in source["objects"]), context + " stored field count")
    check(actual["memory"]["cells"], source["settings"][0] * source["settings"][2], context + " stored cell count")
    text_size = sum(len(bytes.fromhex(v)) for v in (source["palette"], source["light"]))
    for obj in source["objects"]:
        text_size += sum(len(bytes.fromhex(obj[k])) for k in ("sprite", "animation", "sound"))
        text_size += sum(len(bytes.fromhex(v)) for k, _, v in obj["fields"] if k not in (W, L, C))
    check(actual["memory"]["text_size"], text_size, context + " all ROM string bytes retained")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=ROOT / "build/native/dok-runtime.exe")
    parser.add_argument("--tests", type=Path, default=ROOT / "build/native/dok-runtime-tests.exe")
    args = parser.parse_args()
    tool = args.tool.resolve()
    report = {"task": "G02", "verification_kind": "independent_static_reconstruction",
              "dos_equivalence": "not established; G00 independently confirms only initial hero LEVEL1",
              "levels": [], "tool": {"path": str(tool), "sha256": sha(tool.read_bytes())},
              "checker_sha256": sha(Path(__file__).read_bytes()),
              "originals": {}, "totals": {}}
    layout = json.loads(subprocess.check_output([str(tool), "--layout"], cwd=ROOT))
    report["host_layout"] = layout
    for language in EXPECTED:
        inventory = json.loads((ROOT / f"analysis/inventory-{language}.json").read_text(encoding="utf-8"))
        report["originals"][language] = {"exe": EXPECTED[language][1], "res": inventory["resource"]["sha256"]}
        for archive_path, entry, source, native, states, entries in load_oracles(language):
            raw = subprocess.check_output([str(tool), str(archive_path), entry["name"]], cwd=ROOT)
            actual = json.loads(raw)
            context = language + " " + entry["name"]
            summary = verify_model(actual, source, native, states, context)
            directory = ROOT / "research/evidence/runtime" / language
            summary.update(language=language, resource_index=entry["index"], level_sha256=entry["sha256"],
                           source_model=save(directory / (entry["name"] + "-definitions.json"), source),
                           native_oracle=save(directory / (entry["name"] + "-native.json"), native),
                           runtime_model=save(directory / (entry["name"] + "-runtime.json"), actual))
            report["levels"].append(summary)
    for key in ("definitions", "actors", "placed_actor_cells", "unique_placed_actors", "unplaced_actors",
                "repeated_actor_definitions", "numeric_fields", "resource_fields", "text_fields",
                "native_known_bytes", "runtime_known_bytes", "missing_known_bytes"):
        report["totals"][key] = sum(level[key] for level in report["levels"])
    report["maxima"] = {key: max(level[key] for level in report["levels"])
                        for key in ("definitions", "actors", "placed_actor_cells", "unique_placed_actors",
                                    "unplaced_actors", "numeric_fields", "resource_fields", "text_fields", "unique_resource_names")}
    report["class_maxima"] = {cls: max(level["classes"].get(cls, 0) for level in report["levels"])
                              for cls in sorted({cls for level in report["levels"] for cls in level["classes"]})}
    report["capacity_headroom"] = {}
    for key, measured in (("objects", "object_count"), ("actors", "actor_count"),
                          ("fields", "field_count"), ("text", "text_size"), ("cells", "cells")):
        used = max(level["memory"][measured] for level in report["levels"])
        capacity = layout["capacities"][key]
        if used > capacity:
            raise AssertionError(f"{key}: observed {used} exceeds capacity {capacity}")
        report["capacity_headroom"][key] = {"observed_maximum": used, "capacity": capacity,
                                              "spare": capacity - used}
    for level in report["levels"]:
        memory = level["memory"]
        expected_mutable = memory["object_count"] * layout["object"]["size"] + memory["actor_count"] * layout["actor"]["size"] + layout["player"]["size"] + 4 * memory["cells"]
        check(memory["mutable_used"], expected_mutable, "mutable used byte arithmetic")
        expected_rom = memory["object_count"] * (layout["definition"]["size"] + layout["object"]["size"]) + memory["actor_count"] * layout["actor"]["size"] + layout["player"]["size"] + memory["field_count"] * layout["field"]["size"] + memory["text_size"] + 3 * memory["cells"]
        check(memory["rom_records_used"], expected_rom, "ROM used record byte arithmetic")
    tests = args.tests.resolve()
    executed = subprocess.run([str(tests)], cwd=ROOT, capture_output=True, text=True, check=True)
    report["synthetic_tests"] = {"path": str(tests), "sha256": sha(tests.read_bytes()),
                                  "source": "tests/test_runtime.c", "source_sha256": sha((ROOT / "tests/test_runtime.c").read_bytes()),
                                  "exit_code": executed.returncode, "stdout": executed.stdout.strip(),
                                  "stderr": executed.stderr.strip(),
                                  "coverage": [
                                      "shared glyph references one actor; last row-major placement wins",
                                      "unplaced class5 has unknown position and preserves failed-read outputs",
                                      "unknown course counter/pointers and invalid scalar widths remain unknown",
                                      "pickup category99 signed-word wrap; projectile negative velocity and SAR half-damage",
                                      "every fixture native-known byte and mask matches runtime diagnostics",
                                      "mutable player/monster/position reads reflect current state",
                                      "reset restores complete state, visited and all reserved object/actor/map slots",
                                      "non-null weapon object IDs are not fabricated native pointers",
                                      "reset rejects object/actor/field/text/cell overcapacity and malformed metadata",
                                      "object-overcapacity reset leaves prior runtime byte-for-byte unchanged",
                                      "runtime reset survives destroyed stage6 source arrays",
                                      "release drops borrowed view and clears mutable state",
                                      "conversion of 129 actual actor definitions rejects actor capacity",
                                      "conversion rejects object-count overflow and null arguments"
                                  ]}
    report["status"] = "passed"
    save(ROOT / "analysis/runtime-verification.json", report)
    print(json.dumps({"status": report["status"], "levels": len(report["levels"]),
                      "totals": report["totals"], "maxima": report["maxima"]}, indent=2))


if __name__ == "__main__":
    main()
