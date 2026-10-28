"""Compile original-map render boundaries into a segment BSP, independently of gameplay.

This is the data/feasibility stage of a new renderer, not a runtime FPS result.
Coordinates describe the current GBA renderer policy; no EXE addresses are used.
The old uniform 8191-node cell tree is not reused. No external engine code is copied.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def word(n):
    return (n + 32768) % 65536 - 32768


def rectangles(cells):
    """Disjoint rectangles with identical material and height, retaining every cell."""
    remaining = dict(cells)
    result = []
    while remaining:
        cell = min(remaining)
        z, x = divmod(cell, 64)
        value = remaining[cell]
        right = x + 1
        while right < 64 and remaining.get(z * 64 + right) == value:
            right += 1
        bottom = z + 1
        while bottom < 64 and all(remaining.get(bottom * 64 + c) == value for c in range(x, right)):
            bottom += 1
        for r in range(z, bottom):
            for c in range(x, right):
                del remaining[r * 64 + c]
        result.append(dict(x=x, z=z, width=right-x, length=bottom-z, material=value))
    # An independent expansion checks both material identity and exact cell ownership.
    expanded = {}
    for rec in result:
        for z in range(rec['z'], rec['z'] + rec['length']):
            for x in range(rec['x'], rec['x'] + rec['width']):
                cell = z * 64 + x
                assert cell not in expanded
                expanded[cell] = rec['material']
    assert expanded == cells
    return result


def merge_segments(atoms):
    groups = defaultdict(list)
    for index, atom in enumerate(atoms):
        groups[tuple(atom[k] for k in ('axis', 'fixed', 'direction', 'kind', 'object', 'y', 'height', 'v0'))].append(index)
    result = []
    for indices in groups.values():
        for index in sorted(indices, key=lambda i: atoms[i]['lo']):
            a = atoms[index]
            # Keep source pieces explicitly: merging visibility geometry is lossless,
            # but flattening their UV periods into one affine span would not be exact.
            if result and result[-1]['group'] == tuple(a[k] for k in ('axis', 'fixed', 'direction', 'kind', 'object', 'y', 'height', 'v0')) and result[-1]['hi'] == a['lo']:
                result[-1]['hi'] = a['hi']
                result[-1]['pieces'].append(index)
            else:
                result.append(dict(group=tuple(a[k] for k in ('axis', 'fixed', 'direction', 'kind', 'object', 'y', 'height', 'v0')),
                                   axis=a['axis'], fixed=a['fixed'], lo=a['lo'], hi=a['hi'], pieces=[index]))
    expanded = [i for seg in result for i in seg['pieces']]
    assert sorted(expanded) == list(range(len(atoms)))
    for seg in result:
        assert sum(atoms[i]['hi']-atoms[i]['lo'] for i in seg['pieces']) == seg['hi']-seg['lo']
        del seg['group']
    return result


def segment_bsp(segments):
    """Axis-aligned geometry partition planes; split crossing segments exactly."""
    nodes, pieces = [], []
    fragments = [dict(segment=i, lo=s['lo'], hi=s['hi']) for i, s in enumerate(segments)]
    max_depth = 0

    def build(items, depth):
        nonlocal max_depth
        if not items:
            return -1
        max_depth = max(max_depth, depth)
        candidates = sorted({(segments[p['segment']]['axis'], segments[p['segment']]['fixed']) for p in items})
        # Bound compiler work. Every selected plane is still an actual segment plane.
        stride = max(1, len(candidates)//48)
        def score(plane):
            a, b = 0, 0
            for p in items:
                seg = segments[p['segment']]
                if seg['axis'] == plane[0]:
                    a += seg['fixed'] < plane[1]
                    b += seg['fixed'] > plane[1]
                else:
                    a += p['lo'] < plane[1]
                    b += p['hi'] > plane[1]
            split = a+b-sum(not (segments[p['segment']]['axis'] == plane[0] and segments[p['segment']]['fixed'] == plane[1]) for p in items)
            return max(a, b)*4 + split*3, abs(a-b), plane
        axis, fixed = min(candidates[::stride], key=score)
        left, right, coplanar = [], [], []
        for p in items:
            seg = segments[p['segment']]
            if seg['axis'] == axis:
                (left if seg['fixed'] < fixed else right if seg['fixed'] > fixed else coplanar).append(p)
            elif p['hi'] <= fixed:
                left.append(p)
            elif p['lo'] >= fixed:
                right.append(p)
            else:
                left.append(dict(p, hi=fixed))
                right.append(dict(p, lo=fixed))
        assert coplanar
        node = len(nodes)
        nodes.append(None)
        begin = len(pieces)
        pieces.extend(coplanar)
        # Bounding box encloses every fragment, including both child subtrees.
        xs, zs = [], []
        for p in items:
            seg = segments[p['segment']]
            if seg['axis'] == 0:  # fixed X, varying Z
                xs.append(seg['fixed']); zs.extend((p['lo'], p['hi']))
            else:
                zs.append(seg['fixed']); xs.extend((p['lo'], p['hi']))
        nodes[node] = dict(axis=axis, fixed=fixed, first=begin, count=len(coplanar),
                           children=[build(left, depth+1), build(right, depth+1)],
                           bounds=[min(xs), min(zs), max(xs), max(zs)])
        return node

    root = build(fragments, 1)
    # Verify partition output covers each input segment exactly once, without holes.
    ranges = defaultdict(list)
    for p in pieces:
        ranges[p['segment']].append((p['lo'], p['hi']))
    for i, seg in enumerate(segments):
        end = seg['lo']
        for lo, hi in sorted(ranges[i]):
            assert lo == end and hi > lo, (i, end, lo, hi)
            end = hi
        assert end == seg['hi']
    return dict(root=root, nodes=nodes, fragments=pieces, max_depth=max_depth)


def compile_view(data, view):
    u16 = lambda off: struct.unpack_from('<H', data, off)[0]
    i16 = lambda off: struct.unpack_from('<h', data, off)[0]
    u32 = lambda off: struct.unpack_from('<I', data, off)[0]
    rec = u32(28) + view*80
    defs, fields, maps, glyphs = (u32(rec+k) for k in (36, 40, 48, 32))
    links = {struct.unpack_from('<HH', data, u32(rec+72)+i*8): u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}
    def object_at(plane, x, z):
        return u16(glyphs+data[maps+plane*4096+max(0,min(63,z))*64+max(0,min(63,x))]*2)-1
    def inset(obj):
        d = defs+obj*32
        for j in range(u16(d+14), u16(d+14)+u16(d+16)):
            if u16(fields+j*8) == 0x54:
                return struct.unpack_from('<i', data, fields+j*8+4)[0]
        raise AssertionError(('missing inset', obj))
    def image(obj, slot):
        rid = links[obj, slot]
        at = u32(24)+(rid-1)*32+16+(view%2)*8
        start, size = u32(at), u32(at+4)
        w, h = struct.unpack_from('<HH', data, start)
        assert size == 18+w*h
        return w, h, u16(start+4), rid
    classes = [u16(defs+object_at(1, c%64, c//64)*32) for c in range(4096)]
    atoms, ceiling_cells, wall_profiles, dynamic = [], {}, Counter(), Counter()
    offsets = ((0,-1),(-1,0),(0,1),(1,0))
    def edge(x, z, width, side, **properties):
        ax, az, bx, bz = x*480, z*480, x*480, z*480
        if side == 0: bx += width
        elif side == 1: az += width
        elif side == 2: ax += width; az += width; bz += width
        else: ax += width; bx += width; bz += width
        axis = int(az == bz)  # 0 fixed X;1 fixed Z
        a, b = (az,bz) if axis == 0 else (ax,bx)
        atoms.append(dict(axis=axis, fixed=ax if axis == 0 else az, lo=min(a,b), hi=max(a,b),
                          direction=int(b>a), cell=z*64+x, side=side, **properties))
    for cell, cls in enumerate(classes):
        z, x = divmod(cell, 64)
        obj, fc = object_at(1,x,z), object_at(2,x,z)
        value = inset(fc)
        if cls != 2:
            ceiling_cells[cell] = (fc, value, i16(defs+fc*32+8))
        if cls in (4,7):
            dynamic[cls] += 1
        if cls == 2:
            w,h,flags,rid = image(obj,0xfff0)
            scale = i16(defs+obj*32+6)
            width, height = word(w*scale), word(h*scale)
            assert width > 0 and height > 0
            wall_profiles[(width,height,bool((flags|u16(defs+obj*32+4))&8))] += 1
            for side,(dx,dz) in enumerate(offsets):
                if u16(defs+object_at(1,x+dx,z+dz)*32) == 2:
                    continue
                crop = inset(object_at(2,x+dx,z+dz))
                wall_h = word(height-crop+1)
                if wall_h > 0:
                    edge(x,z,width,side,kind='wall',object=obj,y=word(-i16(defs+fc*32+8)),height=wall_h,
                         v0=word((h*crop)//960 if crop>=0 else -((-h*crop)//960)),
                         u0=0,u1=w-1,texture=rid,texture_width=w,texture_height=h)
        if value > 0:
            for side,(dx,dz) in enumerate(offsets):
                if inset(object_at(2,x+dx,z+dz)) < value:
                    w,h,flags,rid = image(fc,0x5e)
                    edge(x,z,480,side,kind='rim',object=fc,y=-960+value,height=value,
                         v0=word(h*value//960),u0=0,u1=w-1,texture=rid,texture_width=w,texture_height=h)
    merged = merge_segments(atoms)
    patches = rectangles(ceiling_cells)
    tree = segment_bsp(merged)
    summary = dict(view=view,level=view//2+1,language=view%2,wall_cells=classes.count(2),
                   source_segments=len(atoms),merged_segments=len(merged),
                   segment_kinds=dict(Counter(a['kind'] for a in atoms)),
                   ceiling_floor_cells=len(ceiling_cells),material_rectangles=len(patches),
                   lowered_cells=sum(v[1]>10 for v in ceiling_cells.values()),
                   dynamic_panels=dict(dynamic),wall_profiles=[dict(width=k[0],height=k[1],masked=k[2],cells=v) for k,v in wall_profiles.items()],
                   bsp_nodes=len(tree['nodes']),bsp_fragments=len(tree['fragments']),bsp_max_depth=tree['max_depth'],
                   old_cell_bsp_nodes=8191)
    return dict(summary=summary,segments=merged,source_pieces=atoms,patches=patches,bsp=tree)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,default=ROOT/'build/gba/assets/assets.bin')
    p.add_argument('--out',type=Path,required=True)
    args = p.parse_args()
    data = args.assets.read_bytes()
    args.out.mkdir(parents=True,exist_ok=True)
    rows = []
    for view in range(6):
        compiled = compile_view(data,view)
        payload = (json.dumps(compiled,separators=(',',':'))+'\n').encode('utf-8')
        path = args.out/f'view-{view}-scene.json'
        path.write_bytes(payload)
        rows.append(dict(compiled['summary'],compiled_sha256=digest(payload),compiled_bytes=len(payload)))
        print(json.dumps(compiled['summary']),flush=True)
    report = dict(package_sha256=digest(data),script_sha256=digest(Path(__file__).read_bytes()),views=rows,
                  checks=['exact disjoint cell/material reconstruction','source segment ownership','BSP fragment coverage without gaps/overlap'],
                  scope='Offline static render representation feasibility only. No runtime renderer, FPS, visibility equivalence, map mutations, or gameplay acceptance claimed.',
                  preserved=['all emitted static wall boundaries','all emitted lowered-ceiling rims','all non-wall floor/ceiling cell materials and heights','per-cell UV source pieces'],
                  pending=['native GBA traversal/visibility/raster','dynamic doors/actors integration','mutable-map invalidation','world-space versus native rounded image comparison'])
    (args.out/'scene-compile-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__ == '__main__':
    main()
