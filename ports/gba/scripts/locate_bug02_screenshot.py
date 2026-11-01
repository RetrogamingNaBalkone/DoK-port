"""Rank original-map wall contours against the supplied, centered BUG02 map.

Read-only PNG analysis, no original EXE execution. Assumes unmodified 4x RGB(A)
capture and centered automap; ranks evidence, does not recover subcell pose.
"""
import collections
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[3]


def pixels(path):
    data = path.read_bytes()
    pos, compressed = 8, b''
    while pos < len(data):
        n = struct.unpack_from('>I', data, pos)[0]
        kind, block = data[pos+4:pos+8], data[pos+8:pos+8+n]
        pos += n+12
        if kind == b'IHDR':
            w, h, depth, colour, compression, filtering, interlace = struct.unpack('>IIBBBBB', block)
            assert w % 240 == 0 and h == w*2//3
            assert (depth, compression, filtering, interlace) == (8, 0, 0, 0)
        if kind == b'IDAT':
            compressed += block
    channels = {2: 3, 6: 4}[colour]
    stride = w*channels
    raw, rows, previous = zlib.decompress(compressed), [], bytearray(stride)
    for y in range(h):
        f = raw[y*(stride+1)]
        row = bytearray(raw[y*(stride+1)+1:(y+1)*(stride+1)])
        for x in range(stride):
            a = row[x-channels] if x >= channels else 0
            b = previous[x]
            c = previous[x-channels] if x >= channels else 0
            q = a+b-c
            distances = [abs(q-a), abs(q-b), abs(q-c)]
            paeth = [a, b, c][distances.index(min(distances))]
            row[x] = (row[x]+[0, a, b, (a+b)//2, paeth][f]) % 256
        rows.append(row)
        previous = row
    scale = w//240
    return {(x, y): tuple(rows[y*scale][x*scale*channels:x*scale*channels+3])
            for y in range(144) for x in range(240)}


def main():
    image = pixels(ROOT/'research/evidence/gba/bug02-20261101/mGBA_UvoiNjh6hE.png')
    colours = collections.Counter(image.values())
    wall = colours.most_common(2)[1][0]
    points = [xy for xy, rgb in image.items() if rgb == wall]
    print('wall RGB:', wall, 'pixels:', len(points))
    print('white marker:', [xy for xy, rgb in image.items() if min(rgb) > 240])
    for level in (1, 2, 3):
        model = json.loads((ROOT/f'research/evidence/runtime/ru/LEVEL{level}-runtime.json').read_text())
        lookup = {o['glyph']: o['class'] for o in model['objects']}
        plane = bytes.fromhex(model['maps'][1])

        def cls(x, z):
            return lookup.get(plane[z*64+x], 0) if 0 <= x < 64 and 0 <= z < 64 else 0

        edges = set()
        for z in range(64):
            for x in range(64):
                if cls(x, z) != 2:
                    continue
                sx, sy = (63-x)*3, z*3
                for dx, dz, ax, ay, bx, by in ((-1, 0, 3, 0, 3, 3), (1, 0, 0, 0, 0, 3),
                                              (0, -1, 0, 0, 3, 0), (0, 1, 0, 3, 3, 3)):
                    if cls(x+dx, z+dz) != 2:
                        edges.update((sx+xx, sy+yy) for xx in range(ax, bx+1) for yy in range(ay, by+1))
        scores = []
        for pz in range(64):
            for px in range(64):
                ox, oy = 118-(63-px)*3, 70-pz*3
                score = sum((x-ox, y-oy) in edges for x, y in points)
                scores.append((score, px, pz))
        print('LEVEL', level, 'best (matched pixels, cell X, cell Z):', sorted(scores, reverse=True)[:5])


if __name__ == '__main__':
    main()
