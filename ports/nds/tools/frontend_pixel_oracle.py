"""Independent N21 image/text primitives read directly from original RES.

This module never reads the prepared frontend bank. Its scale is the agreed
NDS 4:3 adaptation, rather than an assertion of DOS framebuffer equality.
"""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path

import audit_assets

ROOT = Path(__file__).resolve().parents[3]


class OriginalFrontendArt:
    def __init__(self):
        self.sources = audit_assets.originals()
        self.resources = {
            lang: {item["name"]: item["data"] for item in source["entries"]}
            for lang, source in self.sources.items()
        }

    def palette(self, language, name):
        raw = self.resources[language][name]
        assert len(raw) == 768 and max(raw) <= 63
        return [sum((raw[i * 3 + c] >> 1) << (5 * c) for c in range(3))
                for i in range(256)]

    def image(self, language, name, width=256, height=192):
        sw, sh, _, raw, _ = audit_assets.decode_image(self.resources[language][name])
        return bytes(raw[((2 * y + 1) * sh // (2 * height)) * sw
                         + (2 * x + 1) * sw // (2 * width)]
                     for y in range(height) for x in range(width))

    def tiled(self, language, name="FON", width=256, height=192):
        sw, sh, _, raw, _ = audit_assets.decode_image(self.resources[language][name])
        return bytearray(raw[(y % sh) * sw + x % sw]
                         for y in range(height) for x in range(width))

    def text(self, canvas, language, value, x, y, color=2, height=8):
        raw = value.encode("cp866") if isinstance(value, str) else value
        font = self.resources[language][f"FONT{height}"]
        assert 0 <= x <= 256 - len(raw) * 8 and 0 <= y <= 192 - height
        for i, code in enumerate(raw):
            for row in range(height):
                bits = font[code * height + row]
                for col in range(8):
                    if bits & (128 >> col):
                        canvas[(y + row) * 256 + x + i * 8 + col] = color

    def original_text(self, language, name):
        raw = self.resources[language][name]
        stop = next((i for i, b in enumerate(raw) if b in (0, 36)), len(raw))
        return raw[:stop]

    def background(self, language, background_index):
        names = ("ЗАСТАВКА_СПР", "BSPR1", "ESPR1", "BSPR2", "ESPR2",
                 "BSPR3", "ESPR3", "FON")
        name = names[background_index]
        pixels = self.tiled(language) if background_index == 7 else self.image(language, name)
        palette = "ЗАСТАВКА_ПАЛ" if background_index == 0 else (
            "МЕНЮ_ПАЛ" if background_index == 7 else f"GAMEP{name[-1]}")
        return bytes(pixels), self.palette(language, palette)

    def popal_gpu(self, language):
        raw = self.resources[language]["POPAL"]
        count = struct.unpack_from("<H", raw)[0]
        assert count == 6
        cursor = 8 + count * 10
        for frame in range(count):
            width, height, _, pixels, cursor = audit_assets.decode_image(raw, cursor)
            if frame == 5:
                assert (width, height) == (158, 70)
                out = bytearray(256 * 128)
                for row in range(height):
                    out[row * 256:row * 256 + width] = pixels[row * width:(row + 1) * width]
        assert cursor == len(raw)
        return bytes(out)


def text_pages(raw, columns, rows):
    """Wrap complete CP866 source, retaining spaces and explicit blank lines.

    Source CR/LF, NUL and DOS '$' are controls. Printable bytes are never
    dropped; even trailing spaces survive wrapping. A long word is split.
    """
    assert columns > 0 and rows > 0
    stop = next((i for i, b in enumerate(raw) if b in (0, 36)), len(raw))
    source = raw[:stop].replace(b"\r", b"")
    lines = []
    for logical in source.split(b"\n"):
        while len(logical) > columns:
            split = logical.rfind(b" ", 0, columns + 1)
            # Include the separator in the previous line where it fits.
            take = split + 1 if 0 < split < columns else columns
            lines.append(logical[:take])
            logical = logical[take:]
        lines.append(logical)
    # A final line-feed moves the cursor but contains no following line of
    # text. Do not invent a wholly empty page after a full final source row.
    if source.endswith(b"\n"):
        lines.pop()
    pages = [lines[i:i + rows] for i in range(0, len(lines), rows)] or [[b""]]
    assert b"".join(line for page in pages for line in page) == source.replace(b"\n", b"")
    return pages


def digest(pixels):
    return hashlib.sha256(pixels).hexdigest()


def level_names(language):
    """Original immutable name spans from the accepted Ghidra NE exports."""
    label, segment, offsets = (("ru", "1190", (0x191, 0x1a0, 0x1af))
                               if language == 1 else
                               ("en", "10d0", (0x192, 0x1a1, 0x1ae)))
    data = (ROOT / "research/evidence/nds/n19/original" / label / f"{segment}.bin").read_bytes()
    return [data[offset:data.index(0, offset)] for offset in offsets]


def render_frontend(art, labels, controls, state, language, selection, level, page, fixtures):
    """Render the agreed NDS layout using original images, fonts and text.

    labels/controls are the reviewed NDS interface copy, not original DOS menu
    claims. Original level names and story bytes are independently sourced.
    """
    assert state in (1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12)
    lang = language - 1
    text_index = {6: 0, 7: 2 * level - 1, 10: 2 * level, 11: 7}.get(state)
    background = (0 if state in (1, 6) else text_index if state in (7, 10) else 7)
    top, top_palette = art.background(language, background)
    bottom = bytearray(49152)
    copy = labels[lang]

    def label(value, x=8, y=8, color=2, height=8):
        art.text(bottom, language, value, x, y, color, height)

    def menu_line(value, row):
        y = 28 + 24 * row
        color = 2
        if row == selection:
            for r in range(y, y + 20):
                bottom[r * 256 + 8:r * 256 + 248] = bytes([2]) * 240
            color = 0
            label(">", 16, y + 4, color)
        label(value, 32, y + 4, color)

    message = b""
    pages = None
    if state == 1:
        label(copy[0])
        label("A / Start", 88, 88, 2)
    elif state in (2, 9):
        label(copy[1 if state == 2 else 7])
        first, count = (2, 5) if state == 2 else (8, 4)
        for row in range(count):
            menu_line(copy[first + row], row)
    elif state == 3:
        label(copy[12])
        for row, name in enumerate(level_names(language)):
            menu_line(name, row)
    elif state == 4:
        label(copy[3])
        for row in range(2):
            menu_line(labels[row][13], row)
    elif state == 5:
        label(copy[4])
        for row, value in enumerate(controls[lang]):
            label(value, 8, 24 + 12 * row, 2)
        if fixtures:
            label("Fixture: Sel+Start/X/Y", 8, 160, 2)
    elif state == 12:
        label(copy[6])
        label(copy[16], 8, 80, 2)
    else:
        name = ("README", "BINTRO1", "EINTRO1", "BINTRO2", "EINTRO2",
                "BINTRO3", "EINTRO3", "ENDTXT")[text_index]
        message = art.resources[language][name]
        pages = text_pages(message, 30, 9)
        assert 0 <= page < len(pages)
        label(copy[5 if state == 6 else 17 if state == 7 else 18 if state == 10 else 19])
        label(str(page + 1), 208, 8)
        label("/", 224, 8)
        label(str(len(pages)), 232, 8)
        for row, line in enumerate(pages[page]):
            label(line, 8, 24 + 16 * row, 2, 16)
    if state not in (1, 12):
        label(copy[15 if text_index is not None else 14], 8, 176, 2)
    return dict(top=top, bottom=bytes(bottom), top_palette=top_palette,
                bottom_palette=art.palette(language, "МЕНЮ_ПАЛ"), background=background,
                text_index=text_index, message=message, pages=len(pages) if pages else 1)
