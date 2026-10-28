"""Source-art affine OBJ patch planner; no GBA CPU speed claims.

All geometry is in physical 240x144 scene pixels. A patch retains native palette
indices and uses one shared diagonal matrix for each constant-depth rectangle.
The native integration still needs front-to-back coverage and upload/cache work.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from dataclasses import dataclass
import hashlib
import struct

SHAPES = {(8, 8):(0, 0), (16, 16):(0, 1), (32, 32):(0, 2), (64, 64):(0, 3),
          (16, 8):(1, 0), (32, 8):(1, 1), (32, 16):(1, 2), (64, 32):(1, 3),
          (8, 16):(2, 0), (8, 32):(2, 1), (16, 32):(2, 2), (32, 64):(2, 3)}


def rounded(n, d):
    if d < 0:
        n, d = -n, -d
    return (n + d // 2) // d


@dataclass
class Patch:
    width: int
    height: int
    left: int
    top: int
    double: bool
    pa: int
    pd: int
    pixels: bytes  # row major, independent model input
    source_x: int
    source_y: int

    @property
    def canvas(self):
        scale = 2 if self.double else 1
        return self.width * scale, self.height * scale

    def sample(self, x, y):
        cw, ch = self.canvas
        x -= self.left
        y -= self.top
        if not (0 <= x < cw and 0 <= y < ch):
            return 0
        u = ((self.pa * (x - cw // 2)) >> 8) + self.width // 2
        v = ((self.pd * (y - ch // 2)) >> 8) + self.height // 2
        return self.pixels[v * self.width + u] if 0 <= u < self.width and 0 <= v < self.height else 0

    def tiled(self):
        return bytes(self.pixels[(ty + y) * self.width + tx + x]
                     for ty in range(0, self.height, 8) for tx in range(0, self.width, 8)
                     for y in range(8) for x in range(8))


def rectangle(image, box, uv=None, chunk=64):
    """Return source patches or a specific unsupported projection reason.

    The affine canvas clips enlargement beyond 2x. It is intentionally rejected,
    not silently counted as an implemented near-wall renderer.
    """
    w, h = image['width'], image['height']
    x0, y0, x1, y1 = box
    u0, u1, v0 = uv if uv is not None else (0, w - 1, 0)
    if x1 <= x0 or y1 <= y0 or u0 == u1:
        return [], 'degenerate'
    pa = rounded((u1 - u0) * 256, x1 - x0)
    pd = rounded((h - 1 - v0) * 256, y1 - y0)
    if not (128 <= abs(pa) <= 32767 and 128 <= pd <= 32767):
        return [], 'affine_canvas_scale_limit'
    patches = []
    raw = image['pixels']
    for sy in range(0, h, chunk):
        for sx in range(0, w, chunk):
            valid_w, valid_h = min(chunk, w - sx), min(chunk, h - sy)
            pw, ph = min((shape for shape in SHAPES if shape[0] >= valid_w and shape[1] >= valid_h),
                         key=lambda s:(s[0] * s[1], s))
            double = abs(pa) < 256 or pd < 256
            cw, ch = pw * (2 if double else 1), ph * (2 if double else 1)
            cx = x0 + rounded((sx + pw // 2 - u0) * 256, pa)
            cy = y0 + rounded((sy + ph // 2 - v0) * 256, pd)
            left, top = cx - cw // 2, cy - ch // 2
            if left >= 240 or left + cw <= 0 or top >= 144 or top + ch <= 0:
                continue
            # Do not include a crop region above the original face's v0. Keeping
            # it transparent makes hardware sampling and CPU crop agree.
            pixels = bytes(raw[(sy + y) * w + sx + x]
                           if x < valid_w and y < valid_h and sy + y >= v0 and
                           min(u0,u1) <= sx + x <= max(u0,u1) else 0
                           for y in range(ph) for x in range(pw))
            if any(pixels):
                patches.append(Patch(pw, ph, left, top, double, pa, pd, pixels, sx, sy))
    return patches, None


def pack(patches, vram_bytes=32768, obj_limit=128, matrix_limit=32,
         obj_first=0, matrix_first=0, tile_first=0):
    """Create real OAM and 1D 8bpp OBJ tiles; no guessed memory discount."""
    assert 0 <= obj_first <= obj_first + obj_limit <= 128
    assert 0 <= matrix_first <= matrix_first + matrix_limit <= 32
    assert 0 <= tile_first * 32 <= tile_first * 32 + vram_bytes <= 32768
    oam = bytearray(1024)
    for i in range(128):
        struct.pack_into('<H', oam, i * 8, 0x200)
    tiles, tile_cache, matrices = bytearray(), {}, {}
    if len(patches) > obj_limit:
        return None, 'oam_capacity'
    for index, p in enumerate(patches):
        matrix = (p.pa, p.pd)
        if matrix not in matrices:
            if len(matrices) == matrix_limit:
                return None, 'matrix_capacity'
            matrices[matrix] = matrix_first + len(matrices)
        mi = matrices[matrix]
        raw = p.tiled()
        if raw not in tile_cache:
            if len(tiles) + len(raw) > vram_bytes:
                return None, 'texture_capacity'
            tile_cache[raw] = tile_first + len(tiles) // 32
            tiles.extend(raw)
        shape, size = SHAPES[p.width, p.height]
        a = 0x2100 | (0x200 if p.double else 0) | shape << 14 | (p.top & 255)
        b = size << 14 | mi << 9 | (p.left & 511)
        struct.pack_into('<3H', oam, (obj_first + index) * 8, a, b, tile_cache[raw] | 0x400)
    for (pa, pd), i in matrices.items():
        for at, val in ((6, pa), (14, 0), (22, 0), (30, pd)):
            struct.pack_into('<h', oam, i * 32 + at, val)
    return dict(oam=bytes(oam), tiles=bytes(tiles), objects=len(patches),
                matrices=len(matrices), texture_bytes=len(tiles),
                tile_sha256=hashlib.sha256(tiles).hexdigest()), None


def render(patches):
    # Lower OAM slot wins, color zero is transparent. Deliberately separate
    # row-major patch sampling from the packed tile/OAM representation.
    return bytes(next((color for p in patches if (color := p.sample(x, y))), 0)
                 for y in range(160) for x in range(240))
