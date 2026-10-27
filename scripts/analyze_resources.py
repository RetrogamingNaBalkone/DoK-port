"""Verify and extract both RES archives; compare every payload with the C reader.

Only metadata enters analysis/. Game bytes and decoded files stay in build/.
"""
import argparse
import collections
import csv
import hashlib
import io
import json
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return f"{value:08x}"


def parse(data):
    if len(data) < 2:
        raise ValueError("Truncated header")
    count, = struct.unpack_from("<H", data)
    end = 2 + count * 30
    if end > len(data):
        raise ValueError("Truncated directory")
    entries = []
    for i in range(count):
        record = data[2 + i * 30:32 + i * 30]
        name = record[:16].split(b"\0", 1)[0]
        if not 0 < len(name) < 16:
            raise ValueError("Invalid name")
        size, offset = struct.unpack_from("<II", record, 16)
        if offset < end or offset > len(data) or size > len(data) - offset:
            raise ValueError("Invalid payload range")
        payload = data[offset:offset + size]
        entries.append(dict(index=i, name=name.decode("cp866"), name_hex=name.hex(),
                            size=size, offset=offset, sha256=sha(payload), fnv1a32=fnv(payload)))
    return entries


def level_sections(data):
    sections = collections.defaultdict(list)
    section = None
    # Keep bytes and whitespace: map symbols are CP866 bytes, including spaces.
    for line in data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n"):
        stripped = line.strip()
        if stripped.startswith(b"[") and stripped.endswith(b"]"):
            section = stripped[1:-1].decode("ascii")
        elif section is not None:
            sections[section].append(line)
    return sections


def level_summary(data):
    sections = level_sections(data)
    settings = {}
    for line in sections["Game Ini"]:
        if b"=" in line and not line.lstrip().startswith(b"#"):
            key, value = line.split(b"=", 1)
            settings[key.strip().decode("ascii")] = int(value.strip())
    width, height = settings["Level Width"], settings["Level Length"]
    maps = {}
    for name in ("MMap", "TMap", "FCMap"):
        rows = [line for line in sections[name] if line and not line.startswith(b"#")]
        if len(rows) != height or any(len(row) != width for row in rows):
            raise ValueError(f"Unexpected {name} geometry: {len(rows)} rows, lengths {sorted(set(map(len,rows)))}")
        maps[name] = dict(width=width, height=height, sha256=sha(b"".join(rows)))
    definitions = {}
    for line in sections["Game Object"]:
        if line.lstrip().startswith(b"#"):
            continue
        parts = line.split()
        if len(parts) >= 2 and len(parts[0]) == 1 and parts[1].isdigit():
            definitions[parts[0][0]] = int(parts[1])
    return dict(settings=settings, maps=maps,
                object_definitions=len(definitions),
                native_type_counts=dict(sorted(collections.Counter(definitions.values()).items())))


def classify(name, data):
    if name in ("FONT8", "FONT16") and len(data) in (2048, 4096):
        return "bitmap_font"
    if name.startswith("LEVEL"):
        return "level_text"
    if len(data) == 768 and max(data) <= 63:
        return "vga_palette"
    if name.startswith("LIGHT") and len(data) == 4096:
        return "lighting_table"
    if len(data) >= 18:
        w, h = struct.unpack_from("<HH", data)
        size, = struct.unpack_from("<I", data, 10)
        if w and h and size == w * h and len(data) == 18 + size:
            return "indexed_image18"
    if data.startswith(b"\xfd\x04SVM"):
        return "sound_signature_unparsed"
    if name in ("README", "ENDTXT", "PANEL_DAT") or name.startswith(("BINTRO", "EINTRO")):
        return "text"
    return "unparsed"


def run(tool):
    reports = {}
    for lang in ("ru", "en"):
        inventory = json.loads((ROOT / "analysis" / f"inventory-{lang}.json").read_text())
        source = ROOT / inventory["resource"]["file"]
        data = source.read_bytes()
        if sha(data) != inventory["resource"]["sha256"]:
            raise ValueError(f"Unexpected original: {source}")
        entries = parse(data)
        completed = subprocess.run([str(tool), "--list", str(source)], check=True, capture_output=True, text=True)
        native = list(csv.DictReader(io.StringIO(completed.stdout), delimiter="\t"))
        if len(native) != len(entries):
            raise ValueError("C and Python entry counts differ")
        output = ROOT / "research/evidence/resources" / lang
        output.mkdir(parents=True, exist_ok=True)
        levels = {}
        for entry, other in zip(entries, native):
            for key in ("index", "name_hex", "offset", "size", "fnv1a32"):
                if str(entry[key]) != other[key]:
                    raise ValueError(f"C/Python mismatch: {entry['index']} {key}")
            payload = data[entry["offset"]:entry["offset"] + entry["size"]]
            path = output / f"{entry['index']:04d}.bin"
            path.write_bytes(payload)
            if sha(path.read_bytes()) != entry["sha256"]:
                raise ValueError("Extracted bytes differ")
            kind = classify(entry["name"], payload)
            entry["kind"] = kind
            if kind in ("level_text", "text"):
                (output / f"{entry['index']:04d}.txt").write_text(payload.decode("cp866").replace("\r\n", "\n"), encoding="utf-8", newline="\n")
            if kind == "level_text":
                levels[entry["name"]] = level_summary(payload)
            if kind == "indexed_image18":
                width, height = struct.unpack_from("<HH", payload)
                entry["image"] = dict(width=width, height=height, flags=struct.unpack_from("<H", payload, 4)[0])
                (output / f"{entry['index']:04d}.pgm").write_bytes(f"P5\n{width} {height}\n255\n".encode()+payload[18:])
            if kind == "bitmap_font":
                height = len(payload) // 256
                atlas = bytearray(128 * height * 16)
                for glyph in range(256):
                    for y in range(height):
                        row = payload[glyph * height + y]
                        for x in range(8):
                            atlas[((glyph // 16) * height + y) * 128 + (glyph % 16) * 8 + x] = 255 if row & (128 >> x) else 0
                (output / f"{entry['index']:04d}.pgm").write_bytes(f"P5\n128 {height*16}\n255\n".encode()+atlas)
        intervals = sorted((e["offset"], e["offset"] + e["size"]) for e in entries)
        contiguous = intervals[0][0] == 2 + 30 * len(entries) and all(a[1] == b[0] for a,b in zip(intervals,intervals[1:]))
        report = dict(file=inventory["resource"]["file"], sha256=sha(data), count=len(entries),
                      directory_end=2+30*len(entries), contiguous_payloads=contiguous,
                      trailing_bytes=len(data)-intervals[-1][1], trailing_hex=data[intervals[-1][1]:].hex(),
                      categories=dict(collections.Counter(e["kind"] for e in entries)), entries=entries, levels=levels,
                      c_reader_payload_checks=len(native))
        reports[lang] = report
        (ROOT / "analysis" / f"resources-{lang}.json").write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
        print(lang,report["count"],report["categories"],"C/Python payload hashes agree")
    matches = []
    en = {entry["name_hex"]: entry for entry in reports["en"]["entries"]}
    for entry in reports["ru"]["entries"]:
        other = en.get(entry["name_hex"])
        if other:
            matches.append(dict(name=entry["name"], ru_index=entry["index"], en_index=other["index"], identical=entry["sha256"] == other["sha256"]))
    (ROOT / "analysis/resources-comparison.json").write_text(json.dumps(matches, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    map_differences = {}
    for level in ("LEVEL1", "LEVEL2", "LEVEL3"):
        maps = {}
        for lang in ("ru", "en"):
            entry = next(e for e in reports[lang]["entries"] if e["name"] == level)
            maps[lang] = level_sections((ROOT / "research/evidence/resources" / lang / f"{entry['index']:04d}.bin").read_bytes())
        map_differences[level] = {}
        width = reports["ru"]["levels"][level]["settings"]["Level Width"]
        for name in ("MMap", "TMap", "FCMap"):
            flat = {lang: b"".join(row for row in maps[lang][name] if row and not row.startswith(b"#")) for lang in maps}
            if len(flat["ru"]) != len(flat["en"]):
                raise ValueError("Map geometry differs")
            cells = [dict(x=i % width, y=i // width, ru=f"{a:02x}", en=f"{b:02x}")
                     for i,(a,b) in enumerate(zip(flat["ru"], flat["en"])) if a != b]
            map_differences[level][name] = dict(changed_cells=len(cells), cells=cells)
    (ROOT / "analysis/level-map-differences.json").write_text(json.dumps(map_differences, indent=2)+"\n", encoding="utf-8")
    print("Same-name resources:",len(matches),"identical:",sum(m["identical"] for m in matches))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=ROOT / "build/native/dok-res.exe")
    run(parser.parse_args().tool.resolve())
