"""Verify the isolated Old Gold export and compare original data for a shared port.

Originals are read only. Full derived payloads remain under decompiled/old-gold/.
analysis/originals/old-gold/ receives metadata, addresses, counts and hashes only.
Python 3.11+, standard library and existing project analysis helpers.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[2]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import collections
import csv
import io
import json
import struct
import subprocess
from pathlib import Path

from analyze_resources import ROOT, parse, classify, level_sections, level_summary, sha
from analyze_animations import animation, image
from analyze_sounds import decode as sound

OUT = ROOT / "analysis/originals/old-gold"
EXPORT = ROOT / "decompiled/old-gold"
EXPECTED = {
    "OG.EXE": "7965e1d67128f196fbcf2bc449d97197f355c6b81cf043608fc9e01103ea043f",
    "OLD_GOLD.RES": "e376855167b64a7190ec2da0ba9bd1da129ba178c4033aebadebca3f08b2be25",
}


def write_json(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read_tsv(path):
    with path.open(encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def file_metadata(path):
    return dict(path=path.relative_to(ROOT).as_posix(), size=path.stat().st_size, sha256=sha(path.read_bytes()))


def level_inventory(payload):
    summary = level_summary(payload)
    sections = level_sections(payload)
    maps = {name: collections.Counter(b"".join(line for line in sections[name]
            if line and not line.startswith(b"#"))) for name in ("MMap", "TMap", "FCMap")}
    definitions = []
    bindings = dict(sprite=None, animation=None, sound=None)
    for line in sections["Game Object"]:
        if not line or line.startswith(b"#"):
            continue
        if b"=" in line:
            key, value = line.split(b"=", 1)
            key = key.strip().lower()
            if key in (b"sprite", b"flic", b"sound"):
                target = {b"sprite": "sprite", b"flic": "animation", b"sound": "sound"}[key]
                bindings[target] = value.strip().decode("cp866") or None
            continue
        fields = line.split()
        if len(fields) < 6 or len(fields[0]) != 1 or not fields[1].isdigit():
            raise ValueError(f"Unrecognized object header: {line!r}")
        glyph = fields[0][0]
        if glyph == ord("_"):
            glyph = 32
        obj = dict(glyph_hex=f"{glyph:02x}", glyph=bytes([glyph]).decode("cp866"),
                   class_id=int(fields[1]), flags=int(fields[2]), width=int(fields[3]),
                   height=int(fields[4]), marker=int(fields[5]),
                   placements={name: counts[glyph] for name, counts in maps.items()})
        if obj["class_id"] in (3, 10):
            start = 6
            if obj["class_id"] == 10:
                obj["weapon_slot"] = int(fields[start])
                start += 1
            obj.update(category=int(fields[start]), value=int(fields[start+1]))
        if obj["class_id"] == 8:
            obj["projectile_payload_tokens"] = [v.decode("cp866") for v in fields[6:]]
        # Bindings are parser state. Class-specific consumers still require ABI validation.
        obj["inherited_bindings"] = bindings.copy()
        definitions.append(obj)
    if len({d["glyph_hex"] for d in definitions}) != len(definitions):
        raise ValueError("Duplicate glyph")
    summary["definitions"] = definitions
    summary["placed_category99"] = sum(d["placements"]["TMap"] for d in definitions if d.get("category") == 99)
    summary["total_category99_value"] = sum(d["placements"]["TMap"] * d["value"] for d in definitions if d.get("category") == 99)
    return summary


def inspect_resources(path, label, extract=False):
    data = path.read_bytes()
    entries = parse(data)
    levels = {}
    formats = collections.Counter()
    for entry in entries:
        payload = data[entry["offset"]:entry["offset"]+entry["size"]]
        if extract:
            dest = EXPORT / "resources" / f"{entry['index']:04d}.bin"
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(payload)
            assert sha(dest.read_bytes()) == entry["sha256"]
        kind = classify(entry["name"], payload)
        if entry["name"] == "СВЕТ_Ч" and len(payload) == 4096:
            kind = "lighting_table"
        elif payload[2:5] == b"SVM":
            # First word is serialized native state, not the format signature.
            kind = "sound_signature_unparsed"
        elif entry["name"].startswith("INTRO") and not entry["name"].endswith("SPR"):
            kind = "text"
        if kind == "level_text":
            levels[entry["name"]] = level_inventory(payload)
            if not extract:
                # Keep only item/weapon descriptors for comparison; existing Kremlin
                # inventories already hold its full object metadata.
                inventory = levels[entry["name"]]
                inventory["definitions"] = [d for d in inventory["definitions"] if "category" in d]
                inventory["definition_subset"] = "item_and_weapon_descriptors_only"
            if extract:
                (EXPORT / "resources" / f"{entry['index']:04d}.txt").write_text(
                    payload.decode("cp866").replace("\r\n", "\n"), encoding="utf-8")
        elif kind == "indexed_image18":
            entry["image"], _ = image(payload)
        elif kind == "sound_signature_unparsed":
            entry["sound"] = sound(payload)[0]
            samples = payload[36:]
            entry["sound"].update(native_word=struct.unpack_from("<H", payload)[0],
                sample_min=min(samples), sample_max=max(samples),
                sample_sha256=sha(samples), terminal_byte=samples[-1])
            kind = "svm_pcm8"
        elif kind == "unparsed":
            try:
                frames, _ = animation(payload)
                kind = "animation8_10_image18"
                entry["animation"] = dict(frames=len(frames), intervals=[f["interval_ticks"] for f in frames])
            except ValueError:
                pass
        entry["kind"] = kind
        formats[kind] += 1
    result = dict(**file_metadata(path), count=len(entries), formats=dict(formats), levels=levels)
    if extract:
        with (OUT / "resource-catalog.tsv").open("w", encoding="utf-8", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["index", "name", "offset", "size", "kind", "sha256"], delimiter="\t", extrasaction="ignore")
            writer.writeheader()
            writer.writerows(entries)
        write_json("resource-formats.json", entries)
    return result


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    original = ROOT / "originals/Old_Gold_3D"
    before = {p.name: sha(p.read_bytes()) for p in original.iterdir() if p.is_file()}
    for name, expected in EXPECTED.items():
        if before[name] != expected:
            raise ValueError(f"Unexpected sample {name}")
    manifest = json.loads((EXPORT / "manifest.json").read_text(encoding="utf-8-sig"))
    for key in ("sha256_before", "sha256_after"):
        assert manifest[key] == EXPECTED
    for item in manifest["scripts"]:
        assert sha((ROOT / item["path"]).read_bytes()) == item["sha256"], item["path"]
    functions = read_tsv(EXPORT / "functions.tsv")
    summary = dict(line.split("=", 1) for line in (EXPORT / "summary.txt").read_text().splitlines())
    assert int(summary["functions"]) == len(functions) == int(summary["decompiled"])
    assert int(summary["failed"]) == 0
    assert {r["file"] for r in functions} == {p.relative_to(EXPORT).as_posix() for p in (EXPORT / "functions").glob("*.c")}
    warning_functions, diagnostics = [], []
    for row in functions:
        assert row["status"] == "ok" and row["file"]
        code = (EXPORT / row["file"]).read_text(encoding="utf-8")
        assert code.strip()
        if "WARNING:" in code:
            warning_functions.append(row["address"])
        if row["diagnostic"]:
            diagnostics.append(dict(address=row["address"], diagnostic=row["diagnostic"]))
    log_bytes = (EXPORT / "ghidra.log").read_bytes()
    log = log_bytes.decode("utf-16" if log_bytes.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
    assert "OLD_GOLD_EXPORT_COMPLETE" in log
    assert not any(line.startswith("ERROR") for line in log.splitlines())
    assert "Analysis timed out" not in log
    blocks = read_tsv(EXPORT / "blocks.tsv")
    code_blocks = [b for b in blocks if b["execute"] == b["initialized"] == "true"]
    metrics = {key: sum(int(b[key]) for b in code_blocks) for key in ("size", "instruction_bytes", "function_bytes")}
    # DOS loader marks data and alignment bytes executable too; this is a physical byte metric only.
    metrics["function_bytes_percent"] = round(100*metrics["function_bytes"]/metrics["size"], 4)
    exe = (original / "OG.EXE").read_bytes()
    header = struct.unpack_from("<14H", exe)
    mz_size = (header[2]-1)*512+header[1] if header[1] else header[2]*512
    switches = []
    for entry, jump, table, count in (("1a0c:005c", "1a0c:0219", "1a0c:0724", 6),
                                      ("1ae1:0003", "1ae1:0017", "1ae1:0142", 14),
                                      ("1ae1:2eea", "1ae1:2fcf", "1ae1:30c5", 8),
                                      ("20c4:03b0", "20c4:055d", "20c4:0d62", 4)):
        seg, off = (int(v, 16) for v in table.split(":"))
        physical = header[4]*16 + seg*16 + off - 0x10000
        offsets = struct.unpack_from(f"<{count}H", exe, physical)
        targets = [f"{seg:04x}:{n:04x}" for n in offsets]
        switches.append(dict(function=entry, jump=jump, table=table, file_offset=physical, targets=targets))
        # Full table bytes are independently read from the untouched EXE.
        listing = (EXPORT / "listing.asm").read_text()
        for target in targets:
            assert target + "\t" in listing, target
    resources = {"old_gold": inspect_resources(original / "OLD_GOLD.RES", "old_gold", True)}
    for language, directory, base in (("ru", "RUS", "PKR"), ("en", "ENG", "PKE")):
        resources[language] = inspect_resources(ROOT / f"originals/pkr/{directory}/{base}.RES", language)
    write_json("data-comparison.json", resources)
    # Existing native C reader independently checks all directory fields and payload FNVs.
    tool = ROOT / "build/gba/host/dok-res.exe"
    completed = subprocess.run([str(tool), "--list", str(original / "OLD_GOLD.RES")], check=True, capture_output=True, text=True)
    native = list(csv.DictReader(io.StringIO(completed.stdout), delimiter="\t"))
    python = parse((original / "OLD_GOLD.RES").read_bytes())
    assert len(native) == len(python)
    for a, b in zip(native, python):
        for key in ("index", "name_hex", "offset", "size", "fnv1a32"):
            assert a[key] == str(b[key]), (key, a, b)
    native_decoders = collections.Counter()
    catalog = json.loads((OUT / "resource-formats.json").read_text(encoding="utf-8"))
    for entry in catalog:
        source = EXPORT / "resources" / f"{entry['index']:04d}.bin"
        payload = source.read_bytes()
        if entry["kind"] == "indexed_image18":
            expected = [image(payload)[0]]
            args = [str(ROOT / "build/gba/host/dok-anim.exe"), "--image", str(source)]
        elif entry["kind"] == "animation8_10_image18":
            expected = animation(payload)[0]
            args = [str(ROOT / "build/gba/host/dok-anim.exe"), "--list", str(source)]
        elif entry["kind"] == "svm_pcm8":
            expected = [sound(payload)[0]]
            args = [str(ROOT / "build/gba/host/dok-sound.exe"), "--list", str(source)]
        else:
            continue
        result = subprocess.run(args, check=True, capture_output=True, text=True)
        actual = list(csv.DictReader(io.StringIO(result.stdout), delimiter="\t"))
        assert len(actual) == len(expected)
        for a, b in zip(actual, expected):
            assert all(value == str(b[key]) for key, value in a.items()), entry["name"]
        native_decoders[entry["kind"]] += 1
    level_probe = subprocess.run([str(ROOT / "build/gba/host/dok-level.exe"), str(EXPORT / "resources/0007.bin")],
                                 capture_output=True, text=True)
    # Existing Kremlin grammar is probed, not silently used to interpret the older LEVEL dialect.
    level_compatibility = dict(exit_code=level_probe.returncode, diagnostic=level_probe.stderr.strip(),
                               compatible_without_changes=level_probe.returncode == 0)
    artifacts = [p for p in EXPORT.iterdir() if p.is_file()]
    after = {p.name: sha(p.read_bytes()) for p in original.iterdir() if p.is_file()}
    assert before == after
    recheck_dir = ROOT / "research/evidence/old-gold/recheck-export"
    repeat = None
    if (recheck_dir / "manifest.json").exists():
        other = json.loads((recheck_dir / "manifest.json").read_text(encoding="utf-8-sig"))
        assert other["sha256_before"] == other["sha256_after"] == EXPECTED
        for item in other["scripts"]:
            assert sha((ROOT / item["path"]).read_bytes()) == item["sha256"]
        names = ("functions.tsv", "blocks.tsv", "listing.asm", "references.tsv", "unrecovered.tsv", "decompiled.c", "strings.tsv")
        equal = {name: (EXPORT / name).read_bytes() == (recheck_dir / name).read_bytes() for name in names}
        assert all(equal.values()), equal
        a, b = ((d / "with_types.c").read_text(encoding="utf-8") for d in (EXPORT, recheck_dir))
        # Ghidra may enumerate the two independent MZ header types in either order.
        def normalized_types(text):
            import re
            types = re.findall(r"typedef struct OLD_IMAGE_DOS_.*?\n};", text, flags=re.S)
            rest = re.sub(r"typedef struct OLD_IMAGE_DOS_.*?\n};", "", text, flags=re.S)
            return sorted(types), "\n".join(line for line in rest.splitlines() if line.strip())
        assert normalized_types(a) == normalized_types(b)
        repeat = dict(project=other["project"], export=other["export"], byte_identical=equal,
                      with_types_equal_after_header_type_order_normalization=True,
                      with_types_byte_identical=a == b)
    report = dict(date="2026-10-26", ghidra=manifest,
                  input_files=[file_metadata(p) for p in sorted(original.iterdir()) if p.is_file()],
                  originals_unchanged=True, mz=dict(declared_size=mz_size, header_bytes=header[4]*16,
                  relocation_count=header[3], trailing_bytes=len(exe)-mz_size),
                  summary=summary, coverage=metrics, warning_functions=warning_functions,
                  unique_log_warnings=sorted(set(line for line in log.splitlines() if line.startswith("WARN"))),
                  functions_with_diagnostics=diagnostics, switch_overrides=switches,
                  clean_import_recheck=repeat,
                  native_resource_reader=dict(entries_verified=len(native), executable=file_metadata(tool)),
                  native_decoders=dict(resources_verified=dict(native_decoders), executables=[
                      file_metadata(ROOT / "build/gba/host" / name) for name in ("dok-anim.exe", "dok-sound.exe", "dok-level.exe")]),
                  kremlin_level_parser_probe=level_compatibility,
                  exporter_checks=dict(indexed_functions_present=True, all_decompiled=True,
                  no_log_errors=True, no_analysis_timeout=True),
                  exports=[file_metadata(p) for p in sorted(artifacts)],
                  verifier_sources=[file_metadata(Path(__file__).resolve())] + [file_metadata(ROOT / "ports/gba/scripts" / name)
                    for name in ("analyze_resources.py", "analyze_animations.py", "analyze_sounds.py")],
                  limitations=["Automatic C is not buildable or behavior-verified source.",
                    "Executable block coverage includes data; not a semantic recovery percentage.",
                    "CS jump overrides preserve targets, not high-level switch expressions or case labels.",
                    "Resource decoder checks validate bytes, not DOS execution equivalence.",
                    "DOS gameplay, original audio hardware and SDL2 execution were not tested."])
    write_json("verification.json", report)
    print(json.dumps(dict(functions=len(functions), warning_functions=len(warning_functions), coverage=metrics,
          resource_formats=resources["old_gold"]["formats"], native_res_entries=len(native)), ensure_ascii=False))


if __name__ == "__main__":
    main()
