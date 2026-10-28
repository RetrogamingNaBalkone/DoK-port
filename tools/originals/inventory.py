"""Read-only NE inventory and conservative RUS/ENG segment correspondence.

Python 3.11+, standard library only. Does not execute or alter game files.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[2]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import csv
import hashlib
import json
import struct
import zlib
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    "ru": ("RUS/PKR.EXE", "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28"),
    "en": ("ENG/PKE.EXE", "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b"),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def parse_ne(data):
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError(f"Truncated NE at {offset:#x}")
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b"MZ":
        raise ValueError("Not MZ")
    ne = unpack("<I", 60)[0]
    if data[ne:ne + 2] != b"NE":
        raise ValueError("Not NE")
    count = unpack("<H", ne + 28)[0]
    table = ne + unpack("<H", ne + 34)[0]
    shift = unpack("<H", ne + 50)[0]
    segments = []
    for i in range(count):
        sector, length, flags, allocation = unpack("<4H", table + 8 * i)
        length = length or 65536
        start = sector << shift if sector else None
        payload = data[start:start + length] if start is not None else b""
        if start is not None and len(payload) != length:
            raise ValueError("Truncated segment")
        normalized = bytearray(payload)
        records = []
        fixups = set()
        if flags & 0x100 and start is not None:
            n = unpack("<H", start + length)[0]
            for j in range(n):
                kind, target_flags, source, target, target_offset = unpack("<BBHHH", start + length + 2 + 8*j)
                width = {0: 1, 2: 2, 3: 4, 5: 2, 11: 6, 13: 4}.get(kind)
                if width is None:
                    raise ValueError(f"Unsupported fixup {kind}")
                cursor, seen, sources = source, set(), []
                while cursor != 0xffff:
                    if cursor in seen or cursor + max(width, 2) > length:
                        raise ValueError("Invalid fixup chain")
                    seen.add(cursor)
                    sources.append(cursor)
                    fixups.update(range(cursor, cursor + width))
                    normalized[cursor:cursor + width] = b"\0" * width
                    if target_flags & 4:
                        break
                    cursor = struct.unpack_from("<H", payload, cursor)[0]
                records.append(dict(kind=kind, flags=target_flags, source=source, target=target,
                                    target_offset=target_offset, chain=sources))
        segments.append(dict(index=i+1, file_offset=start, length=length, flags=f"0x{flags:04x}",
                             kind="data" if flags & 1 else "code", allocation=allocation or 65536,
                             sha256=sha(payload), fixup_masked_sha256=sha(normalized),
                             fixup_bytes=len(fixups), relocations=records))
    return dict(ne_offset=ne, segment_table=table, alignment_shift=shift,
                entry_ip=unpack("<H", ne+20)[0], entry_segment=unpack("<H", ne+22)[0],
                automatic_data_segment=unpack("<H", ne+14)[0], target_os=data[ne+54], segments=segments)


def tsv(path):
    with path.open(encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter="\t"))


def run(wasdify):
    out = ROOT / "analysis/originals"
    out.mkdir(exist_ok=True)
    inventories = {}
    for language, (relative, expected) in EXPECTED.items():
        path = ROOT / "originals/pkr" / relative
        data = path.read_bytes()
        if sha(data) != expected:
            raise ValueError(f"Reference hash mismatch: {path}")
        report = dict(file=path.relative_to(ROOT).as_posix(), size=len(data), sha256=sha(data), **parse_ne(data))
        resource = path.with_suffix(".RES")
        resource_data = resource.read_bytes()
        report["resource"] = dict(file=resource.relative_to(ROOT).as_posix(), size=len(resource_data),
                                  sha256=sha(resource_data), crc32=f"{zlib.crc32(resource_data):08X}")
        inventories[language] = report
        (out / f"inventory-{language}.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")

    groups = {}
    for language, report in inventories.items():
        by_hash = defaultdict(list)
        for segment in report["segments"]:
            if segment["file_offset"] is not None:
                by_hash[(segment["kind"], segment["length"], segment["fixup_masked_sha256"])].append(segment)
        groups[language] = by_hash
    with (out / "segment-matches.tsv").open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(["ru_segment", "en_segment", "kind", "length", "evidence"])
        for key, ru in groups["ru"].items():
            en = groups["en"].get(key, [])
            if len(ru) == len(en) == 1:
                writer.writerow([ru[0]["index"], en[0]["index"], key[0], key[1], "unique_fixup_masked_byte_match"])

    with (out / "wasdify-sources.json").open("w", encoding="utf-8") as f:
        paths = sorted((wasdify / "docs/games/dungeons-of-kremlin").glob("*.md"))
        paths += [wasdify / "scripts" / name for name in (
            "GhidraPkrPerformance.java", "GhidraKremlinWeapons.java", "GhidraKremlinSaveInventory.java", "analyze_kremlin_save.ps1")]
        paths += sorted((wasdify / "src/wasdify/patches").glob("kremlin*"))
        json.dump([dict(path=p.relative_to(wasdify).as_posix(), sha256=sha(p.read_bytes())) for p in paths if p.is_file()], f, indent=2)
        f.write("\n")

    roles = {}
    seeded_roles = {}
    for language in EXPECTED:
        export = ROOT / "decompiled" / language
        if not (export / "summary.txt").exists():
            continue
        blocks = tsv(export / "blocks.tsv")
        code = [b for b in blocks if b["execute"] == "true" and b["initialized"] == "true"]
        metrics = dict(line.split("=", 1) for line in (export / "summary.txt").read_text().splitlines())
        metrics.update(code_bytes=sum(int(b["size"]) for b in code),
                       instruction_bytes=sum(int(b["instruction_bytes"]) for b in code),
                       function_bytes=sum(int(b["function_bytes"]) for b in code))
        functions = tsv(export / "functions.tsv")
        if len(functions) != int(metrics["functions"]):
            raise ValueError("Function count differs from summary")
        metrics["warning_functions"] = sum("WARNING:" in (export / row["file"]).read_text(encoding="utf-8")
                                          for row in functions if row["status"] == "ok")
        metrics["functions_with_diagnostics"] = sum(bool(row["diagnostic"]) for row in functions)
        external = next(b["start"].split(":")[0] for b in blocks if b["name"] == "EXTERNAL")
        metrics["external_stubs"] = sum(row["address"].split(":")[0] == external for row in functions)
        roles[language] = {row["name"]: row["address"] for row in functions if not row["name"].startswith("FUN_")}
        seeded_roles[language] = {line.split("\t")[2] for line in (out / f"seeds-{language}.tsv").read_text(encoding="utf-8").splitlines()
                                  if line and not line.startswith("#")}
        for role in seeded_roles[language]:
            if role not in roles[language]:
                raise ValueError(f"Missing seed role: {role}")
        (out / f"coverage-{language}.json").write_text(json.dumps(metrics, indent=2)+"\n", encoding="utf-8")
    if len(roles) == 2:
        with (out / "function-roles.tsv").open("w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f, delimiter="\t")
            writer.writerow(["role", "ru_ghidra", "en_ghidra", "evidence"])
            for role in sorted(seeded_roles["ru"] & seeded_roles["en"]):
                if role != "entry":
                    writer.writerow([role, roles["ru"][role], roles["en"][role], "WASDify_role_seed_not_full_semantic_equivalence"])
    print("NE inventories, segment matches, provenance and available coverage reports written to analysis/")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wasdify", type=Path, default=Path("C:/Source/wasdify"))
    run(parser.parse_args().wasdify)
