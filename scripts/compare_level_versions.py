"""Compare LEVEL directly in original RES; emit metadata, never full game text."""
import collections
import json
import subprocess
from pathlib import Path

from analyze_levels import reference
from analyze_resources import parse, sha

ROOT = Path(__file__).resolve().parents[1]


def main():
    sources, levels = {}, {}
    for lang in ("ru", "en"):
        inventory = json.loads((ROOT / f"analysis/inventory-{lang}.json").read_text(encoding="utf-8"))
        path = ROOT / inventory["resource"]["file"]
        archive = path.read_bytes()
        assert sha(archive) == inventory["resource"]["sha256"]
        sources[lang] = dict(file=inventory["resource"]["file"], sha256=sha(archive), levels=[])
        levels[lang] = []
        for entry in parse(archive)[18:21]:
            payload = archive[entry["offset"]:entry["offset"] + entry["size"]]
            model = reference(payload)
            extracted = ROOT / f"build/resources/{lang}/{entry['index']:04d}.bin"
            assert extracted.read_bytes() == payload
            native = json.loads(subprocess.check_output([str(ROOT / "build/native/dok-level.exe"), str(extracted)]))
            assert model == native
            levels[lang].append(model)
            sources[lang]["levels"].append({k: entry[k] for k in ("index", "name", "offset", "size", "sha256")})
        assert sha(path.read_bytes()) == sha(archive)

    comparisons = []
    for number, (ru, en) in enumerate(zip(levels["ru"], levels["en"]), 1):
        assert ru["settings"] == en["settings"]
        assert ru["palette"] == en["palette"] and ru["light"] == en["light"]
        assert len(ru["objects"]) == len(en["objects"])
        maps = {}
        for name, a, b in zip(("TMap", "MMap", "FCMap"), ru["maps"], en["maps"]):
            a, b = bytes.fromhex(a), bytes.fromhex(b)
            maps[name] = [dict(x=i % 64, y=i // 64, ru=f"{x:02x}", en=f"{y:02x}")
                          for i, (x, y) in enumerate(zip(a, b)) if x != y]
        counts = {}
        for lang, model in (("ru", ru), ("en", en)):
            count = collections.Counter()
            mmap = bytes.fromhex(model["maps"][1])
            for obj in model["objects"]:
                if obj["class"] == 9:
                    count[bytes.fromhex(obj["animation"]).decode("cp866")] += mmap.count(obj["glyph"])
            counts[lang] = dict(count)
        differences, text_changes = [], collections.Counter()
        for a, b in zip(ru["objects"], en["objects"]):
            assert (a["glyph"], a["class"]) == (b["glyph"], b["class"])
            common = {k: [a[k], b[k]] for k in ("flags", "width", "height", "marker", "sprite_flags") if a[k] != b[k]}
            assert all(a[k] == b[k] for k in ("sprite", "animation", "sound"))
            fields = []
            assert len(a["fields"]) == len(b["fields"])
            for x, y in zip(a["fields"], b["fields"]):
                assert x[:2] == y[:2]
                if x != y:
                    if x[0] == 6:
                        text_changes[a["class"]] += 1
                    else:
                        fields.append(dict(native_offset=f"0x{x[1]:02X}", kind=x[0], ru=x[2], en=y[2]))
            if common or fields:
                differences.append(dict(glyph=f"{a['glyph']:02x}", class_id=a["class"], common=common, fields=fields))
        comparisons.append(dict(level=number, settings=ru["settings"], map_changes=maps,
                                enemy_placements=counts, numeric_differences=differences,
                                changed_text_fields_by_class=dict(text_changes)))
    report = dict(sources=sources, coordinate_system="zero-based x=column,y=row; no native map border",
                  validation="original RES hashes before/after; Python model equals C model for all six LEVEL",
                  levels=comparisons)
    output = ROOT / "analysis/level-version-comparison.json"
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("Verified six LEVEL resources; wrote analysis/level-version-comparison.json")


if __name__ == "__main__":
    main()
