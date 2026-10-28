"""CPU-geometry bounds and rectangular sectors for a near-first clip renderer."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import struct
import hashlib
from compile_gba_render_scene import compile_view
from gba_stream_scene import runtime_scene_tables


def sector_scene_tables(out,data):
    source=runtime_scene_tables(out,data)
    assembly=[];summaries=[]
    for view,summary in enumerate(source):
        u16=lambda at:struct.unpack_from('<H',data,at)[0]
        u32=lambda at:struct.unpack_from('<I',data,at)[0]
        rec=u32(28)+view*80;defs=u32(rec+36);fields=u32(rec+40)
        links={tuple(struct.unpack_from('<2H',data,u32(rec+72)+i*8)):u16(u32(rec+72)+i*8+4) for i in range(u16(rec+76))}
        roof_ids=set()
        for obj,slot in links:
            if slot!=86:continue
            at=defs+obj*32
            if any(u16(fields+i*8)==84 and struct.unpack_from('<i',data,fields+i*8+4)[0]==400
                for i in range(u16(at+14),u16(at+14)+u16(at+16))):roof_ids.add(links[obj,slot])
        assert len(roof_ids)==1,(view,'one shared roof material',roof_ids)
        roof_id=next(iter(roof_ids));entry=u32(24)+(roof_id-1)*32+16+(view%2)*8
        roof_sha=hashlib.sha256(data[u32(entry):u32(entry)+u32(entry+4)]).hexdigest()
        for a in compile_view(data,view)['source_pieces']:
            if a['kind']!='rim':continue
            entry=u32(24)+(a['texture']-1)*32+16+(view%2)*8
            # Side surfaces use the side image's stored flags; sprite_flags
            # belong to the floor/water animation, not this surface image.
            assert not(u16(u32(entry)+4)&8),(view,'opaque rim',a)
        nodes=bytearray((out/f'stream-{view}-nodes.bin').read_bytes())
        leaves=(out/f'stream-{view}-leaves.bin').read_bytes()
        atoms=(out/f'stream-{view}-atoms.bin').read_bytes()
        ids=struct.unpack('<'+'H'*(len((out/f'stream-{view}-ids.bin').read_bytes())//2),(out/f'stream-{view}-ids.bin').read_bytes())
        low_leaves=0
        def union(bounds):
            valid=[b for b in bounds if b is not None]
            return [min(b[0] for b in valid),min(b[1] for b in valid),max(b[2] for b in valid),max(b[3] for b in valid)] if valid else None
        def visit(index):
            nonlocal low_leaves
            if index<0:
                first,count,x0,z0,x1,z1,height,_=struct.unpack_from('<6HhH',leaves,(-index-1)*16)
                if not count:return None,False
                assert height==-561 and count*480*480==(x1-x0)*(z1-z0),(view,index,count)
                low_leaves+=1;return [x0,z0,x1,z1],True
            fixed,axis,_,a,b,first,count,*_=struct.unpack_from('<HBB2h2H4H',nodes,index*20)
            ba,la=visit(a);bb,lb=visit(b);bounds=[ba,bb]
            for k in ids[first:first+count]:
                ax,az,bx,bz,y,height,v0,obj,cell,kind,side=struct.unpack_from('<4H3h2HBB',atoms,k*20)
                if kind and height<=10:continue
                bounds.append([min(ax,bx),min(az,bz),max(ax,bx),max(az,bz)])
            bound=union(bounds);has_low=la or lb
            struct.pack_into('<HBB2h2H4H',nodes,index*20,fixed,axis,int(has_low),a,b,first,count,*(bound or [65535,65535,0,0]))
            return bound,has_low
        visit(0)
        for name,payload in [('nodes',nodes),('leaves',leaves),('atoms',atoms),
            ('ids',(out/f'stream-{view}-ids.bin').read_bytes()),('cells',(out/f'stream-{view}-cells.bin').read_bytes())]:
            file=out/f'sector-{view}-{name}.bin';file.write_bytes(payload)
            symbol=f'dok_sector_{view}_{name}'
            assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'{symbol}:',f'.incbin "{file.resolve().as_posix()}"']
        summaries.append(dict(summary,low_rectangular_leaves=low_leaves,shared_roof_resource=roof_id,
            shared_roof_payload_sha256=roof_sha,all_source_rims_opaque=True))
    assembly += ['.section .rodata.dok_sector_views,"a",%progbits','.balign 4','.global dok_sector_views','dok_sector_views:']
    for view,row in enumerate(summaries):
        assembly += ['.word '+','.join(f'dok_sector_{view}_{name}' for name in ('nodes','leaves','atoms','ids','cells')),
                     f'.short {row["nodes"]},{row["leaves"]}']
    # Bitmap alternative shares the same reduced32x32 /3-light-band materials.
    # Only the cell lookup is compiled; animated phases remain runtime-owned.
    u16=lambda at:struct.unpack_from('<H',data,at)[0]
    u32=lambda at:struct.unpack_from('<I',data,at)[0]
    mapping=(out/'hw-bg-maps.bin').read_bytes();cells=bytearray()
    for view in range(6):
        rec=u32(28)+view*80;maps=u32(rec+48);defs=u32(rec+36);glyphs=u32(rec+32)
        for plane in (0,1):
            for cell in range(4096):
                obj=u16(glyphs+data[maps+4096+cell]*2)-1
                cells.append(255 if u16(defs+obj*32)==2 else mapping[(view*2+plane)*256+data[maps+8192+cell]])
    file=out/'sector-material-cells.bin';file.write_bytes(cells)
    assembly += ['.section .rodata.dok_sector_material_cells,"a",%progbits','.balign 4',
        '.global dok_sector_material_cells','dok_sector_material_cells:',f'.incbin "{file.resolve().as_posix()}"']
    (out/'sector-tables.S').write_text('\n'.join(assembly)+'\n',encoding='utf-8')
    return summaries
