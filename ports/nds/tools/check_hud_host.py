"""N19 strict host gate: original-resource pixel oracle vs the actual C UI.

The DLL includes the same ui/hud.c used by ARM9. A tiny exported wrapper also
exercises its internal bitmap primitive for all 256 unsigned CP866 glyphs.
This is a host check; actual ARM9 video output is a separate emulator gate.
"""
from __future__ import annotations

import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import subprocess
import sys
import time

from hud_pixel_oracle import OriginalHudArt, render_hud
from layout_ui import paginate

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n19/host-ui"
EVIDENCE = ROOT / "research/evidence/nds/n19"
U8 = C.c_ubyte
P8 = C.POINTER(U8)
CALLBACK = C.CFUNCTYPE(None, C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32)


class Values(C.Structure):
    _fields_ = [(key, C.c_int16) for key in ("health", "charge", "max_health", "max_charge", "lives", "score")] + [
        ("keys", C.c_uint16), ("weapon", C.c_uint16)]


def artifact(path):
    data = path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def run(command, **kwargs):
    result = subprocess.run(list(map(str, command)), cwd=ROOT, text=True, encoding="utf8", capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    wrapper = OUT / "ui-wrapper.c"
    wrapper.write_text('''#include "ui/hud.c"
#define EXPORT __declspec(dllexport)
EXPORT uint32_t n19_number(int16_t v,unsigned char out[7]) {return dok_ui_number(v,out);}
EXPORT uint32_t n19_paginate(const unsigned char *p,uint32_t n,uint32_t cols,uint32_t rows,uint32_t page,dok_ui_glyph_fn fn,void *c)
{return dok_ui_paginate(p,n,cols,rows,page,fn,c);}
EXPORT void n19_hud(unsigned char *out,const unsigned char *bank,uint32_t language,const dok_ui_values *v,const unsigned char *m,uint32_t n,uint32_t page)
{dok_ui_hud(out,bank,language,v,m,n,page);}
EXPORT void n19_glyph(unsigned char *out,const unsigned char *bank,unsigned char glyph,uint32_t height)
{text(out,bank,&glyph,1,7,5,height);}
''', encoding="ascii")
    zig = Path.home() / "AppData/Local/Programs/Zig/0.13.0/zig.exe"
    command = [zig, "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror", "-O2", "-shared",
               "-I", ROOT / "ports/nds/include", "-I", ROOT / "ports/nds", wrapper, "-o", OUT / "ui-host.dll"]
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT / "build/zig-global-cache"),
               ZIG_LOCAL_CACHE_DIR=str(ROOT / "build/zig-local-cache"))
    run(command, env=env)
    return command


class Driver:
    def __init__(self):
        self.dll = C.CDLL(str(OUT / "ui-host.dll"))
        self.dll.n19_number.argtypes, self.dll.n19_number.restype = [C.c_int16, P8], C.c_uint32
        self.dll.n19_paginate.argtypes = [P8, C.c_uint32, C.c_uint32, C.c_uint32, C.c_uint32, CALLBACK, C.c_void_p]
        self.dll.n19_paginate.restype = C.c_uint32
        self.dll.n19_hud.argtypes = [P8, P8, C.c_uint32, C.POINTER(Values), P8, C.c_uint32, C.c_uint32]
        self.dll.n19_glyph.argtypes = [P8, P8, U8, C.c_uint32]
        blob = (ROOT / "build/nds/n19/ui.dat").read_bytes()
        assert blob[:8] == b"N19UI01\0" and len(blob) == 16416
        self.banks = [(U8 * 8192).from_buffer_copy(blob[32 + 8192*i:32 + 8192*(i + 1)]) for i in range(2)]
        self.raw = (U8 * (9728 + 64))()
        self.output = C.cast(C.byref(self.raw, 32), P8)
        self.number_raw = (U8 * 23)()
        self.number_output = C.cast(C.byref(self.number_raw, 8), P8)
        self.draws = 0

    def number(self, value):
        C.memset(self.number_raw, 0xa5, 23)
        count = self.dll.n19_number(value, self.number_output)
        result = bytes(self.number_raw)
        expected = str(value).encode("ascii")
        assert count == len(expected) <= 6
        assert result[:8] == b"\xa5" * 8 and result[15:] == b"\xa5" * 8
        assert result[8:8 + count + 1] == expected + b"\0"
        assert result[8 + count + 1:15] == b"\xa5" * (6 - count)

    def hud(self, state, message=b"", page=0, fill=0xa5):
        if fill is not None:
            C.memset(self.raw, fill, len(self.raw))
        before = bytes(self.raw)
        values = Values(**{key: state[key] for key, _ in Values._fields_})
        raw_message = (U8 * max(1, len(message))).from_buffer_copy(message or b"\0")
        self.dll.n19_hud(self.output, self.banks[state["language"] - 1], state["language"], C.byref(values), raw_message, len(message), page)
        raw = bytes(self.raw)
        assert raw[:32] == before[:32] and raw[-32:] == before[-32:], "HUD wrote beyond 256x38"
        self.draws += 1
        return raw[32:-32]

    def pagination(self, raw, columns, rows, plan):
        data = (U8 * max(1, len(raw))).from_buffer_copy(raw or b"\0")
        for page, expected in enumerate(plan["pages"]):
            actual = []
            callback = CALLBACK(lambda _, offset, col, row: actual.append([offset, col, row]))
            count = self.dll.n19_paginate(data, len(raw), columns, rows, page, callback, None)
            assert count == len(plan["pages"]) and actual == expected["glyphs"], (raw, page, actual, expected)
        return len(plan["pages"])


class SweepOracle:
    """Row composition accelerated from independently read original fonts.

    Every source glyph is expanded directly from RES; no UI bank/C output is
    an input. Checked against render_hud before the complete signed WORD sweep.
    """
    def __init__(self, art, language):
        self.language = language
        self.glyph_rows = {}
        for height in (8, 16):
            source = art.resources[language][f"FONT{height}"]
            self.glyph_rows[height] = [[bytes(2 if bits & (128 >> col) else 86 for col in range(8))
                                       for bits in source[code*height:(code + 1)*height]] for code in range(256)]
        base = art.panel(language)
        art.icon(base, language, "ПЖИЗНЬ", 4, 3, 17, 14)
        art.icon(base, language, "ПМАГИЯ", 82, 2, 13, 16)
        art.text(base, language, ("О:" if language == 1 else "W:") + "2", 220, 2, 16)
        for name, x in (("ПКЛЮЧМ", 156), ("ПКЛЮЧЖ", 172), ("ПКЛЮЧЗ", 188)):
            art.icon(base, language, name, x, 3, 10, 14)
        self.base = bytes(base)

    def render(self, value):
        canvas = bytearray(self.base)
        digits = str(value).encode("ascii")
        font = self.glyph_rows[16]
        width = len(digits)*8
        for row in range(16):
            tile = b"".join(font[code][row] for code in digits)
            canvas[(2 + row)*256 + 23:(2 + row)*256 + 23 + width] = tile
            canvas[(2 + row)*256 + 97:(2 + row)*256 + 97 + width] = tile
        for x in (23, 97):
            extent = min(48, max(0, value)*48//100)
            canvas[18*256 + x:18*256 + x + 48] = b"\x02" * extent + b"\0" * (48 - extent)
        secondary = (b"\x8f:" if self.language == 1 else b"L:") + digits + b"  " + (b"\x91:" if self.language == 1 else b"S:") + digits
        font = self.glyph_rows[8]
        for row in range(8):
            line = b"".join(font[code][row] for code in secondary)
            canvas[(20 + row)*256 + 4:(20 + row)*256 + 4 + len(line)] = line
        return bytes(canvas)


def main():
    started = time.monotonic()
    source_names = (
        "ports/nds/ui/hud.c", "ports/nds/include/dok/ui.h", "ports/nds/tools/check_hud_host.py",
        "ports/nds/tools/hud_pixel_oracle.py", "ports/nds/tools/audit_assets.py", "ports/nds/tools/layout_ui.py",
        "ports/nds/tools/ui/extract_ui.py", "build/nds/n19/ui.dat", "research/evidence/nds/n19/ui-catalog.json")
    source_before = [artifact(ROOT / relative) for relative in source_names]
    command = build()
    driver, art = Driver(), OriginalHudArt()
    report = dict(task="N19", passed=False, architecture="Windows x86_64 host; same C as ARM9, not ARM9 execution",
                  command=list(map(str, command)), python=sys.version, platform=platform.platform(), counts={})
    base = dict(language=1, level=1, health=100, charge=150, max_health=100, max_charge=200, lives=3, score=0, keys=7, weapon=2)
    glyph_count = 0
    for language in (1, 2):
        for height in (8, 16):
            for glyph in range(256):
                C.memset(driver.raw, 0xa5, len(driver.raw))
                driver.dll.n19_glyph(driver.output, driver.banks[language - 1], glyph, height)
                expected = bytearray([0xa5]*9728)
                art.text(expected, language, bytes([glyph]), 7, 5, height)
                raw = bytes(driver.raw)
                assert raw[:32] == b"\xa5"*32 and raw[-32:] == b"\xa5"*32
                assert raw[32:-32] == expected, (language, height, glyph)
                glyph_count += 1
    report["counts"]["unsigned_cp866_glyphs"] = glyph_count
    catalog = json.loads((EVIDENCE / "ui-catalog.json").read_text("utf8"))
    message_pages = story_pages = 0
    frame_hash = hashlib.sha256()
    for entry in catalog["messages"]:
        message = bytes.fromhex(entry["cp866_hex"])
        message_pages += driver.pagination(message, 28, 1, entry["plan"])
        state = dict(base, language=entry["language"], level=entry.get("level", 1))
        for page in range(len(entry["plan"]["pages"])):
            expected, metadata = render_hud(art, state, message, page)
            actual = driver.hud(state, message, page)
            assert actual == expected, (entry.get("level"), entry.get("object_index"), entry.get("role"), page)
            frame_hash.update(actual)
    assert message_pages == 589
    for story in catalog["stories"]:
        story_pages += driver.pagination(bytes.fromhex(story["cp866_hex"]), 30, 9, story["plan"])
    report["counts"].update(catalog_messages=len(catalog["messages"]), message_pages=message_pages, story_pages=story_pages)
    synthetics = (b"", b"A\r\nB$ignored", b"A\rB\0ignored", b"A"*61, b" "*30 + b"B\n"*40,
                  bytes(range(128, 256))*10, b"A"*28 + b"\r\nB", b"\n\nA\n", b"A B C   ", bytes(range(1, 256)))
    synthetic_pages = 0
    for value in synthetics:
        for columns, rows in ((28, 1), (30, 9), (1, 1), (7, 3)):
            synthetic_pages += driver.pagination(value, columns, rows, paginate(value, columns, rows))
    report["counts"]["synthetic_pages"] = synthetic_pages
    layout_cases = 0
    for language in (1, 2):
        for keys in range(8):
            for weapon in range(9):
                for value in (-32768, -1, 0, 9, 100, 32767):
                    state = dict(base, language=language, keys=keys, weapon=weapon,
                                 health=value, charge=value, lives=value, score=value)
                    expected, metadata = render_hud(art, state)
                    assert driver.hud(state, fill=(value & 255)) == expected
                    frame_hash.update(expected)
                    layout_cases += 1
    report["counts"]["key_weapon_extreme_layout_cases"] = layout_cases
    # Different maxima exercise signed rejection, integer ratio and upper clamp.
    max_cases = 0
    for maximum in (-32768, -1, 0, 1, 49, 100, 200, 32767):
        for current in (-32768, -1, 0, 1, 49, 100, 32767):
            state = dict(base, health=current, charge=current, max_health=maximum, max_charge=maximum)
            assert driver.hud(state) == render_hud(art, state)[0]
            max_cases += 1
    report["counts"]["maximum_bar_cases"] = max_cases
    cleanup_cases = 0
    for language in (1, 2):
        for keys in range(8):
            for value in (32767, -32768, 100, 9, 0):
                state = dict(base, language=language, keys=keys, health=value, charge=value, lives=value, score=value)
                assert driver.hud(state, fill=None) == render_hud(art, state)[0]
                cleanup_cases += 1
    report["counts"]["retained_output_shrink_key_cleanup_cases"] = cleanup_cases
    anchors = []
    for language in (1, 2):
        for value in (0, 9, 100, -32768):
            state = dict(base, language=language, health=value, charge=value)
            expected, metadata = render_hud(art, state)
            assert driver.hud(state) == expected
            anchors.append(dict(language=language, value=value, cell_anchors=dict(health=23, charge=97),
                                original_ink_adjacency=metadata["adjacency"]))
    report["fixed_left_anchors"] = anchors
    rng = random.Random(190019)
    sweep = {}
    for language in (1, 2):
        oracle = SweepOracle(art, language)
        # The fast composition has an independent full-render comparison before
        # it serves as the exhaustive reference, including all signed bounds.
        examples = (-32768, -32767, -10000, -1, 0, 1, 9, 10, 99, 100, 32767,
                    *(rng.randrange(-32768, 32768) for _ in range(64)))
        for value in examples:
            state = dict(base, language=language, health=value, charge=value, lives=value, score=value, max_charge=100)
            assert oracle.render(value) == render_hud(art, state)[0], (language, value, "accelerated oracle")
        all_hash = hashlib.sha256()
        for value in range(-32768, 32768):
            if language == 1:
                driver.number(value)
            state = dict(base, language=language, health=value, charge=value, lives=value, score=value, max_charge=100)
            expected = oracle.render(value)
            actual = driver.hud(state, fill=value & 255)
            assert actual == expected, (language, value, "full signed16 raster")
            all_hash.update(actual)
        sweep[str(language)] = dict(cases=65536, fields=["health", "charge", "lives", "score"],
            all_bytes_per_case=9728, validated_bytes=65536*9728, raster_sha256=all_hash.hexdigest(),
            source_oracle_cross_checks=len(examples))
    report["signed16_sweep"] = sweep
    report["counts"].update(number_strings=65536, guarded_hud_renders=driver.draws)
    report["representative_raster_sha256"] = frame_hash.hexdigest()
    report["sources"] = [artifact(ROOT / relative) for relative in source_names]
    assert report["sources"] == source_before, "UI source/input changed during the check"
    report["artifacts"] = [artifact(OUT / name) for name in ("ui-wrapper.c", "ui-host.dll")]
    report["runtime_seconds"] = time.monotonic() - started
    report["passed"] = True
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    target = EVIDENCE / "host-ui-check.json"
    target.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    summary = {key: value for key, value in report.items() if key not in ("sources", "artifacts", "command", "python", "platform")}
    summary["report"] = artifact(target)
    (ROOT / "analysis/nds-n19-ui-host.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf8")
    print(json.dumps({key: summary[key] for key in ("task", "passed", "counts", "runtime_seconds", "report")}, indent=2))


if __name__ == "__main__":
    main()
