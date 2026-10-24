"""Independent byte scanner vs C LEVEL model; resolve every resource reference.

Full decoded definitions stay in ignored build/. analysis/ contains metadata only.
"""
import collections
import json
import re
import subprocess
from pathlib import Path
from analyze_resources import parse
from inventory import EXPECTED, sha

ROOT = Path(__file__).resolve().parents[1]
# Kind, destination byte offset in the native class, in extraction order.
W, L, C, I, A, S, T = range(7)
SURFACE = [(W, 0x54), (I, 0x56), (I, 0x5a), (I, 0x5e), (I, 0x62)]
MOTION = [(L, 0x38), (W, 0x32), (W, 0x34)]
SCHEMA = {
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
KEYS = [b"Level Width", b"Level Height", b"Level Length", b"Cell Side",
        b"Cell Height", b"Floor Color", b"Ceiling Color", b"Min Dis"]


def reference(data):
    settings, objects, maps = {}, [], {key: [] for key in (b"TMap", b"MMap", b"FCMap")}
    section = b""
    sprite = animation = sound = b""
    sprite_flags = ignored = truncated = 0
    for line in re.split(rb"\r\n|\r|\n", data):
        if not line or line.startswith(b"#"):
            continue
        if line.startswith(b"["):
            section = line[1:-1]
        elif section == b"Game Ini":
            k, v = line.split(b"=", 1)
            if k.strip() == b"Step Ver":
                ignored += 1
            else:
                settings[k.strip()] = int(v)
        elif section in (b"Palette", b"Light"):
            name = line.split(b"=", 1)[1].lstrip(b" ")
            if section == b"Palette":
                palette = name.hex()
            else:
                light = name.hex()
        elif section in maps:
            maps[section].append(line)
        elif section == b"Game Object":
            if line.startswith(b"Sprite"):
                sprite, flag = line.split(b"=", 1)[1].split()
                sprite_flags = int(flag)
                continue
            if line.startswith(b"Flic"):
                animation = line.split(b"=", 1)[1].lstrip(b" ")
                continue
            if line.startswith(b"Sound"):
                sound = line.split(b"=", 1)[1].lstrip(b" ")
                continue
            common = re.match(rb"(\S)\s+(\d+)\s+(\d+)\s+([+-]?\d+)\s+([+-]?\d+)\s+([+-]?\d+)", line)
            assert common
            glyph, cls, flags, width, height, marker = common.groups()
            cls, flags, width, height, marker = map(int, (cls, flags, width, height, marker))
            remaining, fields = line[common.end():], []
            for kind, offset in SCHEMA[cls]:
                if kind in (W, L):
                    match = re.match(rb"\s*([+-]?\d+)", remaining)
                    assert match, (cls, kind, offset)
                    value = int(match[1])
                    remaining = remaining[match.end():]
                    assert -(2**(15 if kind == W else 31)) <= value < 2**(15 if kind == W else 31)
                elif kind == C:
                    remaining = remaining.lstrip()
                    value, remaining = remaining[0], remaining[1:]
                elif kind == T:
                    truncated += int(len(remaining) > 39)
                    value, remaining = remaining[:39].hex(), b""
                else:
                    match = re.match(rb"\s*(\S+)", remaining)
                    assert match
                    value, remaining = match[1].hex(), remaining[match.end():]
                fields.append([kind, offset, value])
            assert not remaining.strip(), remaining
            objects.append(dict(glyph=32 if glyph == b"_" else glyph[0], **{"class": cls},
                                flags=flags, width=width, height=height, marker=marker & 255,
                                sprite_flags=sprite_flags, sprite=sprite.hex(),
                                animation=animation.hex(), sound=sound.hex(), fields=fields))
    return dict(settings=[settings[k] for k in KEYS], palette=palette, light=light,
                ignored_step_ver=ignored, truncated_messages=truncated, objects=objects,
                maps=[b"".join(maps[k]).hex() for k in (b"TMap", b"MMap", b"FCMap")])


def main():
    verification = {"stage": 5, "levels": 0, "definitions": 0, "field_values": 0,
                    "resource_references": 0, "original_sha256": {}, "ghidra_roles": {}}
    for lang, (exe, expected) in EXPECTED.items():
        assert sha((ROOT / "originals/pkr" / exe).read_bytes()) == expected
        inventory = json.loads((ROOT / f"analysis/inventory-{lang}.json").read_text(encoding="utf-8"))
        archive_path = ROOT / inventory["resource"]["file"]
        archive = archive_path.read_bytes()
        assert sha(archive) == inventory["resource"]["sha256"]
        entries = parse(archive)
        by_name = {e["name_hex"]: e for e in reversed(entries)}
        reports = []
        for index in (18, 19, 20):
            entry = entries[index]
            payload = archive[entry["offset"]:entry["offset"] + entry["size"]]
            path = ROOT / f"build/resources/{lang}/{index:04d}.bin"
            assert path.read_bytes() == payload
            actual = json.loads(subprocess.check_output([str(ROOT / "build/native/dok-level.exe"), str(path)]))
            assert actual == reference(payload), (lang, index)
            directory = ROOT / "build/levels" / lang
            directory.mkdir(parents=True, exist_ok=True)
            (directory / f"{index:04d}.json").write_text(json.dumps(actual, indent=2) + "\n", encoding="utf-8")
            references = []

            def resolve(name, kind):
                if not name:
                    return
                resource = by_name[name]
                block = archive[resource["offset"]:resource["offset"] + resource["size"]]
                if kind == S:
                    assert block[2:5] == b"SVM"
                elif kind == I:
                    assert int.from_bytes(block[10:14], "little") + 18 == len(block)
                elif kind == A:
                    assert resource["index"] in {r["resource_index"] for r in animations}
                references.append(resource["index"])

            animations_doc = json.loads((ROOT / f"analysis/animations-{lang}.json").read_text(encoding="utf-8"))
            animations = animations_doc["entries"]
            resolve(actual["palette"], None)
            resolve(actual["light"], None)
            glyphs = {o["glyph"] for o in actual["objects"]}
            final_sprite_flags, changes = {}, 0
            for o in actual["objects"]:
                for name, kind in ((o["sprite"], I), (o["animation"], A), (o["sound"], S)):
                    resolve(name, kind)
                for kind, offset, value in o["fields"]:
                    if kind in (I, A, S):
                        resolve(value, kind)
                if o["sprite"] in final_sprite_flags and final_sprite_flags[o["sprite"]] != o["sprite_flags"]:
                    changes += 1
                final_sprite_flags[o["sprite"]] = o["sprite_flags"]
            maps_report = {}
            for name, encoded in zip(("TMap", "MMap", "FCMap"), actual["maps"]):
                cells = bytes.fromhex(encoded)
                maps_report[name] = dict(sha256=sha(cells), cells=len(cells),
                                         undefined_glyphs=sorted(set(cells) - glyphs))
            count = sum(len(o["fields"]) for o in actual["objects"])
            reports.append(dict(resource_index=index, sha256=entry["sha256"],
                                definitions=len(actual["objects"]), field_values=count,
                                classes=dict(sorted(collections.Counter(o["class"] for o in actual["objects"]).items())),
                                maps=maps_report, reference_uses=len(references),
                                referenced_resources=sorted(set(references)),
                                sprite_flag_changes=changes, ignored_step_ver=actual["ignored_step_ver"],
                                truncated_messages=actual["truncated_messages"]))
            verification["levels"] += 1
            verification["definitions"] += len(actual["objects"])
            verification["field_values"] += count
            verification["resource_references"] += len(references)
        (ROOT / f"analysis/levels-{lang}.json").write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
        verification["original_sha256"][lang] = dict(exe=expected, res=sha(archive))
        roles = (ROOT / f"build/stage5/{lang}/roles.tsv").read_text(encoding="utf-8").splitlines()
        verification["ghidra_roles"][lang] = len(roles) - 1
        log = (ROOT / f"build/stage5/{lang}/ghidra.log").read_text(encoding="utf-16")
        assert "KREMLIN_STAGE5_COMPLETE" in log and not re.search(r"^ERROR", log, re.M)
        assert sha((ROOT / "originals/pkr" / exe).read_bytes()) == expected
        assert sha(archive_path.read_bytes()) == sha(archive)
    (ROOT / "analysis/stage5-verification.json").write_text(json.dumps(verification, indent=2) + "\n", encoding="utf-8")
    rows = ["language\taddress\trole"]
    for lang in EXPECTED:
        rows += [f"{lang}\t{row}" for row in (ROOT / f"build/stage5/{lang}/roles.tsv").read_text(encoding="utf-8").splitlines()[1:]]
    (ROOT / "analysis/stage5-roles.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    differences = []
    for index in (18, 19, 20):
        ru, en = [json.loads((ROOT / f"build/levels/{lang}/{index:04d}.json").read_text(encoding="utf-8"))
                  for lang in ("ru", "en")]
        changed = []
        assert ru["settings"] == en["settings"]
        for a, b in zip(ru["objects"], en["objects"]):
            assert (a["glyph"], a["class"]) == (b["glyph"], b["class"])
            common = {k: [a[k], b[k]] for k in ("flags", "width", "height", "marker", "sprite_flags") if a[k] != b[k]}
            fields = []
            for x, y in zip(a["fields"], b["fields"]):
                assert x[:2] == y[:2]
                if x != y:
                    item = dict(kind=x[0], native_offset=x[1])
                    if x[0] <= C:
                        item.update(ru=x[2], en=y[2])
                    fields.append(item)
            if common or fields:
                changed.append(dict(glyph=a["glyph"], class_id=a["class"], common=common, fields=fields))
        differences.append(dict(resource_index=index, objects=changed))
    (ROOT / "analysis/level-definition-differences.json").write_text(json.dumps(differences, indent=2) + "\n", encoding="utf-8")
    artifacts = {}
    for name in ("dok-level.exe", "dok-level-tests.exe", "dok-level-linux", "level-arm7tdmi.o"):
        binary = (ROOT / "build/native" / name).read_bytes()
        if name.endswith(".exe"):
            assert binary[:2] == b"MZ"
        else:
            assert binary[:4] == b"\x7fELF"
            assert int.from_bytes(binary[18:20], "little") == (40 if name.endswith(".o") else 62)
        artifacts[name] = dict(size=len(binary), sha256=sha(binary))
    verification["artifacts"] = artifacts
    verification["cross_target_execution"] = "not performed"
    (ROOT / "analysis/stage5-verification.json").write_text(json.dumps(verification, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verification, indent=2))


if __name__ == "__main__":
    main()
