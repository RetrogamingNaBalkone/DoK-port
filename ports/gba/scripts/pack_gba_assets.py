"""Deterministic G03 bilingual ROM package. Original bytes stay under build/."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

from analyze_resources import parse

ROOT = Path(__file__).resolve().parents[3]
MAGIC = b"DOKGBA1\0"
SLOTS = {"sprite": 0xfff0, "animation": 0xfff1, "sound": 0xfff2,
         "palette": 0xfff3, "light": 0xfff4}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def save_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, sort_keys=True) + "\n", encoding="utf-8")

def put(buffer, offset, fmt, *values):
    struct.pack_into("<" + fmt, buffer, offset, *values)

def source_scalar(obj, offset, width=2, signed=True, optional=False):
    mask = bytes.fromhex(obj["known"])
    if not all(mask[n // 8] & (1 << (n % 8)) for n in range(offset, offset + width)):
        if optional:
            return None
        raise ValueError(f"Unknown required scalar class={obj['class']} glyph={obj['glyph']:02x} offset={offset:x}")
    data = bytes.fromhex(obj["bytes"])
    return int.from_bytes(data[offset:offset + width], "little", signed=signed)

def update(obj, offset, value, width=2):
    fields = [f for f in obj["fields"] if f["offset"] == offset and f["kind"] <= 2]
    if len(fields) != 1:
        raise ValueError(f"Expected one numeric field at {offset:x}")
    before = fields[0]["number"]
    fields[0]["number"] = value
    data = bytearray.fromhex(obj["bytes"])
    data[offset:offset + width] = value.to_bytes(width, "little", signed=True)
    obj["bytes"] = data.hex()
    return before

def apply_profile(model):
    by_glyph = {o["glyph"]: o for o in model["objects"]}
    boyars = [o for o in model["objects"] if o["class"] == 9 and
              bytes.fromhex(o["animation"]) == "БОЯР_И".encode("cp866")]
    mice = [o for o in model["objects"] if o["class"] == 9 and
            bytes.fromhex(o["animation"]) == "МЫШЬ_Л".encode("cp866")]
    if len(boyars) != 33 or len(mice) != 30:
        raise ValueError("LEVEL1 resource-defined groups do not match checked33 boyars/30 mice")
    changes = []
    def change(glyph, offset, value, reason, width=2):
        obj = by_glyph[glyph]
        before = update(obj, offset, value, width)
        changes.append({"glyph": glyph, "native_offset": offset, "before_ru": before,
                        "value": value, "reason": reason})
    for glyph in (0x81, 0x86):
        change(glyph, 0x38, 20, "ENG calmer boyar speed", 4)
        change(glyph, 0xba, 9000, "ENG boyar activation distance", 4)
        if source_scalar(by_glyph[glyph], 0xa4) != 42:
            raise ValueError("Unexpected boyar HP baseline")
    change(0x8c, 0xa4, 12, "ENG boyar HP")
    for obj in boyars:
        change(obj["glyph"], 0xae, 24, "ENG course period for resource-defined BOYAR group")
    change(0xa0, 0x38, 60, "Hybrid Baba-yaga speed", 4)
    baba = by_glyph[0xa0]
    if (source_scalar(baba, 0xa4), baba["width_parameter"], source_scalar(baba, 0xba, 4)) != (54, 7, 3400):
        raise ValueError("Unexpected RUS Baba-yaga baseline")
    if any(source_scalar(o, 0xae) != 8 for o in mice):
        raise ValueError("RUS mouse course period must remain8")
    return changes, {"boyars": sorted(o["glyph"] for o in boyars),
                     "mice": sorted(o["glyph"] for o in mice), "baba_yaga": 0xa0}

def object_record(o):
    b = bytearray(36)
    value = lambda off, width=2, signed=True, optional=False: source_scalar(o, off, width, signed, optional)
    known = 0
    interval = value(0x52, optional=True)
    aa = value(0x20, optional=True)
    bb = value(0x22, optional=True)
    if o["class"] == 8:
        aa, bb = value(0x54), value(0x56)
    if aa is not None: known |= 1
    if bb is not None: known |= 2
    if interval is not None: known |= 4
    put(b, 0, "HHHHhhHH", value(0x15, signed=False), value(0x50, signed=False),
        value(0x24, signed=False), (interval or 0) & 65535, aa or 0, bb or 0, known, 0)
    c = o["class"]
    if c == 6:
        put(b, 16, "hhhh", value(0x52), value(0x54), value(0x56), value(0x58))
    elif c == 7:
        put(b, 16, "ihhhhhHBBh", value(0x66, 4), value(0x5c), value(0x5e), value(0x5a),
            value(0x58), value(0x56), value(0x6a, signed=False), value(0x54, 1, False), value(0x55, 1, False), value(0x60))
    elif c == 8:
        put(b, 16, "iihhhhB3x", value(0x3c, 4), value(0x2a, 4), value(0x5c), value(0x5e),
            value(0x5a), value(0x58), value(0x68, 1, False))
    elif c == 9:
        course = value(0xac, optional=True)
        put(b, 16, "hhhBB", value(0xa4), value(0xa2), course or 0, value(0xe6, 1, False), int(course is not None))
    elif c == 10:
        put(b, 16, "h", value(0x83))
    return bytes(b)

def actor_record(o):
    b = bytearray(64)
    value = lambda off, width=2, signed=True, optional=False: source_scalar(o, off, width, signed, optional)
    known = 0
    for mask, offset, target in ((1, 0x26, 4), (2, 0x2a, 8), (4, 0x2e, 12)):
        v = value(offset, 4, optional=True)
        if v is not None:
            known |= mask
            put(b, target, "i", v)
    bound = value(0x9e, optional=True)
    if bound is not None:
        known |= 8
        put(b, 56, "hh", bound, value(0xa0))
    put(b, 0, "I", known)
    for off, target in ((0x38, 16), (0x3c, 20), (0x90, 24), (0x94, 28)):
        put(b, target, "i", value(off, 4))
    put(b, 32, "I", value(0x1c, 4, False))
    for j, off in enumerate((0x32, 0x34, 0x36, 0x5a, 0x7c, 0x7e, 0x80, 0x82, 0x84, 0x98)):
        put(b, 36 + 2 * j, "h", value(off))
    put(b, 60, "BB", value(0x5c, 1, False), value(0x5d, 1, False))
    return bytes(b)

def player_record(objects):
    b = bytearray(60)
    players = [o for o in objects if o["class"] == 1]
    if len(players) != 1:
        raise ValueError("Expected exactly one player definition")
    p = players[0]
    for j, off in enumerate((0x5e, 0x60, 0x62, 0x64, 0x66, 0x68, 0x6a, 0x6c, 0x6e, 0x70)):
        put(b, j * 2, "h", source_scalar(p, off))
    put(b, 20, "H", source_scalar(p, 0x72, signed=False))
    for j, off in enumerate((0x76, 0x78, 0x7a, 0xce, 0xd0, 0xd6, 0xd8)):
        put(b, 22 + j * 2, "h", source_scalar(p, off))
    if any(source_scalar(p, 0xaa + j * 4, 4, False) for j in range(9)):
        raise ValueError("Non-null startup weapon pointer cannot be serialized as an object ID")
    put(b, 56, "I", source_scalar(p, 0xda, 4, False))
    return bytes(b)

class Writer:
    def __init__(self, prefix):
        self.data = bytearray(prefix)
        self.blocks = {}
    def add(self, data):
        data = bytes(data)
        if data in self.blocks:
            return self.blocks[data]
        self.data.extend(b"\0" * (-len(self.data) % 4))
        offset = len(self.data)
        self.data.extend(data)
        self.blocks[data] = offset
        return offset

def localize(common, source, language):
    model = copy.deepcopy(common)
    matches = {(o["class"], o["glyph"]): o for o in source["objects"]}
    changes = []
    for obj in model["objects"]:
        other = matches.get((obj["class"], obj["glyph"]))
        if other is None:
            raise ValueError("Localization lacks common class/glyph definition")
        for key in ("sprite", "animation", "sound"):
            if obj[key] != other[key]:
                changes.append({"glyph": obj["glyph"], "slot": key, "before_ru": obj[key], "localized": other[key]})
            obj[key] = other[key]
        other_fields = {(f["offset"], f["kind"]): f for f in other["fields"]}
        for field in obj["fields"]:
            if field["kind"] >= 3:
                localized = other_fields[(field["offset"], field["kind"])]
                if field["hex"] != localized["hex"]:
                    changes.append({"glyph": obj["glyph"], "slot": field["offset"], "kind": field["kind"],
                                    "before_ru": field["hex"], "localized": localized["hex"]})
                field["hex"] = localized["hex"]
    model["palette"], model["light"] = source["palette"], source["light"]
    return model, changes

def view_record(writer, model, common_offsets, resource_ids, resources, language):
    text = bytearray()
    def span(raw):
        raw = bytes.fromhex(raw)
        if len(text) + len(raw) > 16384:
            raise ValueError("Localized text pool exceeds G02 capacity")
        result = (len(text), len(raw))
        text.extend(raw)
        return result
    palette, light = span(model["palette"]), span(model["light"])
    definitions, fields, bindings = bytearray(), bytearray(), []
    def bind(object_index, slot, raw, kind):
        name = bytes.fromhex(raw)
        if not name:
            return
        if name not in resource_ids or name not in resources[language]:
            raise ValueError(f"Unresolved {language} resource {name!r}")
        bindings.append((object_index, slot, resource_ids[name], kind, 0))
    bind(65535, SLOTS["palette"], model["palette"], 255)
    bind(65535, SLOTS["light"], model["light"], 255)
    for index, obj in enumerate(model["objects"]):
        d = bytearray(32)
        begin = len(fields) // 8
        for field in obj["fields"]:
            if field["kind"] <= 2:
                fields.extend(struct.pack("<HBBi", field["offset"], field["kind"], 0, field["number"]))
            else:
                off, size = span(field["hex"])
                fields.extend(struct.pack("<HBBHH", field["offset"], field["kind"], 0, off, size))
                if field["kind"] <= 5:
                    bind(index, field["offset"], field["hex"], field["kind"])
        common_spans = []
        for key, kind in (("sprite", 3), ("animation", 4), ("sound", 5)):
            common_spans.extend(span(obj[key]))
            bind(index, SLOTS[key], obj[key], kind)
        put(d, 0, "HHHhhBBHHHH", obj["class"], obj["flags"], obj["sprite_flags"],
            obj["width_parameter"], obj["height_parameter"], obj["glyph"], obj["marker"],
            obj["actor_index"], begin, len(obj["fields"]), obj["placements"])
        put(d, 20, "HHHHHH", *common_spans)
        definitions.extend(d)
    bindings.sort()
    binding_bytes = b"".join(struct.pack("<HHHBB", *record) for record in bindings)
    record = bytearray(80)
    put(record, 0, "8H", *model["settings"])
    put(record, 16, "6HhH", len(model["objects"]), common_offsets["actor_count"], len(fields) // 8,
        len(text), len(bytes.fromhex(model["maps"][0])), model["player_index"], model["pickup_99_total"], 0)
    offsets = (writer.add(struct.pack("<256H", *model["glyph_index"])), writer.add(definitions),
               writer.add(fields), writer.add(text), common_offsets["maps"], common_offsets["objects"],
               common_offsets["actors"], common_offsets["player"])
    put(record, 32, "8I", *offsets)
    put(record, 64, "4H", *palette, *light)
    put(record, 72, "IHH", writer.add(binding_bytes), len(bindings), 0)
    return record, {"object_count": len(model["objects"]), "actor_count": common_offsets["actor_count"],
                    "field_count": len(fields) // 8, "text_size": len(text), "binding_count": len(bindings),
                    "maps_sha256": sha(b"".join(bytes.fromhex(x) for x in model["maps"][:3])),
                    "startup_sha256": common_offsets["startup_sha256"]}

def difference_manifest(common, ru, en):
    differences = {"numeric": [], "maps": []}
    for index, (r, e, c) in enumerate(zip(ru["settings"], en["settings"], common["settings"])):
        if r != e:
            differences["numeric"].append({"scope": "settings", "index": index, "ru": r, "en": e, "chosen": c, "policy": "RUS baseline"})
    english = {o["glyph"]: o for o in en["objects"]}
    chosen = {o["glyph"]: o for o in common["objects"]}
    for obj in ru["objects"]:
        other, current = english[obj["glyph"]], chosen[obj["glyph"]]
        for key in ("class", "flags", "sprite_flags", "width_parameter", "height_parameter", "marker"):
            if obj[key] != other[key]:
                differences["numeric"].append({"scope": "object", "glyph": obj["glyph"], "field": key,
                    "ru": obj[key], "en": other[key], "chosen": current[key], "policy": "RUS baseline/common profile"})
        fields_en = {(f["offset"], f["kind"]): f for f in other["fields"]}
        fields_current = {(f["offset"], f["kind"]): f for f in current["fields"]}
        for f in obj["fields"]:
            if f["kind"] > 2:
                continue
            key = f["offset"], f["kind"]
            if key not in fields_en:
                differences["numeric"].append({"scope": "field", "glyph": obj["glyph"], "offset": f["offset"],
                    "ru": f["number"], "en": None, "chosen": f["number"], "policy": "RUS baseline, no compatible ENG field"})
            elif f["number"] != fields_en[key]["number"]:
                value = fields_current[key]["number"]
                differences["numeric"].append({"scope": "field", "glyph": obj["glyph"], "offset": f["offset"],
                    "ru": f["number"], "en": fields_en[key]["number"], "chosen": value,
                    "policy": "RUS baseline" if value == f["number"] else "explicit gba-balance-proposal profile"})
    for plane, r, e in zip(("MMap", "TMap", "FCMap"), ru["maps"][:3], en["maps"][:3]):
        changed = [{"cell": i, "ru": a, "en": b} for i, (a, b) in enumerate(zip(bytes.fromhex(r), bytes.fromhex(e))) if a != b]
        differences["maps"].append({"plane": plane, "changed_cells": changed, "chosen": "RUS baseline", "ENG_candidates": "deferred until interactions/routes tested"})
    return differences

def build(runtime_dir, source_files):
    resources, sources = {}, {}
    verified_report = json.loads((ROOT / "analysis/gba/runtime-verification.json").read_text(encoding="utf-8"))
    verified_runtime = {(entry["language"], entry["resource_index"] - 17): entry["runtime_model"]["sha256"]
                        for entry in verified_report["levels"]}
    for language in ("ru", "en"):
        path = source_files[language]
        data = path.read_bytes()
        inventory = json.loads((ROOT / f"analysis/originals/inventory-{language}.json").read_text(encoding="utf-8"))
        if sha(data) != inventory["resource"]["sha256"]:
            raise ValueError(f"Original {language} RES hash changed")
        all_entries = parse(data)
        resources[language] = {bytes.fromhex(e["name_hex"]): data[e["offset"]:e["offset"] + e["size"]]
                               for e in reversed(all_entries) if not e["name"].startswith("LEVEL")}
        exe = (ROOT / inventory["file"]).read_bytes()
        if sha(exe) != inventory["sha256"]:
            raise ValueError(f"Original {language} EXE hash changed")
        sources[language] = {"res_sha256": sha(data), "res_size": len(data), "source_entries": len(all_entries),
                             "exe_sha256": sha(exe), "exe_size": len(exe),
                             "levels": {e["name"]: {"sha256": e["sha256"], "size": e["size"]}
                                        for e in all_entries if e["name"].startswith("LEVEL")}}
    names = sorted(set(resources["ru"]) | set(resources["en"]))
    if len(names) > 65535:
        raise ValueError("Too many stable resource IDs")
    ids = {name: i + 1 for i, name in enumerate(names)}
    resource_offset, view_offset = 64, 64 + 32 * len(names)
    writer = Writer(view_offset + 6 * 80)
    resource_records = []
    for name in names:
        record = bytearray(32)
        record[:len(name)] = name
        entry = {"id": ids[name], "name_hex": name.hex(), "languages": {}}
        for language_index, language in enumerate(("ru", "en")):
            payload = resources[language].get(name)
            if payload is None:
                continue
            offset = writer.add(payload)
            put(record, 16 + language_index * 8, "II", offset, len(payload))
            entry["languages"][language] = {"offset": offset, "size": len(payload), "sha256": sha(payload)}
        writer.data[resource_offset + (ids[name] - 1) * 32:resource_offset + ids[name] * 32] = record
        resource_records.append(entry)
    levels, runtime_inputs, profile_changes = [], [], []
    for number in (1, 2, 3):
        models = {}
        for language in ("ru", "en"):
            path = runtime_dir / language / f"LEVEL{number}-runtime.json"
            raw = path.read_bytes()
            if sha(raw) != verified_runtime.get((language, number)):
                raise ValueError(f"Unverified/stale G02 runtime input: {path}; rerun analyze_runtime.py")
            models[language] = json.loads(raw.decode("utf-8-sig"))
            runtime_inputs.append({"level": number, "language": language, "sha256": sha(raw)})
        common = copy.deepcopy(models["ru"])
        groups = None
        if number == 1:
            changes, groups = apply_profile(common)
            profile_changes.extend(changes)
        actors = [o for o in common["objects"] if o["class"] in (1, 5, 9)]
        for index, obj in enumerate(actors):
            if obj["actor_index"] != index:
                raise ValueError("G02 actor indices must be dense in definition order")
        objects_data = b"".join(object_record(o) for o in common["objects"])
        actors_data = b"".join(actor_record(o) for o in actors)
        player_data = player_record(common["objects"])
        common_offsets = {"objects": writer.add(objects_data), "actors": writer.add(actors_data),
                          "player": writer.add(player_data), "actor_count": len(actors),
                          "maps": writer.add(b"".join(bytes.fromhex(x) for x in common["maps"][:3])),
                          "startup_sha256": sha(objects_data + actors_data + player_data)}
        metadata = {"level": number, "baseline": "ru", "groups": groups, "languages": {},
                    "deferred_differences_policy": "All remaining numeric, settings, class, dimensions, geometry and weapon pickup scale use RUS baseline; ENG map candidates remain untested.",
                    "source_differences": difference_manifest(common, models["ru"], models["en"])}
        for language_index, language in enumerate(("ru", "en")):
            model, localized = localize(common, models[language], language)
            record, info = view_record(writer, model, common_offsets, ids, resources, language)
            index = (number - 1) * 2 + language_index
            writer.data[view_offset + index * 80:view_offset + (index + 1) * 80] = record
            info["localized_changes"] = localized
            metadata["languages"][language] = info
        levels.append(metadata)
    writer.data[:8] = MAGIC
    put(writer.data, 8, "HHIHHIIIHHII", 1, 64, len(writer.data), 3, 2, len(names), resource_offset,
        view_offset, 80, 32, 1, 1)
    data = bytes(writer.data)
    for language in ("ru", "en"):
        if sha(source_files[language].read_bytes()) != sources[language]["res_sha256"]:
            raise ValueError("Original changed during packaging")
        inventory = json.loads((ROOT / f"analysis/originals/inventory-{language}.json").read_text(encoding="utf-8"))
        if sha((ROOT / inventory["file"]).read_bytes()) != sources[language]["exe_sha256"]:
            raise ValueError("Original EXE changed during packaging")
    manifest = {"format": "DOKGBA", "version": 1, "profile": "gba-balance-proposal-v1",
                "target_fps": 30, "endianness": "little", "alignment": 4,
                "profile_document_sha256": sha((ROOT / "docs/gba/gba-balance-proposal.md").read_bytes()),
                "package": {"size": len(data), "sha256": sha(data)}, "sources": sources,
                "runtime_inputs": runtime_inputs, "profile_changes": profile_changes, "levels": levels,
                "resources": resource_records, "resource_count": len(names),
                "excluded": ["LEVEL1", "LEVEL2", "LEVEL3"], "payload_policy": "Byte-exact original non-LEVEL data; no image/animation/sound conversion",
                "geometry_policy": "RUS baseline for every language; all ENG map candidates deferred"}
    return data, manifest

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=ROOT / "build/gba/assets")
    p.add_argument("--runtime-dir", type=Path, default=ROOT / "research/evidence/runtime")
    p.add_argument("--ru-res", type=Path, default=ROOT / "originals/pkr/RUS/PKR.RES")
    p.add_argument("--en-res", type=Path, default=ROOT / "originals/pkr/ENG/PKE.RES")
    p.add_argument("--emit-c", action="store_true")
    args = p.parse_args()
    data, manifest = build(args.runtime_dir, {"ru": args.ru_res, "en": args.en_res})
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.output / "assets.bin"
    binary.write_bytes(data)
    save_json(args.output / "manifest.json", manifest)
    # Assembly include path is project-relative; compiler invocation uses ROOT.
    try:
        include = binary.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        include = binary.resolve().as_posix()
    assembly = '\n'.join(['.section .rodata.dok_assets,"a",%progbits', '.balign 4',
        '.global dok_gba_assets', '.type dok_gba_assets,%object', 'dok_gba_assets:',
        f'.incbin "{include}"', '.global dok_gba_assets_end', 'dok_gba_assets_end:',
        '.size dok_gba_assets,.-dok_gba_assets', '.balign 4', '.global dok_gba_assets_size',
        '.type dok_gba_assets_size,%object', 'dok_gba_assets_size:', f'.long {len(data)}',
        '.size dok_gba_assets_size,4', ''])
    (args.output / "assets.S").write_text(assembly, encoding="ascii")
    (args.output / "assets.h").write_text('#include <stdint.h>\nextern const uint8_t dok_gba_assets[];\nextern const uint8_t dok_gba_assets_end[];\nextern const uint32_t dok_gba_assets_size;\n', encoding="ascii")
    if args.emit_c:
        rows = [','.join(str(x) for x in data[i:i+24]) for i in range(0, len(data), 24)]
        (args.output / "assets.c").write_text('#include <stdint.h>\n_Alignas(4) const uint8_t dok_gba_assets[]={\n' + ',\n'.join(rows) + '\n};\nconst uint32_t dok_gba_assets_size=' + str(len(data)) + ';\n', encoding="ascii")
    print(json.dumps({"package": str(binary), "size": len(data), "sha256": sha(data), "resources": manifest["resource_count"]}))

if __name__ == "__main__":
    main()

