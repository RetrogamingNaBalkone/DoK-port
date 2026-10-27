"""Independent binary-package and unified-profile verification for G03.

Full packages and localized data stay in ignored build/gba/assets-check/.
Only counts, hashes, origins and check results are written to analysis/.
This is asset-format verification, not a game or a 30 FPS measurement.
"""
import argparse
from collections import Counter
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from experiment_cleanup import temporary_workspace

from analyze_levels import reference, W, L, C, I, A, S, T
from analyze_resources import parse
from analyze_worlds import startup
from analyze_runtime import verify_model
from inventory import EXPECTED

ROOT = Path(__file__).resolve().parents[1]
CHECK_ROOT = ROOT / "build/gba/assets-check"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check(actual, expected, context):
    if actual != expected:
        if isinstance(actual, (int, bool, type(None))) and isinstance(expected, (int, bool, type(None))):
            raise AssertionError(f"{context}: actual={actual!r}, expected={expected!r}")
        raise AssertionError(context + ": values differ; full private data retained under build/gba/assets-check")


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path.read_bytes()),
            "size": path.stat().st_size}


def originals():
    result = {}
    for language, (exe_name, exe_hash) in EXPECTED.items():
        exe_path = ROOT / "originals/pkr" / exe_name
        check(sha(exe_path.read_bytes()), exe_hash, language + " original EXE")
        inventory = json.loads((ROOT / f"analysis/inventory-{language}.json").read_text(encoding="utf-8"))
        archive_path = ROOT / inventory["resource"]["file"]
        archive = archive_path.read_bytes()
        check(sha(archive), inventory["resource"]["sha256"], language + " original RES")
        entries = parse(archive)
        resources = {entry["name_hex"]: archive[entry["offset"]:entry["offset"] + entry["size"]]
                     for entry in reversed(entries)}
        levels = {n: reference(resources[entries[17 + n]["name_hex"]]) for n in (1, 2, 3)}
        result[language] = {"archive_path": archive_path, "archive_sha256": sha(archive),
                            "exe_path": exe_path, "exe_sha256": exe_hash, "archive": archive,
                            "entries": entries, "resources": resources, "levels": levels}
    return result


def field_value(obj, offset):
    matches = [field for field in obj["fields"] if field[1] == offset and field[0] in (W, L, C)]
    check(len(matches), 1, "one numeric field at requested native offset")
    return matches[0][2]


def set_field(obj, offset, value):
    field_value(obj, offset)
    next(field for field in obj["fields"] if field[1] == offset)[2] = value


def unified_oracle(original, number, language):
    """Derive policy independently from packer; unresolved data uses RUS."""
    source = deepcopy(original["ru"]["levels"][number])
    localized = original[language]["levels"][number]
    localized_text = {(obj["class"], obj["glyph"], offset): value
                      for obj in localized["objects"] for kind, offset, value in obj["fields"] if kind == T}
    text_keys = set()
    for obj in source["objects"]:
        for field in obj["fields"]:
            if field[0] == T:
                key = (obj["class"], obj["glyph"], field[1])
                field[2] = localized_text[key]
                text_keys.add(key)
    check(text_keys, set(localized_text), "all localized class/glyph/native-offset text keys")
    policy_counts = Counter()
    if number == 1:
        boyar = "БОЯР_И".encode("cp866").hex()
        mouse = "МЫШЬ_Л".encode("cp866").hex()
        by_glyph = {obj["glyph"]: obj for obj in source["objects"]}
        for obj in source["objects"]:
            if obj["class"] == 9 and obj["animation"] == boyar:
                set_field(obj, 0xae, 24)
                policy_counts["boyar_course24"] += 1
            if obj["class"] == 9 and obj["animation"] == mouse:
                set_field(obj, 0xae, 8)
                policy_counts["mouse_course8"] += 1
        check(policy_counts["boyar_course24"], 33, "all 33 boyar definitions in profile")
        check(policy_counts["mouse_course8"], 30, "all 30 mouse definitions in profile")
        for glyph in (0x81, 0x86):
            obj = by_glyph[glyph]
            set_field(obj, 0x38, 20)
            set_field(obj, 0xba, 9000)
            set_field(obj, 0xa4, 42)
        set_field(by_glyph[0x8c], 0xa4, 12)
        witch = by_glyph[0xa0]
        set_field(witch, 0xa4, 54)
        set_field(witch, 0x38, 60)
        set_field(witch, 0xba, 3400)
        witch["width"] = 7
        policy_counts["special_boyars81_86"] = 2
        policy_counts["boyar8c_health12"] = 1
        policy_counts["first_witch_hybrid"] = 1
    native, states = startup(source, original[language]["resources"])
    return source, native, states, dict(policy_counts)


class Package:
    """v1 parser with literal wire offsets; deliberately imports no packer code."""
    def __init__(self, data, original):
        self.data, self.ranges, self.resources, self.views = data, [], [], []
        check(data[:8], b"DOKGBA1\0", "package magic")
        if len(data) < 64:
            raise AssertionError("truncated package header")
        check(self.u16(8), 1, "format version")
        check(self.u16(10), 64, "header size")
        check(self.u32(12), len(data), "declared package size")
        check((self.u16(16), self.u16(18)), (3, 2), "three levels and two languages")
        check((self.u16(32), self.u16(34)), (80, 32), "view/resource record strides")
        check((self.u32(36), self.u32(40)), (1, 1), "unified profile and flags")
        check(data[44:64], bytes(20), "reserved package header bytes")
        self.ranges.append((0, 64, "header"))
        count, res_offset, view_offset = self.u32(20), self.u32(24), self.u32(28)
        expected_names = sorted(e["name_hex"] for e in original["ru"]["entries"] if e["name"] not in ("LEVEL1", "LEVEL2", "LEVEL3"))
        check(expected_names, sorted(e["name_hex"] for e in original["en"]["entries"] if e["name"] not in ("LEVEL1", "LEVEL2", "LEVEL3")), "shared non-LEVEL resource-name set")
        check(count, len(expected_names), "all original resource IDs retained")
        self.span(res_offset, count * 32, "resource directory")
        self.span(view_offset, 6 * 80, "view directory")
        for index in range(count):
            pos = res_offset + index * 32
            name_field = data[pos:pos + 16]
            nul = name_field.find(0)
            if not 0 < nul < 16:
                raise AssertionError("invalid package resource name")
            check(name_field[nul:], bytes(16 - nul), "zero-padded resource name")
            name = name_field[:nul].hex()
            check(name, expected_names[index], "sorted stable resource ID")
            record = {"id": index + 1, "name": name, "variants": {}}
            for language, at in (("ru", 16), ("en", 24)):
                offset, size = self.unpack("II", pos + at)
                payload = self.span(offset, size, f"resource {index + 1} {language}")
                check(payload, original[language]["resources"][name], f"resource {index + 1} {language} original bytes")
                record["variants"][language] = {"offset": offset, "size": size, "sha256": sha(payload)}
            self.resources.append(record)
        self.names_to_ids = {r["name"]: r["id"] for r in self.resources}
        self.view_offset = view_offset

    def unpack(self, fmt, offset):
        size = struct.calcsize("<" + fmt)
        if offset < 0 or offset > len(self.data) or size > len(self.data) - offset:
            raise AssertionError("integer/record outside package")
        return struct.unpack_from("<" + fmt, self.data, offset)

    def u16(self, offset):
        return self.unpack("H", offset)[0]

    def u32(self, offset):
        return self.unpack("I", offset)[0]

    def span(self, offset, size, label):
        if offset % 4 or offset < 64 or offset > len(self.data) or size > len(self.data) - offset:
            raise AssertionError(label + ": unaligned or out-of-bounds range")
        if size:
            self.ranges.append((offset, offset + size, label))
        return self.data[offset:offset + size]

    def view(self, number, language, source, native, states):
        tag = f"LEVEL{number} {language}"
        view_index = (number - 1) * 2 + (language == "en")
        pos = self.view_offset + view_index * 80
        settings = list(self.unpack("8H", pos))
        obj_count, actor_count, field_count, text_size, cells, player, pickup = self.unpack("6Hh", pos + 16)
        check(self.u16(pos + 30), 0, tag + " reserved view word")
        offsets = self.unpack("8I", pos + 32)
        glyph_off, defs_off, fields_off, text_off, maps_off, objects_off, actors_off, player_off = offsets
        binding_off, binding_count, reserved = self.unpack("IHH", pos + 72)
        check(reserved, 0, tag + " final reserved view word")
        check(settings, source["settings"], tag + " settings")
        check(obj_count, len(states), tag + " definition count")
        check(actor_count, sum(o["class"] in (1, 5, 9) for o in source["objects"]), tag + " actor count")
        check(field_count, sum(len(o["fields"]) for o in source["objects"]), tag + " field count")
        check(cells, settings[0] * settings[2], tag + " cell count")
        check((player, pickup), (native["player_index"], native["pickup_99_total"]), tag + " player/sum")
        if obj_count > 256 or actor_count > 128 or field_count > 2048 or text_size > 16384 or cells > 4096:
            raise AssertionError(tag + ": runtime capacity exceeded")
        glyph_raw = self.span(glyph_off, 512, tag + " glyph LUT")
        expected_glyphs = [0] * 256
        for i, obj in enumerate(source["objects"]):
            expected_glyphs[obj["glyph"]] = i + 1
        check(list(struct.unpack("<256H", glyph_raw)), expected_glyphs, tag + " exact glyph LUT")
        self.span(defs_off, obj_count * 32, tag + " definitions")
        self.span(fields_off, field_count * 8, tag + " fields")
        text = self.span(text_off, text_size, tag + " language string pool")
        maps = self.span(maps_off, 3 * cells, tag + " maps")
        check(maps.hex(), "".join(native["maps"][:3]), tag + " shared RUS baseline geometry")
        self.span(objects_off, obj_count * 36, tag + " initial object records")
        self.span(actors_off, actor_count * 64, tag + " initial actor records")
        self.span(player_off, 60, tag + " initial player record")
        bindings = self.span(binding_off, binding_count * 8, tag + " resource bindings")

        def string(offset, size, context):
            if offset > text_size or size > text_size - offset:
                raise AssertionError(context + ": local string span outside view pool")
            return text[offset:offset + size].hex()

        for at, key in ((64, "palette"), (68, "light")):
            check(string(*self.unpack("HH", pos + at), tag + " " + key), source[key], tag + " " + key)
        expected_bindings = []
        actor_index = field_index = 0
        for index, (obj, state) in enumerate(zip(source["objects"], states)):
            dpos = defs_off + index * 32
            cls, flags, sprite_flags, width, height, glyph, marker, ai, begin, count = self.unpack("HHHhhBBHHH", dpos)
            placements = self.u16(dpos + 18)
            check((cls, flags, sprite_flags, width, height, glyph, marker, placements),
                  (obj["class"], obj["flags"], obj["sprite_flags"], obj["width"], obj["height"], obj["glyph"], obj["marker"], state.placements), tag + f" definition {index}")
            check((begin, count), (field_index, len(obj["fields"])), tag + " ordered field slice")
            for at, key, slot, kind in ((20, "sprite", 0xfff0, I), (24, "animation", 0xfff1, A), (28, "sound", 0xfff2, S)):
                name = string(*self.unpack("HH", dpos + at), tag + " " + key)
                check(name, obj[key], tag + f" object {index} " + key)
                if name:
                    expected_bindings.append((index, slot, self.names_to_ids[name], kind))
            for kind, offset, value in obj["fields"]:
                fpos = fields_off + field_index * 8
                native_offset, wire_kind, pad = self.unpack("HBB", fpos)
                check((native_offset, wire_kind, pad), (offset, kind, 0), tag + " class-field tag")
                if kind in (W, L, C):
                    check(self.unpack("i", fpos + 4)[0], value, tag + f" object {index} numeric {offset:#x}")
                else:
                    name = string(*self.unpack("HH", fpos + 4), tag + " class string")
                    check(name, value, tag + f" object {index} class bytes {offset:#x}")
                    if kind in (I, A, S) and name:
                        expected_bindings.append((index, offset, self.names_to_ids[name], kind))
                field_index += 1
            if cls in (1, 5, 9):
                check(ai, actor_index, tag + " typed actor index")
                self.actor(actors_off + 64 * actor_index, state, tag + f" actor {actor_index}")
                actor_index += 1
            else:
                check(ai, 65535, tag + " non-actor sentinel")
            self.object(objects_off + 36 * index, state, tag + f" object state {index}")
        hero = states[player - 1] if player else None
        if hero:
            self.player(player_off, hero, tag + " player state")
        for key, slot in (("palette", 0xfff3), ("light", 0xfff4)):
            if source[key]:
                expected_bindings.append((65535, slot, self.names_to_ids[source[key]], 255))
        decoded_bindings = []
        for index in range(binding_count):
            obj, slot, resource, kind, pad = struct.unpack_from("<HHHBB", bindings, index * 8)
            check(pad, 0, tag + " reserved binding byte")
            if not 1 <= resource <= len(self.resources):
                raise AssertionError(tag + ": resource binding ID outside directory")
            if obj != 65535 and obj >= obj_count:
                raise AssertionError(tag + ": binding object outside level")
            decoded_bindings.append((obj, slot, resource, kind))
        check(Counter(decoded_bindings), Counter(expected_bindings), tag + " all resource references resolve with exact semantic bindings")
        return {"number": number, "language": language, "definitions": obj_count, "actors": actor_count,
                "fields": field_count, "text_size": text_size, "cells": cells,
                "bindings_count": binding_count,
                "maps_sha256": sha(maps), "initial_objects_sha256": sha(self.data[objects_off:objects_off + obj_count * 36]),
                "initial_actors_sha256": sha(self.data[actors_off:actors_off + actor_count * 64]),
                "initial_player_sha256": sha(self.data[player_off:player_off + 60]),
                "startup_sha256": sha(self.data[objects_off:objects_off + obj_count * 36] + self.data[actors_off:actors_off + actor_count * 64] + self.data[player_off:player_off + 60])}

    def actor(self, pos, state, tag):
        expected_known = (1 if 0x26 in state.values else 0) | (2 if 0x2a in state.values else 0) | (4 if 0x2e in state.values else 0) | (8 if 0x9e in state.values and 0xa0 in state.values else 0)
        check(self.u32(pos), expected_known, tag + " unknown-position bits")
        for at, off, fmt in ((4, 0x26, "i"), (8, 0x2a, "i"), (12, 0x2e, "i"), (16, 0x38, "i"),
                             (20, 0x3c, "i"), (24, 0x90, "i"), (28, 0x94, "i"), (32, 0x1c, "I"),
                             (36, 0x32, "h"), (38, 0x34, "h"), (40, 0x36, "h"), (42, 0x5a, "h"),
                             (44, 0x7c, "h"), (46, 0x7e, "h"), (48, 0x80, "h"), (50, 0x82, "h"),
                             (52, 0x84, "h"), (54, 0x98, "h"), (56, 0x9e, "h"), (58, 0xa0, "h"),
                             (60, 0x5c, "B"), (61, 0x5d, "B")):
            width = struct.calcsize("<" + fmt)
            expected = state.get(off, width, sign=fmt in ("i", "h")) if off in state.values else 0
            check(self.unpack(fmt, pos + at)[0], expected, tag + f" typed native {off:#x}")
        check(self.data[pos + 62:pos + 64], bytes(2), tag + " reserved actor bytes")

    def player(self, pos, state, tag):
        offsets = [0x5e, 0x60, 0x62, 0x64] + list(range(0x66, 0x72, 2)) + [0x72] + [0x76, 0x78, 0x7a, 0xce, 0xd0, 0xd6, 0xd8]
        for at, off in enumerate(offsets):
            fmt = "H" if off == 0x72 else "h"
            check(self.unpack(fmt, pos + 2 * at)[0], state.get(off, 2, sign=fmt == "h"), tag + f" native {off:#x}")
        check(self.unpack("9H", pos + 36), (0,) * 9, tag + " initial null weapon IDs")
        check(self.u16(pos + 54), 0, tag + " reserved player word")
        check(self.u32(pos + 56), state.get(0xda, 4), tag + " auxiliary word")

    def object(self, pos, state, tag):
        cls = state.definition["class"]
        aa, ab = (0x54, 0x56) if cls == 8 else (0x20, 0x22)
        known = (1 if aa in state.values else 0) | (2 if ab in state.values else 0) | (4 if 0x52 in state.values else 0)
        for at, off, fmt in ((0, 0x15, "H"), (2, 0x50, "H"), (4, 0x24, "H"), (6, 0x52, "H"), (8, aa, "h"), (10, ab, "h")):
            expected = state.get(off, 2, sign=fmt == "h") if off in state.values else 0
            check(self.unpack(fmt, pos + at)[0], expected, tag + f" native {off:#x}")
        check(self.u16(pos + 12), known, tag + " metadata known bits")
        check(self.u16(pos + 14), 0, tag + " reserved object word")
        fields = []
        if cls == 6:
            fields = [(18, 0x54, "h"), (20, 0x56, "h"), (22, 0x58, "h")]
        elif cls == 7:
            fields = [(16, 0x66, "i"), (20, 0x5c, "h"), (22, 0x5e, "h"), (24, 0x5a, "h"), (26, 0x58, "h"), (28, 0x56, "h"), (30, 0x6a, "H"), (32, 0x54, "B"), (33, 0x55, "B"), (34, 0x60, "h")]
        elif cls == 8:
            fields = [(16, 0x3c, "i"), (20, 0x2a, "i"), (24, 0x5c, "h"), (26, 0x5e, "h"), (28, 0x5a, "h"), (30, 0x58, "h"), (32, 0x68, "B")]
        elif cls == 9:
            fields = [(16, 0xa4, "h"), (18, 0xa2, "h"), (22, 0xe6, "B")]
            check(self.unpack("hB", pos + 20)[0], 0, tag + " unknown course-counter storage")
            check(self.unpack("B", pos + 23)[0], int(0xac in state.values), tag + " course-counter known bit")
        elif cls == 10:
            fields = [(16, 0x83, "h")]
        for at, off, fmt in fields:
            check(self.unpack(fmt, pos + at)[0], state.get(off, struct.calcsize("<" + fmt), sign=fmt in ("h", "i")), tag + f" class state {off:#x}")
        union = bytearray(20)
        for at, off, fmt in fields:
            struct.pack_into("<" + fmt, union, at - 16, state.get(off, struct.calcsize("<" + fmt), sign=fmt in ("h", "i")))
        if cls == 9:
            struct.pack_into("<h", union, 4, state.get(0xac, 2, sign=True) if 0xac in state.values else 0)
            union[7] = int(0xac in state.values)
        check(self.data[pos + 16:pos + 36], bytes(union), tag + " complete tagged union and zero padding")

    def validate_ranges(self):
        unique = sorted(set((start, end) for start, end, _ in self.ranges))
        previous = 0
        for start, end in unique:
            if start < previous:
                raise AssertionError("package ranges partially overlap")
            check(self.data[previous:start], bytes(start - previous), "zero alignment gap")
            previous = end
        check(self.data[previous:], bytes(len(self.data) - previous), "trailing alignment padding")
        return {"ranges_including_shared": len(self.ranges), "unique_ranges": len(unique),
                "alignment": 4, "partial_overlaps": 0}


def corruption_checks(data, package, tool):
    """Exercise production C reader against corruptions of the real package."""
    cases = [("truncated-header", data[:63]), ("truncated-payload", data[:-1])]

    def changed(name, offset, fmt, value):
        raw = bytearray(data)
        struct.pack_into("<" + fmt, raw, offset, value)
        cases.append((name, bytes(raw)))

    changed("unsupported-version", 8, "H", 2)
    changed("overflow-total-size", 12, "I", 0xffffffff)
    changed("resource-directory-outside-file", 24, "I", 0xfffffffc)
    changed("misaligned-view-directory", 28, "I", package.view_offset + 1)
    changed("wrong-view-stride", 32, "H", 76)
    res_offset = package.u32(24)
    changed("resource-payload-outside-file", res_offset + 16, "I", 0xfffffffc)
    changed("resource-size-overflow", res_offset + 20, "I", 0xffffffff)
    view = package.view_offset
    changed("object-count-over-capacity", view + 16, "H", 257)
    changed("actor-count-over-capacity", view + 18, "H", 129)
    changed("field-count-over-capacity", view + 20, "H", 2049)
    changed("string-pool-over-capacity", view + 22, "H", 16385)
    changed("map-cells-over-capacity", view + 24, "H", 4097)
    changed("view-map-range-overflow", view + 48, "I", 0xfffffffc)
    changed("misaligned-object-records", view + 52, "I", package.u32(view + 52) + 1)
    defs = package.u32(view + 36)
    changed("definition-local-string-outside-pool", defs + 20, "H", 65535)
    changed("actor-index-outside-view", defs + 12, "H", 65534)
    bindings = package.u32(view + 72)
    changed("resource-binding-id-outside-directory", bindings + 4, "H", 65535)
    first_id = package.u16(bindings + 4)
    changed("resource-binding-valid-id-wrong-name", bindings + 4, "H", 1 if first_id != 1 else 2)
    changed("resource-binding-invalid-kind", bindings + 6, "B", 254)
    en_view = view + 80
    en_fields = package.u32(en_view + 40)
    changed("english-view-numeric-profile-divergence", en_fields + 4, "i", package.unpack("i", en_fields + 4)[0] + 1)
    changed("english-view-shared-state-divergence", en_view + 52, "I", package.u32(view + 52) + 4)
    results = []
    with temporary_workspace(CHECK_ROOT, "corruptions-") as temporary:
        for name, payload in cases:
            path = Path(temporary) / (name + ".bin")
            path.write_bytes(payload)
            completed = subprocess.run([str(tool.resolve()), str(path), "1", "ru"], cwd=ROOT, capture_output=True)
            if completed.returncode != 1:
                raise AssertionError(f"C reader corruption {name}: expected clean error exit 1, got {completed.returncode}")
            results.append({"case": name, "exit_code": completed.returncode, "bytes": len(payload), "sha256": sha(payload)})
    return results


def verify_manifest(manifest, package, original):
    check((manifest["format"], manifest["version"], manifest["endianness"], manifest["alignment"]),
          ("DOKGBA", 1, "little", 4), "manifest wire-format declarations")
    check(manifest["profile"], "gba-balance-proposal-v1", "manifest named balance profile")
    check(manifest["profile_document_sha256"], sha((ROOT / "docs/gba-balance-proposal.md").read_bytes()), "manifest profile document fingerprint")
    check(manifest["target_fps"], 30, "manifest target FPS is a target, not a measured result")
    check(manifest["package"], {"size": len(package.data), "sha256": sha(package.data)}, "manifest package fingerprint")
    check(manifest["excluded"], ["LEVEL1", "LEVEL2", "LEVEL3"], "manifest source LEVEL exclusion")
    check(manifest["resource_count"], len(package.resources), "manifest resource count")
    check(len(manifest["resources"]), len(package.resources), "manifest resource records")
    for metadata, parsed in zip(manifest["resources"], package.resources):
        check((metadata["id"], metadata["name_hex"]), (parsed["id"], parsed["name"]), "manifest stable resource identity")
        check(metadata["languages"], parsed["variants"], "manifest all resource offsets/sizes/checksums")
    for language, source in original.items():
        check(manifest["sources"][language], {"res_sha256": source["archive_sha256"],
                                               "res_size": len(source["archive"]),
                                               "source_entries": len(source["entries"]),
                                               "exe_sha256": source["exe_sha256"],
                                               "exe_size": source["exe_path"].stat().st_size,
                                               "levels": {f"LEVEL{n}": {"sha256": source["entries"][17 + n]["sha256"],
                                                                         "size": source["entries"][17 + n]["size"]} for n in (1, 2, 3)}},
              "manifest original EXE/RES/LEVEL fingerprints")
    check(len(manifest["runtime_inputs"]), 6, "manifest all G02 inputs recorded")
    for metadata in manifest["runtime_inputs"]:
        path = ROOT / "research/evidence/runtime" / metadata["language"] / f"LEVEL{metadata['level']}-runtime.json"
        check(metadata["sha256"], sha(path.read_bytes()), "manifest G02 input fingerprint")
    expected_changes = {}
    unified, _, _, _ = unified_oracle(original, 1, "ru")
    for before, after in zip(original["ru"]["levels"][1]["objects"], unified["objects"]):
        for old, new in zip(before["fields"], after["fields"]):
            if old[0] in (W, L, C) and old[2] != new[2]:
                expected_changes[(before["glyph"], old[1])] = (old[2], new[2])
    actual_changes = {}
    for change in manifest["profile_changes"]:
        key = (change["glyph"], change["native_offset"])
        if key in actual_changes or not change["reason"]:
            raise AssertionError("manifest duplicate or unexplained profile change")
        actual_changes[key] = (change["before_ru"], change["value"])
    check(actual_changes, expected_changes, "manifest every numeric profile change and RUS origin")
    check(len(manifest["levels"]), 3, "manifest three unified levels")
    numeric_differences = map_cells = 0
    for number in (1, 2, 3):
        ru, en = original["ru"]["levels"][number], original["en"]["levels"][number]
        common, _, _, _ = unified_oracle(original, number, "ru")
        expected = {}
        for old, other, chosen in zip(ru["objects"], en["objects"], common["objects"]):
            check((old["class"], old["glyph"]), (other["class"], other["glyph"]), "source object correspondence")
            for wire_key, source_key in (("flags", "flags"), ("sprite_flags", "sprite_flags"),
                                         ("width_parameter", "width"), ("height_parameter", "height"),
                                         ("marker", "marker")):
                if old[source_key] != other[source_key]:
                    expected[("object", old["glyph"], wire_key)] = (old[source_key], other[source_key], chosen[source_key])
            other_fields = {(k, off): value for k, off, value in other["fields"]}
            chosen_fields = {(k, off): value for k, off, value in chosen["fields"]}
            for kind, offset, value in old["fields"]:
                if kind in (W, L, C) and value != other_fields[(kind, offset)]:
                    expected[("field", old["glyph"], offset)] = (value, other_fields[(kind, offset)], chosen_fields[(kind, offset)])
        metadata = manifest["levels"][number - 1]["source_differences"]
        observed = {}
        for difference in metadata["numeric"]:
            key = (difference["scope"], difference["glyph"], difference.get("field", difference.get("offset")))
            if key in observed or not difference["policy"]:
                raise AssertionError("duplicate or unexplained source difference")
            observed[key] = (difference["ru"], difference["en"], difference["chosen"])
        check(observed, expected, f"LEVEL{number} all original numeric differences and chosen values")
        numeric_differences += len(expected)
        ru_native, _ = startup(ru, original["ru"]["resources"])
        en_native, _ = startup(en, original["en"]["resources"])
        check(len(metadata["maps"]), 3, "three original map-layer comparisons")
        for index, plane in enumerate(("MMap", "TMap", "FCMap")):
            r, e = bytes.fromhex(ru_native["maps"][index]), bytes.fromhex(en_native["maps"][index])
            expected_cells = [{"cell": i, "ru": rv, "en": ev} for i, (rv, ev) in enumerate(zip(r, e)) if rv != ev]
            layer = metadata["maps"][index]
            check((layer["plane"], layer["chosen"]), (plane, "RUS baseline"), "deferred map source policy")
            check(layer["changed_cells"], expected_cells, f"LEVEL{number} exact runtime-oriented {plane} differences")
            if not layer["ENG_candidates"]:
                raise AssertionError("English map-candidate status missing")
            map_cells += len(expected_cells)
    return {"source_fingerprints": 2, "runtime_input_fingerprints": 6,
            "profile_changes": len(expected_changes), "resource_records": len(package.resources),
            "source_numeric_differences_verified": numeric_differences,
            "source_runtime_oriented_map_differences_verified": map_cells}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packer", type=Path, default=ROOT / "scripts/pack_gba_assets.py")
    parser.add_argument("--tool", type=Path, default=ROOT / "build/native/dok-rom-assets.exe")
    parser.add_argument("--tests", type=Path, default=ROOT / "build/native/dok-rom-assets-tests.exe")
    args = parser.parse_args()
    with temporary_workspace(CHECK_ROOT, "repeat-") as temporary:
        verify(args, Path(temporary))


def verify(args, temporary):
    original = originals()
    outputs = [temporary / "repeat-a", temporary / "repeat-b"]
    commands = []
    for output in outputs:
        command = [sys.executable, str(args.packer.resolve()), "--output", str(output)]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True)
        commands.append({"command": command, "exit_code": result.returncode})
    first, second = outputs
    for name in ("assets.bin", "manifest.json"):
        check((first / name).read_bytes(), (second / name).read_bytes(), name + " deterministic fresh output")
    data = (first / "assets.bin").read_bytes()
    package = Package(data, original)
    manifest = json.loads((first / "manifest.json").read_text(encoding="utf-8"))
    manifest_checks = verify_manifest(manifest, package, original)
    report = {"task": "G03", "format_version": 1, "profile_id": 1,
              "status": "running", "verification_kind": "independent_explicit_little_endian_parser_and_profile_oracle",
              "not_a_game": True, "fps_measured": False,
              "checker_sha256": sha(Path(__file__).read_bytes()),
              "packer_sha256": sha(args.packer.read_bytes()), "tool_sha256": sha(args.tool.read_bytes()),
              "commands": commands,
              "determinism": {name: {"identical": True, "size": (first / name).stat().st_size,
                                       "sha256": sha((first / name).read_bytes())}
                              for name in ("assets.bin", "manifest.json")},
              "originals": {language: {"exe_sha256": value["exe_sha256"], "res_sha256": value["archive_sha256"]}
                            for language, value in original.items()},
              "resources": {"directory_entries": len(package.resources), "original_language_variants_checked": 2 * len(package.resources),
                            "source_LEVEL_plaintext_excluded": 6,
                            "payloads": [{"id": r["id"], "variants": r["variants"]} for r in package.resources]},
              "views": [],
              "manifest_checks": manifest_checks,
              "policy_origins": {"source": "docs/gba-balance-proposal.md", "unresolved_geometry_and_numeric": "RUS",
                                 "boyar_course24": "ENG LEVEL1, all 33 definitions",
                                 "boyars81_86_speed20_wake9000_health42": "ENG LEVEL1",
                                 "boyar8c_health12": "ENG LEVEL1",
                                 "first_witch": "hybrid: HP54/speed60/width7/wake3400",
                                 "mouse_course8": "RUS LEVEL1, all 30 definitions",
                                 "localization": "original TEXT fields matched by class/glyph/native_offset; resource variants selected by language"}}
    for number in (1, 2, 3):
        shared = []
        for language in ("ru", "en"):
            source, native, states, policy_counts = unified_oracle(original, number, language)
            summary = package.view(number, language, source, native, states)
            wire = subprocess.check_output([str(args.tool.resolve()), str(first / "assets.bin"), str(number), language], cwd=ROOT)
            actual = json.loads(wire)
            semantic = verify_model(actual, source, native, states, f"packed LEVEL{number} {language}")
            directory = CHECK_ROOT / "decoded" / language
            summary["oracle"] = save(directory / f"LEVEL{number}-oracle.json", source)
            summary["decoded_C_reader"] = save(directory / f"LEVEL{number}-reader.json", actual)
            summary["policy_counts"] = policy_counts
            summary["native_known_bytes_checked"] = semantic["native_known_bytes"]
            summary["numeric_fields_checked"] = semantic["numeric_fields"]
            summary["localized_text_fields_checked"] = semantic["text_fields"]
            summary["resource_fields_checked"] = semantic["resource_fields"]
            metadata = manifest["levels"][number - 1]
            check((metadata["level"], metadata["baseline"]), (number, "ru"), "manifest level baseline")
            locale = metadata["languages"][language]
            for field, summary_field in (("object_count", "definitions"), ("actor_count", "actors"),
                                         ("field_count", "fields"), ("binding_count", "bindings_count"),
                                         ("text_size", "text_size"), ("startup_sha256", "startup_sha256")):
                check(locale[field], summary[summary_field], "manifest view metadata " + field)
            check(locale["maps_sha256"], summary["maps_sha256"], "manifest geometry fingerprint")
            report["views"].append(summary)
            shared.append(summary)
        for key in ("maps_sha256", "initial_objects_sha256", "initial_actors_sha256", "initial_player_sha256"):
            check(shared[0][key], shared[1][key], f"LEVEL{number} RU/EN common " + key)
    report["ranges"] = package.validate_ranges()
    report["real_package_corruption_checks"] = corruption_checks(data, package, args.tool)
    result = subprocess.run([str(args.tests.resolve())], cwd=ROOT, capture_output=True, text=True, check=True)
    real_test = subprocess.run([str(args.tests.resolve()), str(first / "assets.bin")], cwd=ROOT, capture_output=True, text=True, check=True)
    report["reader_tests"] = {"path": str(args.tests), "sha256": sha(args.tests.read_bytes()),
                               "source_sha256": sha((ROOT / "tests/test_rom_assets.c").read_bytes()),
                               "synthetic": {"exit_code": result.returncode, "stdout": result.stdout.strip(), "stderr": result.stderr.strip()},
                               "actual_package_language_switch": {"exit_code": real_test.returncode, "stdout": real_test.stdout.strip(), "stderr": real_test.stderr.strip()},
                               "coverage": ["explicit LE fixture, stable ID/name lookup, semantic resource bindings",
                                            "localized text changes while mutable runtime remains byte-identical",
                                            "failed lookups/open preserve caller outputs",
                                            "misaligned base, truncated header, version, size, reserved-byte, range and binding corruption rejection",
                                            "different RU/EN common geometry rejected even when replacement bytes are equal",
                                            "all three real levels switch RU/EN both directions while preserving health, inventory, maps and reserved state",
                                            "all six real language views resolve a 768-byte palette resource"]}
    for language, info in original.items():
        check(sha(info["exe_path"].read_bytes()), info["exe_sha256"], language + " EXE unchanged after packing/checking")
        check(sha(info["archive_path"].read_bytes()), info["archive_sha256"], language + " RES unchanged after packing/checking")
    report["originals_unchanged"] = True
    report["source_authentic_modes"] = "not included; original language-specific geometry remains in private G02 controls"
    report["status"] = "passed"
    save(ROOT / "analysis/packing-report.json", report)
    print(json.dumps({"status": report["status"], "views": len(report["views"]),
                      "resources": len(package.resources), "package_bytes": len(data),
                      "deterministic": True, "originals_unchanged": True}, indent=2))


if __name__ == "__main__":
    main()
