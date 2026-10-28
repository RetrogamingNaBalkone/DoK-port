"""Native BSP fragment ownership, visibility leases and mutable-map fallback."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
from collections import Counter
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha
from verify_gba_static_cache import Workspace
from verify_items_gba import MAP_OFFSET
from verify_sprites_gba import load_json
from verify_renderer_gba import stack_usage


def signed(n):return (n+2147483648)%4294967296-2147483648


def exercise(build,core,trial):
    p=GameProbe(core,build);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        def snapshot(name,expect_active=True):
            for _ in range(6):d=p.present()
            assert d['phase']==2 and d['guard_ok']==1
            address=p.symbols['g_render_frame']
            lease=p.frontend.read(address+Workspace.image_known.offset,256)+p.frontend.read(address+Workspace.surface_known.offset,768)
            row=dict(name=name,lease_sha256=hashlib.sha256(lease).hexdigest())
            if trial:
                work=list(p.words('g_stream_work',8));assert work[0]==int(expect_active) and work[7]==0,(name,work)
                row['work']=work
                if expect_active:
                    xy=p.words('stream_xy',1)[0];heads=p.words('stream_heads',1)[0];parts=p.words('stream_fragments',1)[0]
                    assert xy%4==heads%4==parts%4==0
                    count=work[5]
                    raw=p.frontend.read(parts,count*8)
                    fragments=[struct.unpack_from('<HHBBH',raw,i*8) for i in range(count)]
                    w=Workspace.from_buffer_copy(p.frontend.read(address,ct.sizeof(Workspace)))
                    view=d['level']*2+d['language']
                    nodes,leaves=struct.unpack('<2H',p.frontend.read(p.symbols['dok_stream_views']+view*24+20,4))
                    head_values=struct.unpack('<'+'H'*(nodes+leaves),p.frontend.read(heads,(nodes+leaves)*2))
                    seen=set();columns=[Counter() for _ in range(w.face_count)]
                    for head in head_values:
                        while head!=65535:
                            assert head<count and head not in seen,(name,'cycle/duplicate',head)
                            seen.add(head);face,head,first,last,_=fragments[head]
                            assert face<w.face_count and first<=last<240
                            columns[face].update(range((first+1)&~1,last+1,2))
                    assert len(seen)==count
                    for i,face in enumerate(w.faces[:w.face_count]):
                        x0,x1=signed(face.n[0]),signed(face.n[1])
                        first,last=max(0,min(x0,x1)),min(239,max(x0,x1))
                        expected=Counter(range((first+1)&~1,last+1,2))
                        assert columns[i]==expected,(name,i,'screen-column ownership',dict(columns[i]-expected),dict(expected-columns[i]))
                    row.update(dynamic_faces=w.face_count,fragments=count,ownership_columns=sum(sum(c.values()) for c in columns))
            p.frontend.run(2);row['ppu_sha256']=hashlib.sha256(p.frontend.last_video_raw).hexdigest()
            image=build/f'stream-qa-{name}.png';p.frontend.screenshot(image)
            row['image']=str(image);row['image_sha256']=sha(image)
            p.present();rows.append(row)
        for language in (0,1):
            p.language(language)
            for level in (1,2,3):
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level}-runtime.json');hero=model['objects'][model['player_index']-1]['actor']
                cases=[(hero['x']//480*480+240,hero['z']//480*480+240,h) for h in (0,45,90,135)]
                heavy={1:[(520,225),(3640,225)],2:[(1592,135),(2871,225)],3:[(1544,315),(1560,90)]}[level]
                cases.extend((c%64*480+240,c//64*480+240,h) for c,h in heavy)
                for x,z,h in cases:
                    p.camera(level-1,x,-300,z,h);snapshot(f'{language}-L{level}-{x}-{z}-{h}')
                # A real mutable-map edit must choose the complete legacy path,
                # rather than silently using stale compiled static geometry.
                cell=hero['z']//480*64+hero['x']//480
                tmap=p.symbols['g_render_world']+MAP_OFFSET+4096
                old=p.frontend.read(tmap+cell,1)
                replacement=32 if old!=b' ' else next(o['glyph'] for o in model['objects'] if o['class']==2)
                p.write(tmap+cell,bytes([replacement]));snapshot(f'{language}-L{level}-tmap-edit',False)
                p.write(tmap+cell,old);snapshot(f'{language}-L{level}-tmap-restored')
        return dict(rom_sha256=sha(p.rom),rows=rows,stack=stack_usage(p.frontend))
    finally:p.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--reference-build',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before=exercise(a.reference_build,core,False);after=exercise(a.build_dir,core,True)
    assert len(before['rows'])==len(after['rows'])
    for b,t in zip(before['rows'],after['rows']):
        assert b['name']==t['name'] and b['lease_sha256']==t['lease_sha256'],('visibility lease changed',b['name'])
        t['reference_ppu_equal']=b['ppu_sha256']==t['ppu_sha256']
        if 'tmap-edit' in t['name']:assert t['reference_ppu_equal'],('fallback pixels changed',t['name'])
    result=dict(task='G20A',status='passed',core_sha256=sha(core),script_sha256=sha(__file__),before=before,after=after,
        scope='Native fragment ownership/alignment, visibility leases, guards, TMap fallback exact PPU. Does not prove all pixel occlusion, FCMap mutation semantics, full gameplay, frame transitions, or FPS acceptance.')
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Passed',len(after['rows']),'native views/mutations; dynamic column ownership, leases and fallback checked.')


if __name__=='__main__':main()
