#!/usr/bin/env python3
"""N22: replay retained Ghidra PKR/PKE machine code and audit original RES.

No GBA implementation imports. Sound/HUD are recorded external calls; native
restore is replayed separately with the far runtime-copy boundary recorded.
This is a controlled x86 slice, never a whole DOS session or playthrough.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "ports/nds/tools"))
from audit_world import originals, parse_level
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, __version__ as unicorn_version
from unicorn import x86_const as reg

OUT = ROOT / "research/evidence/nds/n22/original"
LANGUAGES = {
    "ru": dict(sample="originals/pkr/RUS/PKR.EXE", sha="12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28",
               segment=0x1068, death=0x193e, stat=0x1cc2, world_segment=0x1098,
               helper=0x125b, world_pointer=0x266a, mmap=0x608a,
               sound=(0x1108, 0x1f7), hud=(0x10c0, 0x1395)),
    "en": dict(sample="originals/pkr/ENG/PKE.EXE", sha="cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b",
               segment=0x1020, death=0x1efc, stat=0x2276, world_segment=0x1030,
               helper=0x133f, world_pointer=0x271a, mmap=0x613a,
               sound=(0x1060, 0x173d), hud=(0x1050, 0x16f8)),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def word_wrap(value):
    return (value + 32768) % 65536 - 32768


def native_cell(x, z):
    # Signed IDIV truncates toward zero, followed by WORD and clamp.
    def quotient(value):
        result = abs(value) // 480
        return word_wrap(-result if value < 0 else result)
    return min(63, max(0, quotient(x))) + 64 * min(63, max(0, quotient(z)))


def machine(language, cfg):
    folder = ROOT / "research/evidence/flow" / language
    assert sha(ROOT / cfg["sample"]) == cfg["sha"]
    manifest = json.loads((folder / "analysis-manifest.json").read_text(encoding="utf-8-sig"))
    assert manifest["sha256_before"] == manifest["sha256_after"] == cfg["sha"]
    asm = (folder / "evidence.asm").read_text(encoding="utf-8")
    assert f'FUNCTION {cfg["segment"]:04x}:{cfg["death"]:04x}' in asm
    cpu = Uc(UC_ARCH_X86, UC_MODE_16)
    cpu.mem_map(0, 0x100000)
    cpu.mem_write(cfg["segment"] * 16, (folder / f'{cfg["segment"]:04x}.bin').read_bytes())
    calls = []

    def external(c, address, size, unused):
        cs, ip = c.reg_read(reg.UC_X86_REG_CS), c.reg_read(reg.UC_X86_REG_IP)
        if cs in (cfg["segment"], 0x7000):
            return
        entry = cs, ip
        assert entry in (cfg["sound"], cfg["hud"], (cfg["world_segment"], cfg["helper"])), entry
        sp = c.reg_read(reg.UC_X86_REG_SP)
        args = bytes(c.mem_read(0x30000 + sp + 4, 4))
        calls.append(dict(entry=f"{cs:04x}:{ip:04x}", args=args.hex()))
        if cs == cfg["world_segment"]:
            assert args == struct.pack("<HH", 0, 0x5000)
            c.mem_write(0x40026, bytes(c.mem_read(0x50716, 30)))
        target, selector = struct.unpack("<HH", c.mem_read(0x30000 + sp, 4))
        c.reg_write(reg.UC_X86_REG_SP, sp + 4)
        c.reg_write(reg.UC_X86_REG_CS, selector)
        c.reg_write(reg.UC_X86_REG_IP, target)

    cpu.hook_add(UC_HOOK_CODE, external)

    def run(entry, args):
        for register, value in ((reg.UC_X86_REG_CS, cfg["segment"]), (reg.UC_X86_REG_SS, 0x3000),
                                (reg.UC_X86_REG_DS, 0x3000), (reg.UC_X86_REG_SP, 0xf000),
                                (reg.UC_X86_REG_BP, 0xf800)):
            cpu.reg_write(register, value)
        calls.clear()
        cpu.mem_write(0x3f000, struct.pack("<HH", 0xfff0, 0x7000) + args)
        cpu.emu_start(cfg["segment"] * 16 + entry, 0x7fff0, count=100000)
        assert cpu.reg_read(reg.UC_X86_REG_SP) == 0xf004
        assert cpu.reg_read(reg.UC_X86_REG_BP) == 0xf800
        return cpu.reg_read(reg.UC_X86_REG_AX)

    cpu.mem_write(0x30000 + cfg["world_pointer"], struct.pack("<HH", 0, 0x5000))
    return cpu, calls, run, folder


def death_vectors(language, cfg):
    cpu, calls, run, folder = machine(language, cfg)
    result = []
    for ordinal, (hp, lives, old, new, forced) in enumerate([
        (100, 3, (960, 960), (480, 480), 0), (-1, 3, (960, 960), (480, 480), 0),
        (-32768, 3, (960, 960), (480, 480), 0), (0, 3, (960, 960), (480, 480), 0),
        (0, 2, (-960, 99999), (99999, -960), 0), (0, 1, (960, 960), (480, 480), 0),
        (0, 0, (960, 960), (480, 480), 0), (0, -32768, (99999, -960), (-960, 99999), 0),
        (0, 3, (-479, -479), (0, 0), 0), (0, 3, (480, 480), (480, 480), 0),
        (0, 3, (480*65536+480, 480*65536+960), (480, 960), 0),
        (0, 3, (2147483647, -2147483648), (480*32768, 480*65535), 0),
        (100, 3, (960, 960), (480, 480), 1), (0, 3, (960, 960), (480, 480), 1),
    ]):
        hero = bytearray((i * 17 + ordinal * 11 + 9) & 255 for i in range(0xe0))
        struct.pack_into("<iii", hero, 0x26, old[0], -300, old[1])
        hero[0x5c] = ord("@")
        struct.pack_into("<hhh", hero, 0x5e, hp, 42, 111)
        struct.pack_into("<hh", hero, 0xd6, lives, 7)
        pose = bytearray((i * 37 + ordinal * 7) & 255 for i in range(30))
        struct.pack_into("<iii", pose, 0, new[0], -300, new[1])
        world = bytearray(0x800)
        struct.pack_into("<h", world, 0, 64)
        struct.pack_into("<h", world, 4, 64)
        struct.pack_into("<h", world, 0x738, 480)
        struct.pack_into("<I", world, 0x742, 64)
        struct.pack_into("<HH", world, 0x734, 0, 0x4000)
        world[0x716:0x734] = pose
        occupancy = bytearray((i * 13 + 0x40) & 255 for i in range(4096))
        cpu.mem_write(0x40000, bytes(hero))
        cpu.mem_write(0x50000, bytes(world))
        cpu.mem_write(0x30000 + cfg["mmap"], bytes(occupancy))
        cpu.mem_write(0x300c6, struct.pack("<H", forced))
        expected_hero, expected_map = hero[:], occupancy[:]
        left = word_wrap(lives - 1)
        outcome = 3 if forced or hp < 0 else 0 if hp > 0 else 2 if left == 0 else 1
        if not forced and hp == 0:
            struct.pack_into("<h", expected_hero, 0xd6, left)
            if left:
                expected_hero[0x26:0x44] = pose
                struct.pack_into("<h", expected_hero, 0x5e, 111)
                expected_map[native_cell(*old)] = 32
                expected_map[native_cell(*new)] = ord("@")
        assert run(cfg["death"], struct.pack("<HH", 0, 0x4000)) == outcome
        assert bytes(cpu.mem_read(0x40000, len(hero))) == expected_hero
        assert bytes(cpu.mem_read(0x50000, len(world))) == world
        assert bytes(cpu.mem_read(0x30000 + cfg["mmap"], 4096)) == expected_map
        assert len(calls) == (0 if forced or hp != 0 else 1 if left == 0 else 3)
        result.append(dict(hp=hp, lives=lives, forced=forced, old=old, new=new,
                           result=outcome, lives_after=struct.unpack_from("<h", expected_hero, 0xd6)[0],
                           full_hero_and_mmap_exact=True, world_unchanged=True, external_calls=calls[:]))
    return result, folder


def restore_helper(cfg, folder):
    cpu = Uc(UC_ARCH_X86, UC_MODE_16)
    cpu.mem_map(0, 0x100000)
    cpu.mem_write(cfg["world_segment"] * 16, (folder / f'{cfg["world_segment"]:04x}.bin').read_bytes())
    hero = bytes((i * 7 + 51) & 255 for i in range(0xe0))
    world = bytearray(0x800)
    pose = bytes((i * 23 + 19) & 255 for i in range(30))
    world[0x716:0x734] = pose
    struct.pack_into("<HH", world, 0x734, 0, 0x4000)
    cpu.mem_write(0x40000, hero)
    cpu.mem_write(0x50000, bytes(world))
    copied = []

    def runtime(c, address, size, unused):
        if c.reg_read(reg.UC_X86_REG_CS) != 0x1000:
            return
        assert c.reg_read(reg.UC_X86_REG_IP) == 0x5cb3
        sp = c.reg_read(reg.UC_X86_REG_SP)
        args = struct.unpack("<4H", c.mem_read(0x30000 + sp + 4, 8))
        count = c.reg_read(reg.UC_X86_REG_CX)
        assert args == (0x716, 0x5000, 0x26, 0x4000) and count == 30
        c.mem_write(0x40026, bytes(c.mem_read(0x50716, count)))
        copied.append(dict(src=list(args[:2]), dst=list(args[2:]), cx=count))
        ip, cs = struct.unpack("<HH", c.mem_read(0x30000 + sp, 4))
        c.reg_write(reg.UC_X86_REG_SP, sp + 12)
        c.reg_write(reg.UC_X86_REG_CS, cs)
        c.reg_write(reg.UC_X86_REG_IP, ip)

    cpu.hook_add(UC_HOOK_CODE, runtime)
    for register, value in ((reg.UC_X86_REG_CS, cfg["world_segment"]), (reg.UC_X86_REG_SS, 0x3000),
                            (reg.UC_X86_REG_DS, 0x3000), (reg.UC_X86_REG_SP, 0xf000), (reg.UC_X86_REG_BP, 0xf800)):
        cpu.reg_write(register, value)
    cpu.mem_write(0x3f000, struct.pack("<4H", 0xfff0, 0x7000, 0, 0x5000))
    cpu.emu_start(cfg["world_segment"] * 16 + cfg["helper"], 0x7fff0, count=10000)
    assert bytes(cpu.mem_read(0x40000, len(hero))) == hero[:0x26] + pose + hero[0x44:]
    assert len(copied) == 1 and cpu.reg_read(reg.UC_X86_REG_SP) == 0xf004
    return copied[0]


def equality(language, cfg):
    cpu, calls, run, _ = machine(language, cfg)
    rows = []
    cpu.mem_write(0x50000, bytes(0x800))
    for score, total in [(0, 0), (0, 1), (1, 0), (1, 1), (-1, -1), (-1, 0), (32767, 32767), (-32768, -32768)]:
        hero = bytearray(0xe0)
        struct.pack_into("<h", hero, 0x5e, 77)
        struct.pack_into("<h", hero, 0xd8, score)
        cpu.mem_write(0x40000, bytes(hero))
        cpu.mem_write(0x50710, struct.pack("<h", total))
        run(cfg["stat"], struct.pack("<HHhh", 0, 0x4000, 1000, 1))
        hp = struct.unpack("<h", cpu.mem_read(0x4005e, 2))[0]
        assert hp == (-1 if score == total else 77)
        rows.append(dict(score=score, total=total, hp=hp))
    return rows


def resource_exits():
    archives, rows = originals(), []
    for language, archive in archives.items():
        for number in range(1, 4):
            entry = archive["entries"][17 + number]
            model = parse_level(entry["payload"])
            maps = [bytes.fromhex(value) for value in model["maps"]]
            exits, score_items = [], []
            for obj in model["objects"]:
                fields = {f["native_offset"]: f["value"] for f in obj["fields"]}
                if obj["class_id"] == 12 and fields.get(0x66) == 1000:
                    cells = [i for i, glyph in enumerate(maps[2]) if glyph == obj["glyph"]]
                    exits.append(dict(glyph=obj["glyph"], cells=cells, xy=[[i % 64, i // 64] for i in cells]))
                if obj["class_id"] == 3 and fields.get(0x54) == 99:
                    score_items.append(obj["glyph"])
            assert score_items == [] and len(exits) == 1 and exits[0]["glyph"] == 0xb2
            assert len(exits[0]["cells"]) == (4 if number == 3 else 2)
            rows.append(dict(language=language, level=number, archive=archive["relative"],
                             archive_sha256=archive["sha256"], resource_sha256=entry["sha256"],
                             category99_definitions=score_items, total=0, exits=exits))
        assert sha(archive["path"]) == archive["sha256"]
    return rows


def main():
    report = dict(task="N22", status="passed", python=sys.version.split()[0], unicorn=unicorn_version,
                  whole_DOS_session=False, address_system="Ghidra NE-loader logical address; not file offset, NE index or DPMI selector",
                  language_results={}, resource_address_system="original LEVEL FCMap row-major cells", resource_exits=resource_exits())
    for language, cfg in LANGUAGES.items():
        vectors, folder = death_vectors(language, cfg)
        data_path = folder / ("1190.bin" if language == "ru" else "10d0.bin")
        blank_offset = 0x2ba if language == "ru" else 0x28e
        assert data_path.read_bytes()[blank_offset:blank_offset + 2] == b" \0"
        asm = (folder / "evidence.asm").read_text(encoding="utf-8")
        ctor = "1068:16a0\tMOV word ptr ES:[BX + 0xd6],0x3" if language == "ru" else "1020:1baa\tMOV word ptr ES:[BX + 0xd6],0x3"
        score_init = "1068:16ca\tMOV word ptr ES:[BX + 0xd8],0x0" if language == "ru" else "1020:1bd4\tMOV word ptr ES:[BX + 0xd8],0x0"
        assert ctor in asm and score_init in asm
        report["language_results"][language] = dict(sample=cfg["sample"], sha256_before=cfg["sha"],
            death_entry=f'{cfg["segment"]:04x}:{cfg["death"]:04x}', death_vectors=vectors,
            equality=equality(language, cfg), restore_helper=restore_helper(cfg, folder),
            initial_lives_instruction=ctor, initial_score_instruction=score_init,
            startup_status_message=dict(data_offset=blank_offset, bytes_hex="2000", meaning="blank HUD message; no retry/GameOver prompt", sha256=sha(data_path)),
            retained_ghidra_inputs={p.relative_to(ROOT).as_posix(): sha(p) for p in
                                   [folder / "evidence.asm", folder / "analysis-manifest.json", folder / f'{cfg["segment"]:04x}.bin', folder / f'{cfg["world_segment"]:04x}.bin']},
            sha256_after=sha(ROOT / cfg["sample"]))
        assert report["language_results"][language]["sha256_after"] == cfg["sha"]
    report["script_sha256"] = sha(__file__)
    report["oracle_sha256"] = sha(ROOT / "ports/nds/tools/audit_world.py")
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "audit.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    compact = dict(task="N22", scope="original x86 slices and RES only; no port acceptance or whole DOS session",
                   status="passed", command="py -3.14 -B -X utf8 ports/nds/tools/flow/audit_original.py",
                   report="research/evidence/nds/n22/original/audit.json", report_sha256=sha(OUT / "audit.json"),
                   script_sha256=report["script_sha256"], python=report["python"], unicorn=report["unicorn"],
                   checks=dict(death=sum(len(v['death_vectors']) for v in report['language_results'].values()), category1000=16, native_far_restore_helper=2, original_levels=6),
                   samples=[dict(language=k, sample=v["sample"], sha256_before=v["sha256_before"], sha256_after=v["sha256_after"])
                            for k, v in report["language_results"].items()],
                   exit_cells=[dict(language=v["language"], level=v["level"], total=v["total"], cells=v["exits"][0]["cells"])
                               for v in report["resource_exits"]])
    (ROOT / "analysis/nds-n22-original-flow.json").write_text(json.dumps(compact, indent=2) + "\n", encoding="utf-8")
    print(f"N22 original PASS: {compact['checks']['death']} death vectors, 16 category1000 vectors, 2 native far helpers, 6 LEVEL exit audits")


if __name__ == "__main__":
    main()
