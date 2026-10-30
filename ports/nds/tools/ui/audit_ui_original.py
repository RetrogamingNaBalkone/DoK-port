"""Independent N19 execution of original glyph/string/copy/countdown slices.

Actual x86 font-indexing, signed clipping, control-byte handling, 38-byte
copy/padding and signed DWORD countdown execute through the original RETF.
Only virtual pixel output is intercepted for glyphs; only glyph drawing is
intercepted for the separate string control-flow test. No GBA imports.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "ports/nds/tools"))
from audit_world import originals
from extract_ui import EVIDENCE, load_exports, sha, save_json


class Slice:
    def __init__(self, language):
        self.ru = language == 1
        self.directory = EVIDENCE / "original" / ("ru" if self.ru else "en")
        self.font_segment = 0x1058 if self.ru else 0x1018
        self.hud_segment = 0x10c0 if self.ru else 0x1050
        self.glyph_entry = 0x158 if self.ru else 0x153e
        self.string_entry = 0x2ac if self.ru else 0x1692
        self.c = Uc(UC_ARCH_X86, UC_MODE_16)
        self.c.mem_map(0, 0x100000)
        self.mode, self.pixels, self.glyphs = "pixels", [], []
        self.c.hook_add(UC_HOOK_CODE, self.hook)

    def far_return(self):
        c = self.c
        sp = c.reg_read(R.UC_X86_REG_SP)
        ip, cs = struct.unpack("<HH", c.mem_read(0x30000 + sp, 4))
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def hook(self, c, address, size, context):
        cs, ip = c.reg_read(R.UC_X86_REG_CS), c.reg_read(R.UC_X86_REG_IP)
        if cs == 0x6000:
            sp = c.reg_read(R.UC_X86_REG_SP)
            off, seg, x, y, color = struct.unpack("<HHiiH", c.mem_read(0x30000 + sp + 4, 14))
            assert (off, seg) == (0, 0x4000)
            self.pixels.append((x, y, color & 255))
            self.far_return()
        elif self.mode == "string" and cs == self.font_segment and ip == self.glyph_entry:
            sp = c.reg_read(R.UC_X86_REG_SP)
            off, seg, x, y, glyph, mode = struct.unpack("<HHiiHH", c.mem_read(0x30000 + sp + 4, 16))
            assert (off, seg, mode) == (0, 0x4000, 1)
            self.glyphs.append((x, y, glyph & 255))
            self.far_return()

    def load(self, segment):
        self.c.mem_write(segment * 16, (self.directory / f"{segment:04x}.bin").read_bytes())

    def run(self, segment, entry, arguments):
        c = self.c
        self.pixels.clear()
        self.glyphs.clear()
        for reg, value in ((R.UC_X86_REG_CS, segment), (R.UC_X86_REG_SS, 0x3000),
                           (R.UC_X86_REG_DS, 0x3000), (R.UC_X86_REG_SP, 0xf000), (R.UC_X86_REG_BP, 0xf800)):
            c.reg_write(reg, value)
        c.mem_write(0x3f000, struct.pack("<HH", 0xfff0, 0x7000) + arguments)
        c.emu_start(segment * 16 + entry, 0x7fff0, count=250000)
        assert (c.reg_read(R.UC_X86_REG_CS), c.reg_read(R.UC_X86_REG_IP), c.reg_read(R.UC_X86_REG_SP)) == (0x7000, 0xfff0, 0xf004)

    def object(self, height=8, spacing=8):
        obj = bytearray(0x100)
        struct.pack_into("<H", obj, 0, 0x1000)
        struct.pack_into("<HH", obj, 0x1a, 0, 0x5000)
        obj[0x1e], obj[0x20] = 231, 17
        struct.pack_into("<hh", obj, 0x22, spacing, height)
        struct.pack_into("<iiii", obj, 0x38, 0, 0, 255, 191)
        self.c.mem_write(0x40000, bytes(obj))


def main():
    archives = originals()
    report = dict(task="N19", passed=False, whole_DOS_session=False, languages={},
                  scope="original glyph, string control flow, fixed message copy and nonexpiring countdown; no external video hardware")
    for language in (1, 2):
        _, source = load_exports(language)
        n = Slice(language)
        n.load(n.font_segment)
        resources = {bytes.fromhex(e["name"]).decode("cp866"): e for e in archives[language]["entries"]}
        data = (n.directory / ("1190.bin" if n.ru else "10d0.bin")).read_bytes()
        mask_offset = 0x4d4 if n.ru else 0x4dc
        assert struct.unpack_from("<8H", data, mask_offset) == (128, 64, 32, 16, 8, 4, 2, 1)
        n.c.mem_write(0x30000 + mask_offset, data[mask_offset:mask_offset + 16])
        n.c.mem_write(0x31004, struct.pack("<HH", 0, 0x6000))
        glyph_vectors = 0
        for height in (8, 16):
            font = resources[f"FONT{height}"]["payload"]
            n.c.mem_write(0x50000, font)
            n.object(height)
            for glyph in range(256):
                for x, y, mode in ((3, 4, 1), (-3, 188, 0), (254, -4, 1), (-300, 12, 0)):
                    n.run(n.font_segment, n.glyph_entry, struct.pack("<HHiiHH", 0, 0x4000, x, y, glyph, mode))
                    expected = []
                    for row in range(height):
                        for col in range(8):
                            foreground = font[glyph * height + row] & (128 >> col)
                            if (foreground or mode != 1) and 0 <= x + col <= 255 and 0 <= y + row <= 191:
                                expected.append((x + col, y + row, 231 if foreground else 17))
                    assert n.pixels == expected, (language, height, glyph, x, y, mode)
                    glyph_vectors += 1
        n.mode = "string"
        fixtures = (b"", b"ABC", b"A\r\nB", b"A\rB", b"A$ignored", b"A\0ignored", b"\n\nA",
                    bytes(range(128, 256)), b" A B ", bytes(c for c in range(1, 256) if c not in (10, 13, 36)))
        string_vectors = 0
        for height in (8, 16):
            for spacing in (8, 5, -2):
                n.object(height, spacing)
                for text in fixtures:
                    n.c.mem_write(0x52000, text + b"\0")
                    n.run(n.font_segment, n.string_entry, struct.pack("<HHiiHHH", 0, 0x4000, -7, 13, 0x2000, 0x5000, 1))
                    x, y, expected = -7, 13, []
                    for code in text:
                        if code in (0, 36):
                            break
                        if code == 10:
                            y += height
                            x = -7
                        elif code != 13:
                            expected.append((x, y, code))
                            x += spacing
                    assert n.glyphs == expected, (language, height, spacing, text)
                    string_vectors += 1
        n.mode = "pixels"
        n.load(n.hud_segment)
        copy_vectors = 0
        for length in (0, 1, 27, 28, 29, 36, 38, 39, 240):
            for terminate in (b"\0", b"$Z\0"):
                text = bytes(65 + i % 26 for i in range(length))
                full = text + terminate
                n.c.mem_write(0x40000, b"\xa5" * 256)
                n.c.mem_write(0x50000, full)
                n.run(n.hud_segment, 0x1278 if n.ru else 0x15db, struct.pack("<4H", 0, 0x4000, 0, 0x5000))
                result = bytes(n.c.mem_read(0x40000, 256))
                wanted = full.split(b"\0", 1)[0][:38].ljust(38, b" ")
                assert result[0x2c:0x52] == wanted and result[0x52] == 0
                expected = bytearray(b"\xa5" * 256)
                struct.pack_into("<H", expected, 0x10, 0xa5a5 | 0x22)
                expected[0x2c:0x52], expected[0x52] = wanted, 0
                assert result == bytes(expected)
                copy_vectors += 1
        countdown_vectors = 0
        for counter in (-2147483648, -2, -1, 0, 2, 3, 2147483647):
            n.c.mem_write(0x40000, bytes(256))
            n.c.mem_write(0x4008d, struct.pack("<i", counter))
            n.run(n.hud_segment, 0x12d0 if n.ru else 0x1633, struct.pack("<HH", 0, 0x4000))
            after = struct.unpack("<i", n.c.mem_read(0x4008d, 4))[0]
            assert after == (counter - 1 if counter >= 0 else counter)
            countdown_vectors += 1
        if sha((ROOT / source["sample"]).read_bytes()) != source["sha256_before"]:
            raise AssertionError("Original changed during slice execution")
        report["languages"]["ru" if n.ru else "en"] = dict(source=source, glyph_vectors=glyph_vectors,
            string_vectors=string_vectors, copy_vectors=copy_vectors, countdown_vectors=countdown_vectors,
            intercepted="virtual pixel callback for glyph cases; glyph far entry for string cases; none for copy/countdown")
    report["unicorn_version"] = __import__("unicorn").__version__
    report["script_sha256"] = sha(Path(__file__).read_bytes())
    report["passed"] = True
    save_json(EVIDENCE / "original-ui-check.json", report, EVIDENCE)
    summary = dict(task="N19", passed=True, whole_DOS_session=False, unicorn_version=report["unicorn_version"],
                   script_sha256=report["script_sha256"],
                   report=dict(path="research/evidence/nds/n19/original-ui-check.json",
                               sha256=sha((EVIDENCE / "original-ui-check.json").read_bytes())),
                   counts={language: {key: value for key, value in item.items() if key.endswith("_vectors")}
                           for language, item in report["languages"].items()})
    save_json(ROOT / "analysis/nds-n19-ui-original.json", summary, ROOT / "analysis")
    print(json.dumps({key: value for key, value in report.items() if key != "languages"}, ensure_ascii=False))


if __name__ == "__main__":
    main()
