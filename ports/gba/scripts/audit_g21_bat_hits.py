"""Both player weapons at original LEVEL1/3 bats across angles, ranges and cell edges.

Controlled open-map/actor poses isolate collision from navigation and AI.
No physical GBA or exact user route is claimed. Rational grid oracle and
Ghidra-exported x86 cell division are independent of production C lookup.
"""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from profile_game_gba import GameProbe,freeze
from verify_combat_gba import actor_pose
from verify_items_gba import MAP_OFFSET,baseline
from verify_projectile_cells_gba import trajectory
from verify_gba import sha
from verify_combat_original import Original,R
from verify_movement import Original as NativeMotion
from verify_centered_projectiles_gba import actor_contact,path_segments


def native_cell(original,x,z):
    c=original.c
    c.reg_write(R.UC_X86_REG_SS,0x3000)
    c.reg_write(R.UC_X86_REG_DS,0x3000)
    c.reg_write(R.UC_X86_REG_ES,0x4000)
    c.reg_write(R.UC_X86_REG_BP,0xf800)
    c.mem_write(0x3f806,struct.pack('<HH',0,0x4000))
    c.mem_write(0x40026,struct.pack('<iii',x,-250,z))
    c.mem_write(0x30000+(0x266a if original.ru else 0x271a),struct.pack('<HH',0,0x7000))
    c.mem_write(0x70000,struct.pack('<H',64))
    c.mem_write(0x70004,struct.pack('<H',64))
    c.mem_write(0x70738,struct.pack('<h',480))
    c.mem_write(0x70742,struct.pack('<I',64))
    start,end=(0x299c,0x2a25) if original.ru else (0x303f,0x30c8)
    c.reg_write(R.UC_X86_REG_CS,original.actor)
    c.reg_write(R.UC_X86_REG_BX,0)
    c.emu_start(original.actor*16+start,original.actor*16+end,count=150)
    return struct.unpack('<h',c.mem_read(0x3f7fc,2))[0]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    ap.add_argument('--centered',action='store_true',help='Expect the G21 actor-centered player-shot correction')
    ap.add_argument('--weapons',type=int,nargs='+',choices=(1,2),default=[1,2])
    args=ap.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    trig=list(map(int,re.findall(r'-?\d+',(ROOT/'ports/gba/src/renderer_trig.inc').read_text().split('{',1)[1])))
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,args.build_dir)
    rows=[]
    sources={}
    native_vectors=[]
    native_paths=[]
    try:
        p.until(lambda d:d['draws']>=1)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2)
        freeze(p,True)
        for language in (0,1):
            lang='ru' if language==0 else 'en'
            original=Original(lang)
            sample=Path('originals/pkr')/('RUS/PKR.EXE' if language==0 else 'ENG/PKE.EXE')
            manifest=json.loads((original.path/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
            sources[str(sample)]=sha(sample)
            assert sha(sample)==manifest['sha256_before']==manifest['sha256_after']
            for x,z in ((14400,14400),(14399,14401),(14401,14399),(14879,14879)):
                observed=native_cell(original,x,z)
                assert observed==z//480*64+x//480,(lang,x,z,observed)
                native_vectors.append(dict(language=lang,x=x,z=z,cell=observed))
            motion=NativeMotion(lang)
            for heading in (30,45,60,90,135,225,315):
                data=bytearray(0xde)
                x,z=15079,13721
                struct.pack_into('<iii',data,0x26,x,-250,z)
                struct.pack_into('<h',data,0x32,heading)
                path=[]
                for _ in range(25):
                    displaced=motion.displace(data,-120,0)
                    x+=trig[heading]*-120//32768
                    z-=trig[361+heading]*-120//32768
                    observed=struct.unpack('<iii',displaced)
                    assert observed==(x,-250,z),(lang,heading,observed,x,z)
                    data[0x26:0x32]=displaced
                    path.append([x,z])
                native_paths.append(dict(language=lang,heading=heading,endpoints=path))
            p.language(language)
            for level in (0,2):
                model=json.loads((ROOT/f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json').read_text())
                bats=[i for i,o in enumerate(model['objects']) if o['class']==9 and o['animation'].startswith('8c9b989c') and o['placements']]
                assert bats
                p.camera(level,17000,-300,14000,90)
                world=p.symbols['g_render_world']
                p.control(6)
                p.write(world+MAP_OFFSET,b' '*12288)
                for obj in model['objects']:
                    if obj['actor_index']!=65535:
                        p.write(world+9220+obj['actor_index']*64+4,struct.pack('<iii',240,-300,240))
                # Two original bats per level plus a non-bat control.
                other=next(i for i,o in enumerate(model['objects']) if o['class']==9 and i not in bats and o['placements'])
                for target in [*bats[:2],other]:
                    for weapon in args.weapons:
                        for heading in (0,30,45,60,90,135,180,225,270,315):
                            for distance in (960,1920,2880):
                                for offset in (0,1,239,479):
                                    tx,tz=14400+offset,14400+offset
                                    x=tx+round(math.sin(math.radians(heading))*distance)
                                    z=tz-round(math.cos(math.radians(heading))*distance)
                                    p.control(6)
                                    p.write(world+MAP_OFFSET,b' '*4096)
                                    # Other test targets must leave the ray between cases.
                                    for i in [*bats[:2],other]:
                                        p.write(world+9220+model['objects'][i]['actor_index']*64+4,struct.pack('<iii',240,-300,240))
                                    actor_pose(p,model,model['player_index']-1,x,z,heading)
                                    actor_pose(p,model,target,tx,tz)
                                    health=world+4+target*36+16
                                    p.write(health,struct.pack('<hh',100,1))
                                    p.set_player(baseline())
                                    p.restart()
                                    p.control(1,weapon,100)
                                    p.control(3)
                                    slot,initial=p.active_slots()[0]
                                    d=p.poll(initial['deadline'])
                                    hp=struct.unpack('<h',p.frontend.read(health,2))[0]
                                    cells,_=trajectory(x,z,heading,weapon,256,trig)
                                    grid_expected=(tx//480,tz//480) in cells
                                    centered_expected=any(actor_contact(a,b,(tx,tz)) is not None
                                        for a,b in path_segments(x,z,heading,weapon,416,trig))
                                    expected=centered_expected if args.centered else grid_expected
                                    assert (hp<100)==expected,(language,level,target,weapon,heading,distance,offset,hp)
                                    rows.append(dict(language=language,level=level+1,target=target,weapon=weapon,
                                        bat=target in bats,heading=heading,distance=distance,offset=offset,
                                        hero=[x,z],target_position=[tx,tz],health_after=hp,
                                        oracle_hit=expected,grid_hit=grid_expected,centered_hit=centered_expected,
                                        hits=d['hits'],misses=d['misses'],target_cell=[tx//480,tz//480],ray_cells=cells))
        for path,value in sources.items():
            assert sha(path)==value
    finally:
        p.close()
    counts=Counter('hit' if r['oracle_hit'] else 'clear' for r in rows)
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(dict(status='passed',scope=__doc__,script_sha256=sha(__file__),
        rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),core_sha256=sha(core),sources=sources,
        counts=counts,centered_policy=args.centered,weapons=args.weapons,
        native_cell_vectors=native_vectors,native_movement_paths=native_paths,
        injected_native_boundary='Native world movement uses the validated G07 trig table; collision callback permits motion.',
        rows=rows),indent=2)+'\n',encoding='utf-8')
    print('Player weapon angle/range/cell edges:',len(rows),dict(counts))


if __name__=='__main__':
    main()
