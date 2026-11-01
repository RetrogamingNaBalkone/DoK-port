"""Live A-button midrange fights on original LEVEL1/3 maps, both weapons/locales.

Choose unobstructed original bats using a rational initial-line oracle. Only
hero pose, HP/armor and equip/ammo are fixtures. AI, clock, maps and rendering
run normally; this is not a navigation playthrough or physical GBA test.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from profile_game_gba import GameProbe,freeze,collect
from verify_combat_gba import actor_pose
from verify_centered_projectiles_gba import expected_hit
from verify_gba import sha


def scenes(model,trig):
    definitions=model['objects'];glyphs=model['glyph_index']
    blocked=[]
    for cell,glyph in enumerate(bytes.fromhex(model['maps'][1])):
        token=glyphs[glyph]
        if not token:
            continue
        d=definitions[token-1];cls,flags=d['class'],d['flags']
        if (cls==7 and not flags&8) or (cls!=7 and not flags&0x18):
            blocked.append(dict(name='wall',cell=[cell%64,cell//64]))
    blocked_cells={tuple(w['cell']) for w in blocked}
    targets=[dict(id=i,position=[o['actor']['x'],o['actor']['z']]) for i,o in enumerate(definitions)
             if o['class']==9 and o['placements'] and 'actor' in o]
    result=[]
    for target in targets:
        if not definitions[target['id']]['animation'].startswith('8c9b989c'):
            continue
        tx,tz=target['position']
        for distance in (960,1440):
            found=None
            for heading in (45,135,225,315,90,0,180,270):
                x=tx+round(math.sin(math.radians(heading))*distance)
                z=tz-round(math.cos(math.radians(heading))*distance)
                if not (240<=x<30480 and 240<=z<30480) or (x//480,z//480) in blocked_cells:
                    continue
                vector=dict(hero=[x,z],heading=heading,weapon=1,ticks=416,targets=targets,walls=blocked)
                if expected_hit(vector,trig)==target['id']:
                    found=dict(target=target['id'],hero=[x,z],heading=heading,distance=distance,target_start=[tx,tz])
                    break
            if found:
                result.append(found)
        if len(result)>=4:
            return result[:4]
    raise AssertionError('Fewer than four unobstructed original bat scenes')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    trig=list(map(int,re.findall(r'-?\d+',(ROOT/'ports/gba/src/renderer_trig.inc').read_text().split('{',1)[1])))
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,args.build_dir);rows=[]
    try:
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for language in (0,1):
            p.language(language)
            for level in (0,2):
                model=json.loads((ROOT/f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json').read_text())
                for scene in scenes(model,trig):
                    for weapon in (1,2):
                        p.camera((level+1)%3,15120,-300,22800,90)
                        p.camera(level,*[scene['hero'][0],-300,scene['hero'][1]],scene['heading'])
                        actor_pose(p,model,model['player_index']-1,*scene['hero'],scene['heading'])
                        p.control(1,weapon,1000)
                        player=p.player();player[0]=player[2]=32767;p.set_player(player)
                        health_address=p.symbols['g_render_world']+4+scene['target']*36+16
                        initial=struct.unpack('<h',p.frontend.read(health_address,2))[0]
                        assert initial==24
                        p.write(p.symbols['test_camera'],b'\0');freeze(p,False)
                        history=[]
                        target_address=p.symbols['g_render_world']+9220+model['objects'][scene['target']]['actor_index']*64
                        for frame in range(45):
                            timing=collect(p,1,buttons=(8,))
                            hp,state=struct.unpack('<hh',p.frontend.read(health_address,4))
                            combat=p.combat_diag()
                            position=struct.unpack('<iii',p.frontend.read(target_address+4,12))
                            history.append(dict(frame=frame,health=hp,state=state,shots=combat['shots'],hits=combat['hits'],position=position))
                            assert combat['error']==0 and p.ai_diag()['error']==0 and timing['samples'][0]['flow_state']==2
                            # This fixture holds the initial aim. A live bat can
                            # subsequently leave that ray or hide behind walls;
                            # killing it without re-aiming is not a valid gate.
                            if hp<initial:
                                break
                        assert hp<initial,(language,level,scene,weapon,history)
                        rows.append(dict(language=language,level=level+1,weapon=weapon,**scene,history=history))
                        freeze(p,True);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
    finally:
        p.close()
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(dict(status='passed',rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),
        script_sha256=sha(__file__),core_sha256=sha(core),cases=len(rows),rows=rows,scope=__doc__),indent=2)+'\n',encoding='utf-8')
    print('Live midrange bats:',len(rows),'cases inflicted damage under normal AI/clock')


if __name__=='__main__':main()
