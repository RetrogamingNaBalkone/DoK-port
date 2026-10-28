"""Pack the segment compiler's geometry for the native streaming BSP trial."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from collections import Counter
import struct
from compile_gba_render_scene import compile_view


def runtime_scene_tables(out, data):
    assembly, summaries = [], []
    for view in range(6):
        source = compile_view(data, view)
        nodes = source['bsp']['nodes']
        fragments = source['bsp']['fragments']
        segments = source['segments']
        atoms = source['source_pieces']
        atom_ids, packed_nodes, leaves, cells = [], [], [], []
        by_leaf = {}
        material_by_cell = {}
        for patch in source['patches']:
            for z in range(patch['z'], patch['z']+patch['length']):
                for x in range(patch['x'], patch['x']+patch['width']):
                    material_by_cell[z*64+x] = patch['material']

        def visit(node, bounds):
            if node < 0:
                index = len(leaves)
                selected = [c for c in material_by_cell if bounds[0] <= c%64*480+240 < bounds[2] and bounds[1] <= c//64*480+240 < bounds[3]]
                for c in selected:
                    assert c not in by_leaf
                    by_leaf[c] = index
                heights = {material_by_cell[c][1] for c in selected}
                assert len(heights) <= 1, (view, bounds, heights)
                # Base planes stay on BG2; include only actual CPU lowered ceilings.
                low = sorted(c for c in selected if material_by_cell[c][1] > 10)
                ceiling = next(iter(heights), 10)-961
                leaves.append(struct.pack('<HH4HhH', len(cells), len(low), *bounds, ceiling, 0))
                cells.extend(low)
                return -index-1
            n = nodes[node]
            first = len(atom_ids)
            for f in fragments[n['first']:n['first']+n['count']]:
                seg = segments[f['segment']]
                for i in seg['pieces']:
                    a = atoms[i]
                    if a['hi'] <= f['lo'] or a['lo'] >= f['hi']:
                        continue
                    # Every current static boundary lies on the480 grid. BSP splits
                    # occur at cell endpoints, so exact per-cell UV pieces survive.
                    assert f['lo'] <= a['lo'] < a['hi'] <= f['hi']
                    atom_ids.append(i)
            count = len(atom_ids)-first
            left, right = list(bounds), list(bounds)
            left[n['axis']+2] = n['fixed']
            right[n['axis']] = n['fixed']
            children = [visit(n['children'][0], left), visit(n['children'][1], right)]
            packed_nodes[node] = struct.pack('<HBB2h2H4H', n['fixed'], n['axis'], 0, *children, first, count, *bounds)
            return node

        packed_nodes = [None]*len(nodes)
        visit(0, [0,0,30720,30720])
        assert set(by_leaf) == set(material_by_cell)
        assert Counter(atom_ids) == Counter(range(len(atoms)))
        assert len(nodes)+len(leaves) <= 2048
        packed_atoms = []
        for a in atoms:
            if a['axis'] == 0:
                ax,bx,az,bz = a['fixed'],a['fixed'],a['lo'],a['hi']
            else:
                az,bz,ax,bx = a['fixed'],a['fixed'],a['lo'],a['hi']
            if not a['direction']:
                ax,bx,az,bz = bx,ax,bz,az
            packed_atoms.append(struct.pack('<4H3h2HBB',ax,az,bx,bz,a['y'],a['height'],a['v0'],a['object'],a['cell'],int(a['kind']=='rim'),a['side']))
        for name, payload in [('nodes',b''.join(packed_nodes)),('leaves',b''.join(leaves)),('atoms',b''.join(packed_atoms)),
                              ('ids',struct.pack('<'+'H'*len(atom_ids),*atom_ids)),('cells',struct.pack('<'+'H'*len(cells),*cells))]:
            path=out/f'stream-{view}-{name}.bin'
            path.write_bytes(payload)
            symbol=f'dok_stream_{view}_{name}'
            assembly.extend([f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'{symbol}:',f'.incbin "{path.resolve().as_posix()}"'])
        summaries.append(dict(view=view,nodes=len(nodes),leaves=len(leaves),atoms=len(atoms),low_ceiling_cells=len(cells),
                              packed_bytes=sum((len(nodes)*20,len(leaves)*16,len(atoms)*20,len(atom_ids)*2,len(cells)*2))))
    assembly.extend(['.section .rodata.dok_stream_views,"a",%progbits','.balign 4','.global dok_stream_views','dok_stream_views:'])
    for row in summaries:
        v=row['view']
        assembly.extend(['.word '+','.join(f'dok_stream_{v}_{name}' for name in ('nodes','leaves','atoms','ids','cells')),
                         f'.short {row["nodes"]},{row["leaves"]}'])
    path=out/'stream-tables.S'
    path.write_text('\n'.join(assembly)+'\n',encoding='utf-8')
    return summaries
