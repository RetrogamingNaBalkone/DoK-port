"""Verify startup maps and every known scalar byte independently of the C builder.

This is static reconstruction verification, not DOS runtime equivalence.
"""
import json
import re
import subprocess
from pathlib import Path
from analyze_levels import reference, W, L, C, T
from analyze_resources import parse, sha
from analyze_animations import animation
from inventory import EXPECTED

ROOT = Path(__file__).resolve().parents[1]
SIZES = [0x66, 0xde, 0x54, 0x81, 0x54, 0xa2, 0x5a, 0x7e, 0x83, 0xf8, 0x87, 0, 0x6a, 0x6b]


def signed(n, bits=16):
    return (n + 2**(bits-1)) % 2**bits - 2**(bits-1)


class State:
    def __init__(self, definition):
        self.definition = definition
        self.values = {}
        self.placements = 0

    def put(self, offset, value, width=2):
        for i in range(width):
            self.values[offset+i] = (value >> (8*i)) & 255

    def get(self, offset, width=2, sign=False):
        n = int.from_bytes(bytes(self.values[offset+i] for i in range(width)), "little")
        return signed(n, width*8) if sign else n

    def list10(self, offset):
        self.put(offset, 0)
        self.put(offset+6, 0, 4)

    def model(self):
        raw, mask = bytearray(248), bytearray(31)
        for off, value in self.values.items():
            raw[off] = value
            mask[off//8] |= 1 << (off % 8)
        return dict(glyph=self.definition["glyph"], **{"class": self.definition["class"]},
                    size=SIZES[self.definition["class"]], placements=self.placements,
                    bytes=raw.hex(), known=mask.hex())


def startup(level, resources):
    states = []

    def frame0(d):
        return animation(resources[d["animation"]])[0][0]

    for d in level["objects"]:
        s = State(d)
        cls = d["class"]
        for offset, value in {0: 0, 2: 4, 0x12: cls, 0x15: d["flags"], 0x17: d["width"],
                              0x19: d["height"], 0x24: 0, 0x50: 0}.items():
            s.put(offset, value)
        s.put(0x14, d["glyph"], 1)
        s.put(0x1b, d["marker"], 1)
        if d["flags"] & 0x800:
            f = frame0(d)
            s.put(0x20, f["parameter_a"])
            s.put(0x22, f["parameter_b"])
            s.put(0x52, f["interval_bits"])
        if cls in (1, 5, 9):
            s.list10(0x86)
            s.put(0x5c, d["glyph"], 1)
            s.put(0x5d, 32, 1)
            for offset, value in {0x36: 3, 0x7e: 0, 0x7c: 0, 0x98: 1, 0x80: 1,
                                  0x84: 0, 0x82: 0, 0x5a: 0}.items():
                s.put(offset, value)
            for offset in (0x3c, 0x94, 0x90, 0x1c):
                s.put(offset, 0, 4)
        if cls == 1:
            for offset in range(0xaa, 0xce, 2):
                s.put(offset, 0)
            for offset, value in {0x5a: 4, 0xd6: 3, 0xd0: 0, 0xce: 0, 0xd8: 0}.items():
                s.put(offset, value)
            s.put(0xda, 0, 4)
        elif cls == 6:
            for off in (0x58, 0x52, 0x56, 0x54):
                s.put(off, 0)
        elif cls == 7:
            s.put(0x54, 2, 1)
            s.put(0x55, 1, 1)
            for off, value in {0x5c: 0, 0x52: 0, 0x5a: 0, 0x58: 0, 0x60: 1,
                               0x56: 0, 0x6a: d["flags"]}.items():
                s.put(off, value)
        elif cls == 8:
            s.list10(0x6d)
            s.put(0x68, 32, 1)
            s.put(0x50, 0)
            s.put(0x58, 0)
            f = frame0(d)
            for off, value in ((0x54, f["parameter_a"]), (0x56, f["parameter_b"]), (0x52, f["interval_bits"])):
                s.put(off, value)
        elif cls == 9:
            s.list10(0xb0)
            s.put(0xa2, 0)
            s.put(0xe6, 0, 1)
        elif cls == 10:
            s.put(0x83, 4)
        elif cls == 12:
            s.put(0x52, 0)
            s.put(0x50, 18)
        for kind, offset, value in d["fields"]:
            if kind in (W, L, C):
                s.put(offset, value, {W: 2, L: 4, C: 1}[kind])
            elif kind == T:
                for i, value in enumerate(bytes.fromhex(value) + b"\0"):
                    s.put(offset+i, value, 1)
        if cls == 8:
            s.put(0x3c, -s.get(0x3c, 4), 4)
            s.put(0x2a, signed(-d["height"]), 4)
            s.put(0x5e, s.get(0x5c, sign=True) // 2)
        states.append(s)
    by_glyph = {s.definition["glyph"]: s for s in states}
    width, _, length, side, *_ = level["settings"]
    planes = []
    for encoded in (level["maps"][1], level["maps"][0], level["maps"][2]):
        raw = bytes.fromhex(encoded)
        rows = [raw[i:i+width] for i in range(0, len(raw), width)]
        normalized = b"".join(reversed(rows)).replace(b".", b" ")
        assert all(g in by_glyph for g in normalized)
        planes.append(normalized)
    total, player = 0, 0
    for index, (actor, terrain) in enumerate(zip(planes[0], planes[1])):
        s = by_glyph[terrain]
        if s.definition["class"] == 3 and s.get(0x54, sign=True) == 99:
            total = signed(total + s.get(0x56, sign=True))
        s = by_glyph[actor]
        cls = s.definition["class"]
        if cls in (1, 9):
            y = signed(-s.definition["height"])
            s.put(0x26, (index % width)*side + side//2, 4)
            s.put(0x2a, y, 4)
            s.put(0x2e, (index // width)*side + side//2, 4)
            s.put(0x9e, y + (26 if cls == 1 else 0))
            s.put(0xa0, y - (26 if cls == 1 else 0))
            s.placements += 1
            if cls == 1:
                player = states.index(s) + 1
    planes.append(bytes(width*length))
    return dict(player_index=player, pickup_99_total=total, maps=[p.hex() for p in planes],
                objects=[s.model() for s in states]), states


def main():
    verification = dict(stage=6, levels=0, objects=0, known_bytes=0, placed_actor_cells=0,
                        originals={}, ghidra_roles={}, cross_target_execution="not performed")
    for lang, (exe, expected_sha) in EXPECTED.items():
        exe_bytes = (ROOT / "originals/pkr" / exe).read_bytes()
        assert sha(exe_bytes) == expected_sha
        inv = json.loads((ROOT / f"analysis/inventory-{lang}.json").read_text(encoding="utf-8"))
        archive_path = ROOT / inv["resource"]["file"]
        archive = archive_path.read_bytes()
        assert sha(archive) == inv["resource"]["sha256"]
        entries = parse(archive)
        resources = {e["name_hex"]: archive[e["offset"]:e["offset"]+e["size"]] for e in reversed(entries)}
        directory = ROOT / "build/worlds" / lang
        directory.mkdir(parents=True, exist_ok=True)
        reports = []
        for index in (18, 19, 20):
            entry = entries[index]
            level = reference(resources[entry["name_hex"]])
            expected, states = startup(level, resources)
            actual = json.loads(subprocess.check_output([str(ROOT / "build/native/dok-world.exe"),
                                                        str(archive_path), entry["name"]]))
            assert actual == expected, (lang, index)
            (directory / f"{index:04d}.json").write_text(json.dumps(actual, indent=2)+"\n", encoding="utf-8")
            actors = []
            for s in states:
                if s.placements:
                    actors.append(dict(glyph=s.definition["glyph"], class_id=s.definition["class"],
                                       placements=s.placements, x=s.get(0x26, 4, True),
                                       y=s.get(0x2a, 4, True), z=s.get(0x2e, 4, True),
                                       heading=s.get(0x32, sign=True)))
            report = dict(resource_index=index, level_sha256=entry["sha256"], objects=len(states),
                          placed_actor_cells=sum(s.placements for s in states),
                          unique_placed_actors=len(actors), unplaced_actor_glyphs=[s.definition["glyph"] for s in states
                              if s.definition["class"] in (1, 9) and not s.placements],
                          repeated_actor_glyphs=[a["glyph"] for a in actors if a["placements"]>1],
                          pickup_99_total=expected["pickup_99_total"], known_bytes=sum(len(s.values) for s in states),
                          maps={name: sha(bytes.fromhex(p)) for name, p in zip(("MMap", "TMap", "FCMap", "visited"), actual["maps"])},
                          player=next((a for a in actors if a["class_id"]==1), None), actors=actors)
            reports.append(report)
            verification["levels"] += 1
            verification["objects"] += len(states)
            verification["known_bytes"] += report["known_bytes"]
            verification["placed_actor_cells"] += report["placed_actor_cells"]
        (ROOT / f"analysis/worlds-{lang}.json").write_text(json.dumps(reports, indent=2)+"\n", encoding="utf-8")
        roles = (ROOT / f"build/stage6/{lang}/roles.tsv").read_text(encoding="utf-8").splitlines()
        verification["ghidra_roles"][lang] = len(roles)-1
        log = (ROOT / f"build/stage6/{lang}/ghidra.log").read_text(encoding="utf-16")
        assert "KREMLIN_STAGE6_COMPLETE" in log and not re.search(r"^ERROR", log, re.M)
        assert sha((ROOT / "originals/pkr" / exe).read_bytes()) == expected_sha
        assert sha(archive_path.read_bytes()) == sha(archive)
        verification["originals"][lang] = dict(exe=expected_sha, res=sha(archive))
    verification["artifacts"] = {}
    for name in ("dok-world.exe", "dok-world-tests.exe", "dok-world-linux", "world-arm7tdmi.o"):
        raw = (ROOT / "build/native" / name).read_bytes()
        if name.endswith(".exe"):
            assert raw[:2] == b"MZ"
        else:
            assert raw[:4] == b"\x7fELF" and int.from_bytes(raw[18:20], "little") == (40 if name.endswith(".o") else 62)
        verification["artifacts"][name] = dict(size=len(raw), sha256=sha(raw))
    (ROOT / "analysis/stage6-verification.json").write_text(json.dumps(verification, indent=2)+"\n", encoding="utf-8")
    roles = ["language\taddress\trole"]
    for lang in EXPECTED:
        roles += [lang+"\t"+r for r in (ROOT / f"build/stage6/{lang}/roles.tsv").read_text(encoding="utf-8").splitlines()[1:]]
    (ROOT / "analysis/stage6-roles.tsv").write_text("\n".join(roles)+"\n", encoding="utf-8")
    print(json.dumps(verification, indent=2))


if __name__ == "__main__":
    main()
