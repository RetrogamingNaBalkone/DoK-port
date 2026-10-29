#!/usr/bin/env python3
"""Independent N05 original LEVEL/profile oracle; no port implementation imports.

Original resources and full canonical exports stay in ignored build/research.
This scanner follows the documented original grammar, not the new C parser.
"""
from __future__ import annotations

import argparse
import collections
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = ROOT / "research/evidence/nds/n05"
OUT = ROOT / "build/nds/n05/oracle"
GOLDEN = EVIDENCE / "oracle"
ARCHIVES = (
    (1, "ru", "originals/pkr/RUS/PKR.RES", "af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816"),
    (2, "en", "originals/pkr/ENG/PKE.RES", "865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458"),
)
SETTING_KEYS = (b"Level Width", b"Level Height", b"Level Length", b"Cell Side",
                b"Cell Height", b"Floor Color", b"Ceiling Color", b"Min Dis")
W, L, C, I, A, S, T = range(7)
SURFACE = [(W, 0x54), (I, 0x56), (I, 0x5a), (I, 0x5e), (I, 0x62)]
MOTION = [(L, 0x38), (W, 0x32), (W, 0x34)]
SCHEMAS = {
    0: SURFACE,
    1: MOTION + [(W, n) for n in range(0x5e, 0x74, 2)] +
       [(S, 0xa2), (S, 0xa6), (W, 0x76), (W, 0x78), (W, 0x7a)],
    2: [], 3: [(W, 0x54), (W, 0x56), (T, 0x58)], 4: [], 5: MOTION, 6: [],
    7: [(W, 0x5c), (W, 0x5e), (L, 0x66)],
    8: [(L, 0x3c), (W, 0x5a), (A, 0x77), (W, 0x5c), (A, 0x7b)],
    9: MOTION + [(A, 0xe8), (A, 0xec), (S, 0xf0), (S, 0xf4), (W, 0xa4),
                 (L, 0xba), (C, 0xe7), (W, 0xa6), (W, 0xa8), (W, 0xae), (T, 0xbe)],
    10: [(W, 0x81), (W, 0x54), (W, 0x56), (T, 0x58)],
    12: SURFACE + [(W, 0x66), (W, 0x68)],
    13: SURFACE + [(C, 0x66), (L, 0x67)],
}
WHITESPACE = b" \t\v\f\r\n"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def originals() -> dict:
    result = {}
    for language, label, relative, expected in ARCHIVES:
        path = ROOT / relative
        data = path.read_bytes()
        require(sha(data) == expected, f"Unexpected original archive: {relative}")
        require(len(data) >= 4382 and int.from_bytes(data[:2], "little") == 146, "Original RES directory")
        entries, cursor = [], 4382
        for index in range(146):
            row = data[2 + 30 * index:32 + 30 * index]
            name = row[:16].split(b"\0", 1)[0]
            size, offset = struct.unpack_from("<II", row, 16)
            require(0 < len(name) <= 15 and offset == cursor and size <= len(data) - offset,
                    "Original RES resource bounds")
            payload = data[offset:offset + size]
            entries.append(dict(index=index, name=name.hex(), offset=offset, bytes=size,
                                sha256=sha(payload), payload=payload))
            cursor += size
        require(len(data) - cursor == (1 if language == 1 else 4), "Original RES trailer")
        result[language] = dict(label=label, path=path, relative=relative, sha256=expected, entries=entries)
    return result


def parse_level(data: bytes) -> dict:
    """Preserve source byte ranges, inherited declarations and full text tails."""
    require(b"\0" not in data, "LEVEL contains NUL")
    settings, objects = {}, []
    maps = {key: [] for key in (b"TMap", b"MMap", b"FCMap")}
    inherited = {key: dict(hex="", offset=0, bytes=0) for key in ("sprite", "animation", "sound")}
    sprite_flags, section, absolute, ignored, truncated, palette, light = 0, b"", 0, 0, 0, None, None

    def span(start: int, size: int) -> dict:
        return dict(hex=data[start:start + size].hex(), offset=start, bytes=size)

    for match in re.finditer(rb"([^\r\n]*)(?:\r\n|\r|\n|$)", data):
        line, absolute = match[1], match.start(1)
        if not line or line.startswith(b"#"):
            continue
        if line.startswith(b"["):
            require(line.endswith(b"]"), "LEVEL section terminator")
            section = line[1:-1]
            require(section in (b"Game Ini", b"Palette", b"Light", b"Game Object", *maps), "LEVEL section")
            continue
        if section == b"Game Ini":
            key, value = line.split(b"=", 1)
            key = key.strip()
            if key == b"Step Ver":
                ignored += 1
            else:
                require(key in SETTING_KEYS and key not in settings, "LEVEL setting")
                settings[key] = int(value)
            continue
        if section in (b"Palette", b"Light"):
            equal = line.index(b"=") + 1
            start = equal + len(line[equal:]) - len(line[equal:].lstrip(b" "))
            value = span(absolute + start, len(line) - start)
            if section == b"Palette":
                palette = value
            else:
                light = value
            continue
        if section in maps:
            maps[section].append(line)
            continue
        require(section == b"Game Object", "Unexpected LEVEL content")
        if line.startswith((b"Sprite", b"Flic", b"Sound")):
            equal = line.index(b"=") + 1
            start = equal + len(line[equal:]) - len(line[equal:].lstrip(b" "))
            if line.startswith(b"Sprite"):
                name = re.match(rb"\S+", line[start:])
                require(name is not None, "Sprite name")
                inherited["sprite"] = span(absolute + start, len(name[0]))
                sprite_flags = int(line[start + len(name[0]):])
            else:
                key = "animation" if line.startswith(b"Flic") else "sound"
                inherited[key] = span(absolute + start, len(line) - start)
            continue
        cursor = 0

        def token() -> tuple[bytes, int]:
            nonlocal cursor
            while cursor < len(line) and line[cursor] in WHITESPACE:
                cursor += 1
            start = cursor
            while cursor < len(line) and line[cursor] not in WHITESPACE:
                cursor += 1
            require(cursor > start, "Missing LEVEL token")
            return line[start:cursor], absolute + start

        glyph, _ = token()
        require(len(glyph) == 1, "LEVEL glyph width")
        numbers = []
        for i in range(5):
            raw, _ = token()
            number = int(raw)
            require((0 <= number <= 65535) if i < 2 else (-32768 <= number <= 32767), "LEVEL common number width")
            numbers.append(number)
        class_id, flags, width, height, marker = numbers
        require(class_id in SCHEMAS, "LEVEL class")
        fields, tail_start = [], cursor
        for kind, offset in SCHEMAS[class_id]:
            if kind == T:
                raw, start = line[cursor:], absolute + cursor
                cursor = len(line)
                value = span(start, len(raw))
                value["native_bytes"] = min(39, len(raw))
                truncated += len(raw) > 39
            elif kind == C:
                while cursor < len(line) and line[cursor] in WHITESPACE:
                    cursor += 1
                require(cursor < len(line), "LEVEL char")
                value, start, raw = line[cursor], absolute + cursor, line[cursor:cursor + 1]
                cursor += 1
            else:
                raw, start = token()
                if kind in (W, L):
                    value = int(raw)
                    bits = 16 if kind == W else 32
                    require(-(1 << (bits - 1)) <= value < (1 << (bits - 1)), "LEVEL numeric field width")
                else:
                    require(len(raw) <= 15, "LEVEL resource reference length")
                    value = span(start, len(raw))
            fields.append(dict(kind=kind, native_offset=offset, value=value,
                               token=dict(offset=start, bytes=len(raw))))
        require(not line[cursor:].strip(), "Extra LEVEL tokens")
        objects.append(dict(glyph=32 if glyph == b"_" else glyph[0], class_id=class_id,
                            flags=flags, width=width, height=height, marker=marker & 255,
                            sprite_flags=sprite_flags, **copy.deepcopy(inherited), fields=fields,
                            line_offset=absolute, line_bytes=len(line),
                            tail_offset=absolute + tail_start, tail_bytes=len(line) - tail_start))
    require(len(settings) == 8 and palette is not None and light is not None, "LEVEL configuration completeness")
    require(len({o["glyph"] for o in objects}) == len(objects), "Duplicate LEVEL glyph")
    width, height, length = [settings[k] for k in SETTING_KEYS[:3]]
    require(width == length == 64 and height == 1, "Known original LEVEL geometry")
    for rows in maps.values():
        require(len(rows) == length and all(len(row) == width for row in rows), "LEVEL map geometry")
    return dict(settings=[settings[k] for k in SETTING_KEYS], palette=palette, light=light,
                ignored_step_ver=ignored, truncated_messages=truncated, objects=objects,
                maps=[b"".join(maps[k]).hex() for k in (b"TMap", b"MMap", b"FCMap")])


def canonical_level(model: dict, entry: dict, language: int, include_source: bool) -> dict:
    def extent(value: dict) -> list[int]:
        return [value["offset"], value["bytes"]]
    objects = []
    for obj in model["objects"]:
        objects.append(dict(class_id=obj["class_id"], flags=obj["flags"],
                            width_parameter=obj["width"], height_parameter=obj["height"],
                            sprite_flags=obj["sprite_flags"], glyph=obj["glyph"], marker=obj["marker"],
                            sprite=extent(obj["sprite"]), animation=extent(obj["animation"]), sound=extent(obj["sound"]),
                            raw_line=[obj["line_offset"], obj["line_bytes"]],
                            raw_tail=[obj["tail_offset"], obj["tail_bytes"]],
                            fields=[[f["native_offset"], f["kind"], f["value"] if isinstance(f["value"], int) else 0,
                                     *extent(f["token"])] for f in obj["fields"]]))
    glyph_index = [0] * 256
    for index, obj in enumerate(objects):
        glyph_index[obj["glyph"]] = index + 1
    result = dict(source_bytes=entry["bytes"], source_asset_id=(language << 30) | (entry["index"] << 18) | (14 << 10),
                  source_index=entry["index"], language=language, source_sha256=entry["sha256"],
                  settings=model["settings"], palette=extent(model["palette"]), light=extent(model["light"]),
                  ignored_step_ver=model["ignored_step_ver"], truncated_messages=model["truncated_messages"],
                  object_count=len(objects), glyph_index=glyph_index, objects=objects, maps=model["maps"])
    if include_source:
        result["source_hex"] = entry["payload"].hex()
    return result


def model_digest(model: dict, entry: dict, language: int) -> str:
    """Independent bytewise encoding of the specified immutable model digest."""
    canonical = canonical_level(model, entry, language, False)
    stream = bytearray(b"DOKNDS-N05-MODEL1\0")

    def span_bytes(value: list[int]) -> None:
        offset, length = value
        require(offset <= entry["bytes"] and length <= entry["bytes"] - offset, "Model digest span bounds")
        stream.extend(struct.pack("<II", offset, length))
        stream.extend(entry["payload"][offset:offset + length])

    stream.extend(struct.pack("<IIHBB", entry["bytes"], canonical["source_asset_id"], entry["index"], language, 0))
    stream.extend(bytes.fromhex(entry["sha256"]))
    stream.extend(struct.pack("<8hII", *model["settings"], model["ignored_step_ver"], model["truncated_messages"]))
    span_bytes(canonical["palette"])
    span_bytes(canonical["light"])
    stream.extend(struct.pack("<H256H", len(model["objects"]), *canonical["glyph_index"]))
    for obj in canonical["objects"]:
        stream.extend(struct.pack("<HHhhhBBHH", obj["class_id"], obj["flags"], obj["width_parameter"],
                                  obj["height_parameter"], obj["sprite_flags"], obj["glyph"], obj["marker"], len(obj["fields"]), 0))
        for key in ("sprite", "animation", "sound", "raw_line", "raw_tail"):
            span_bytes(obj[key])
        for offset, kind, value, token_offset, token_length in obj["fields"]:
            stream.extend(struct.pack("<HBBi", offset, kind, 0, value))
            span_bytes([token_offset, token_length])
    for plane in model["maps"]:
        stream.extend(bytes.fromhex(plane))
    return sha(stream)


def scalar(obj: dict, offset: int) -> int:
    values = [f["value"] for f in obj["fields"] if f["native_offset"] == offset and f["kind"] in (W, L, C)]
    require(len(values) == 1, "Missing/duplicate scalar")
    return values[0]


def expected_profile(source: dict, number: int) -> tuple[dict, list[dict], dict]:
    result, changes = copy.deepcopy(source), []
    boyar = "БОЯР_И".encode("cp866").hex()
    mouse = "МЫШЬ_Л".encode("cp866").hex()
    boyars = [o for o in result["objects"] if o["class_id"] == 9 and o["animation"]["hex"] == boyar]
    mice = [o for o in result["objects"] if o["class_id"] == 9 and o["animation"]["hex"] == mouse]
    groups = dict(boyars=sorted(o["glyph"] for o in boyars), mice=sorted(o["glyph"] for o in mice))
    if number != 1:
        return result, changes, groups
    require(len(boyars) == 33 and len(mice) == 30, "LEVEL1 semantic group fingerprints")
    by_glyph = {o["glyph"]: o for o in result["objects"]}

    def change(glyph: int, offset: int, before: int, after: int, reason: str) -> None:
        obj = by_glyph[glyph]
        require(scalar(obj, offset) == before, "Unexpected original profile scalar")
        field = next(f for f in obj["fields"] if f["native_offset"] == offset)
        field["value"] = after
        changes.append(dict(glyph=glyph, native_offset=offset, old=before, new=after, reason=reason))

    for glyph in (0x81, 0x86):
        require(scalar(by_glyph[glyph], 0xa4) == 42, "Special boyar HP invariant")
        change(glyph, 0x38, 60, 20, "accepted_english_boyar_speed")
        change(glyph, 0xba, 20000, 9000, "accepted_english_boyar_wake")
    change(0x8c, 0xa4, 42, 12, "accepted_english_boyar_health")
    for obj in boyars:
        change(obj["glyph"], 0xae, 10, 24, "accepted_english_boyar_course")
    baba = by_glyph[0xa0]
    require(baba["class_id"] == 9 and baba["animation"]["hex"] == "БАБА_А".encode("cp866").hex(), "Baba identity")
    require((scalar(baba, 0xa4), baba["width"], scalar(baba, 0xba)) == (54, 7, 3400), "Hybrid Baba invariants")
    change(0xa0, 0x38, 80, 60, "accepted_hybrid_baba_speed")
    require(all(scalar(o, 0xae) == 8 for o in mice), "Mouse course invariant")
    require(len(changes) == 39, "Exact accepted change count")
    return result, changes, groups


def initial_world(model: dict, epoch: int = 1) -> dict:
    """Documented stage6 preparation only; never assert unknown constructors."""
    width, height, length, side = model["settings"][:4]
    require(width == length and height == 1 and 0 < side <= 32767 and epoch > 0, "World profile")
    cells = width * length
    glyph_index = [0] * 256
    seeds = []
    for i, obj in enumerate(model["objects"]):
        glyph_index[obj["glyph"]] = i + 1
        seeds.append(dict(reference=[epoch, i + 1, 1], definition_index=i, class_id=obj["class_id"],
                          placements=0, pose_known=0, x=0, y=0, z=0, heading=0, upper_y=0, lower_y=0))
    converted = []
    for index in (1, 0, 2):
        source = bytes.fromhex(model["maps"][index])
        transformed = b"".join(source[row * width:(row + 1) * width] for row in range(length - 1, -1, -1))
        transformed = transformed.replace(b".", b" ")
        require(all(glyph_index[glyph] != 0 for glyph in transformed), "Undefined normalized runtime glyph")
        converted.append(transformed)
    converted.append(bytes(cells))
    category99, actor_placements, hero = 0, 0, [0, 0, 0]

    def signed16(number: int) -> int:
        return ((number + 32768) % 65536) - 32768

    for glyph in converted[1]:
        obj = model["objects"][glyph_index[glyph] - 1]
        if obj["class_id"] == 3 and scalar(obj, 0x54) == 99:
            category99 = signed16(category99 + scalar(obj, 0x56))
    for cell, glyph in enumerate(converted[0]):
        index = glyph_index[glyph] - 1
        obj, seed = model["objects"][index], seeds[index]
        if obj["class_id"] not in (1, 9):
            continue
        actor_placements += 1
        seed["placements"] += 1
        y = signed16(-obj["height"])
        seed.update(pose_known=1, x=side * (cell % width) + side // 2, y=y,
                    z=side * (cell // width) + side // 2, heading=scalar(obj, 0x32),
                    upper_y=signed16(y + (26 if obj["class_id"] == 1 else 0)),
                    lower_y=signed16(y - (26 if obj["class_id"] == 1 else 0)))
        if obj["class_id"] == 1:
            hero = list(seed["reference"])
    return dict(epoch=epoch, hero=hero, object_count=len(seeds), actor_placements=actor_placements,
                placed_actors=sum(s["placements"] > 0 for s in seeds), category99_total=category99,
                glyph_index=glyph_index, objects=seeds, maps=[plane.hex() for plane in converted])


def presentation(effective: dict, localized: dict, archive: dict, language: int) -> dict:
    primary = {1: 2, 2: 3, 3: 11, 4: 7, 5: 12, 6: 14, 7: 16, 8: 8}
    resources, by_name = [], {}
    for entry in archive["entries"]:
        name, block = bytes.fromhex(entry["name"]), entry["payload"]
        label = name.decode("cp866")
        if label in ("FONT8", "FONT16"):
            kind = 3
        elif label in ("LEVEL1", "LEVEL2", "LEVEL3"):
            kind = 6
        elif label.startswith("LIGHT"):
            kind = 5
        elif len(block) == 768 and max(block) <= 63:
            kind = 4
        elif block[2:5] == b"SVM":
            kind = 8
        elif label in ("README", "ENDTXT", "PANEL_DAT") or label.startswith(("BINTRO", "EINTRO")):
            kind = 7
        elif len(block) >= 18 and 18 + struct.unpack_from("<H", block)[0] * struct.unpack_from("<H", block, 2)[0] == len(block):
            kind = 1
        else:
            kind = 2
        resource_id = (language << 30) | (entry["index"] << 18) | (primary[kind] << 10)
        resources.append([resource_id, kind, (name + bytes(16 - len(name))).hex()])
        by_name[name.hex()] = (resource_id, kind)
    bindings = []

    def binding(glyph: int, slot: int, kind: int, value: dict, source_kind: int = 0) -> None:
        if kind == T:
            bindings.append([glyph, slot, kind, 0, bytes.fromhex(value["hex"])[:39].hex()])
        elif value["bytes"] == 0:
            bindings.append([glyph, slot, kind, 0, ""])
        else:
            resource_id, actual_kind = by_name[value["hex"]]
            require(actual_kind == source_kind, "Presentation reference kind")
            bindings.append([glyph, slot, kind, resource_id, ""])

    binding(0, 0xfff3, 7, localized["palette"], 4)
    binding(0, 0xfff4, 8, localized["light"], 5)
    by_glyph = {o["glyph"]: o for o in localized["objects"]}
    for common in effective["objects"]:
        obj = by_glyph[common["glyph"]]
        require(obj["class_id"] == common["class_id"], "Presentation class pairing")
        for key, slot, kind, source_kind in (("sprite", 0xfff0, I, 1), ("animation", 0xfff1, A, 2), ("sound", 0xfff2, S, 8)):
            binding(obj["glyph"], slot, kind, obj[key], source_kind)
        for field in obj["fields"]:
            if field["kind"] >= I:
                binding(obj["glyph"], field["native_offset"], field["kind"], field["value"], {I: 1, A: 2, S: 8, T: 0}[field["kind"]])
    return dict(language=language, resource_count=len(resources), resources=resources, bindings=bindings)


def expected_case(archives: dict, number: int, language: int, epoch: int | None = None) -> dict:
    if epoch is None:
        epoch = number
    ru_entry = archives[1]["entries"][number + 17]
    localized_entry = archives[language]["entries"][number + 17]
    base, localized = parse_level(ru_entry["payload"]), parse_level(localized_entry["payload"])
    effective, _, _ = expected_profile(base, number)
    return dict(base=canonical_level(base, ru_entry, 1, True),
                effective=canonical_level(effective, ru_entry, 1, False),
                localized=canonical_level(localized, localized_entry, language, language != 1),
                world=initial_world(effective, epoch),
                presentation=presentation(effective, localized, archives[language], language))


def safe_output(path: Path, root: Path) -> Path:
    """Reject traversal and filesystem aliases before creating output parents."""
    candidate = Path(path.absolute())
    fixed = Path(root.absolute())
    require(candidate.is_relative_to(fixed) and candidate != fixed, "Output outside fixed tree")
    current = ROOT
    for component in candidate.relative_to(ROOT).parts[:-1]:
        current /= component
        require(not current.is_symlink() and not (hasattr(current, "is_junction") and current.is_junction()), "Output parent alias")
        require(not current.exists() or current.is_dir(), "Output parent is file")
    require(candidate.resolve().is_relative_to(fixed.resolve()), "Output resolves outside fixed tree")
    require(not candidate.is_symlink() and not (hasattr(candidate, "is_junction") and candidate.is_junction()), "Output file alias")
    require(not candidate.exists() or (candidate.is_file() and candidate.stat().st_nlink == 1), "Output regular-file requirement")
    candidate.parent.mkdir(parents=True, exist_ok=True)
    return candidate


def write_json(path: Path, value: dict, root: Path = EVIDENCE) -> None:
    safe_output(path, root).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def mismatch(expected, actual, path: str = "$") -> str | None:
    if type(expected) is not type(actual):
        return f"{path}: type {type(expected).__name__} != {type(actual).__name__}"
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            return f"{path}: object keys differ; expected-only={expected.keys()-actual.keys()}, actual-only={actual.keys()-expected.keys()}"
        for key in expected:
            found = mismatch(expected[key], actual[key], f"{path}.{key}")
            if found:
                return found
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}: list length {len(expected)} != {len(actual)}"
        for index, (a, b) in enumerate(zip(expected, actual)):
            found = mismatch(a, b, f"{path}[{index}]")
            if found:
                return found
    elif expected != actual:
        return f"{path}: unequal values" if isinstance(expected, str) else f"{path}: {expected!r} != {actual!r}"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--exporter", type=Path, default=ROOT / "build/nds/n05/host/world_dump.exe")
    parser.add_argument("--prepare-only", action="store_true", help="Generate oracle cases; no C validation or passing claim")
    args = parser.parse_args()
    report = dict(schema_version=1, task="N05", passed=False, prepared=False, python=sys.version,
                  oracle_sha256=sha(Path(__file__).read_bytes()), cases=[], models=[], limitations=[
                      "Source decoding is static grammar/ASM evidence, not a fresh DOS execution.",
                      "Initial seeds omit unverified full native constructors and all gameplay update logic.",
                      "Host exports and emulator execution have separate evidence; physical DS/DS Lite remains N24."])
    report_path = EVIDENCE / ("oracle-prepared.json" if args.prepare_only else "oracle-report.json")
    safe_output(report_path, EVIDENCE)
    try:
        archives = originals()
        report["sources"] = [dict(path=a["relative"], sha256=a["sha256"]) for a in archives.values()]
        if not args.prepare_only:
            report["exporter_sha256"] = sha(args.exporter.read_bytes())
            report["package"] = dict(path=str(args.package.relative_to(ROOT)).replace("\\", "/"), sha256=sha(args.package.read_bytes()))
        for language, archive in archives.items():
            for entry in archive["entries"][18:21]:
                model = parse_level(entry["payload"])
                row = dict(language=language, source_index=entry["index"], source_sha256=entry["sha256"],
                           definitions=len(model["objects"]), fields=sum(len(o["fields"]) for o in model["objects"]),
                           model_sha256=model_digest(model, entry, language))
                if language == 1:
                    effective, changes, groups = expected_profile(model, entry["index"] - 17)
                    row.update(effective_model_sha256=model_digest(effective, entry, language), accepted_changes=changes,
                               semantic_groups=groups)
                report["models"].append(row)
        for number in (1, 2, 3):
            common = None
            for language, label in ((1, "ru"), (2, "en")):
                expected = expected_case(archives, number, language)
                path = GOLDEN / f"level{number}-lang{language}.json"
                compact = (json.dumps(expected, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
                require(len(compact) <= 524288, "Canonical export exceeds 512 KiB observation budget")
                safe_output(path, EVIDENCE).write_bytes(compact)
                record = dict(level=number, language=language, expected=str(path.relative_to(ROOT)).replace("\\", "/"),
                              expected_bytes=len(compact), expected_sha256=sha(compact), checked=False)
                if common is not None:
                    require(expected["world"] == common, "Language changed independent expected world")
                common = expected["world"]
                if not args.prepare_only:
                    command = [str(args.exporter), str(args.package), str(number), str(language), str(number)]
                    completed = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=120)
                    require(completed.returncode == 0, f"C exporter failed for {number}/{language}: {completed.stderr.decode('utf-8',errors='replace')}")
                    actual = json.loads(completed.stdout)
                    actual_path = OUT / f"level{number}-{label}-actual.json"
                    safe_output(actual_path, OUT).write_bytes(completed.stdout)
                    difference = mismatch(expected, actual)
                    require(difference is None, f"Independent original oracle mismatch {number}/{language}: {difference}")
                    record.update(checked=True, actual=str(actual_path.relative_to(ROOT)).replace("\\", "/"),
                                  actual_bytes=len(completed.stdout), actual_sha256=sha(completed.stdout), command=command)
                report["cases"].append(record)
        for archive in archives.values():
            require(sha(archive["path"].read_bytes()) == archive["sha256"], "Original changed during oracle")
        report["prepared"] = True
        report["passed"] = not args.prepare_only
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
        print(report["error"], file=sys.stderr)
    finally:
        write_json(report_path, report)
    print(f"N05 oracle prepared={report['prepared']} passed={report['passed']}; {report_path}")
    return 0 if report["prepared"] and (report["passed"] or args.prepare_only) else 1


if __name__ == "__main__":
    raise SystemExit(main())
