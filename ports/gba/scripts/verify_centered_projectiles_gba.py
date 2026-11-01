"""Independent rational oracle for player shots at actor-centered footprints.

Partition continuous paths at geometric boundaries and sample open intervals;
no C slab algorithm or Q16 contact approximation is reused. Original empty-map
fixtures isolate walls, doors, competing targets, cell edges and catch-up.
"""
import argparse
from fractions import Fraction
import json
import os
from pathlib import Path
import random
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from profile_game_gba import GameProbe,freeze
from verify_combat_gba import setup_open,actor_pose
from verify_items_gba import MAP_OFFSET
from verify_gba import sha


def path_segments(x,z,heading,weapon,ticks,trig):
    elapsed=max(256,min(2048,ticks*256//416))
    distance=-(3000 if weapon==1 else 3200)*elapsed//256
    whole=(-distance)//120
    remainder=distance+whole*120
    r=(x+trig[heading]*remainder//32768,z-trig[361+heading]*remainder//32768)
    q=(trig[heading]*-120//32768,-(trig[361+heading]*-120//32768))
    return [((x,z),r),(r,(r[0]+whole*q[0],r[1]+whole*q[1]))]


def inside(point,box):
    x,z=point;left,top,right,bottom=box
    return left<=x<right and top<=z<bottom


def point_at(start,end,t):
    return tuple(Fraction(a)+(b-a)*t for a,b in zip(start,end))


def actor_contact(start,end,position,side=480):
    left,top=(v-side//2 for v in position)
    box=(left,top,left+side,top+side)
    if inside(start,box):
        return Fraction(0),True
    times={Fraction(0),Fraction(1)}
    for a,b,bounds in zip(start,end,((box[0],box[2]),(box[1],box[3]))):
        if a!=b:
            times.update(t for bound in bounds if 0<(t:=Fraction(bound-a,b-a))<1)
    times=sorted(times)
    for a,b in zip(times,times[1:]):
        if inside(point_at(start,end,(a+b)/2),box):
            return a,inside(point_at(start,end,a),box)
    if inside(end,box):
        return Fraction(1),True
    return None


def terrain_contact(start,end,blocked,side=480):
    def cell(point):
        return tuple(int(v//side) for v in point)
    def obstruction(point):
        cx,cz=cell(point)
        return 'outside' if not (0<=cx<64 and 0<=cz<64) else blocked.get((cx,cz))
    initial=obstruction(start)
    if initial is not None:
        return Fraction(0),True,initial
    times={Fraction(0),Fraction(1)}
    for a,b in zip(start,end):
        if a!=b:
            for line in range(min(a,b)//side*side,max(a,b)+side,side):
                t=Fraction(line-a,b-a)
                if 0<t<1:
                    times.add(t)
    times=sorted(times)
    for a,b in zip(times,times[1:]):
        middle=point_at(start,end,(a+b)/2)
        hit=obstruction(middle)
        if hit is not None:
            return a,cell(point_at(start,end,a))==cell(middle),hit
    last=obstruction(end)
    return (Fraction(1),True,last) if last is not None else None


def expected_hit(vector,trig):
    blocked={tuple(w['cell']):w['name'] for w in vector.get('walls',[]) if not w.get('open')}
    for start,end in path_segments(*vector['hero'],vector['heading'],vector['weapon'],vector['ticks'],trig):
        candidates=[]
        for target in vector['targets']:
            if target.get('dead') or target.get('removed'):
                continue
            x,z=target['position']
            if not (0<=x<30720 and 0<=z<30720):
                continue
            contact=actor_contact(start,end,target['position'])
            if contact is not None:
                candidates.append((*contact,target['id']))
        actor=min(candidates,key=lambda v:(v[0],not v[1],v[2])) if candidates else None
        wall=terrain_contact(start,end,blocked)
        if actor is not None and (wall is None or actor[0]<wall[0] or (actor[0]==wall[0] and actor[1])):
            return actor[2]
        if wall is not None:
            return wall[2]
    return None


def cases(model):
    targets=[i for i,o in enumerate(model['objects']) if o['class']==9 and o['placements']][:3]
    a,b,c=targets
    def target(id,x,z,**extra):
        return dict(id=id,position=[x,z],**extra)
    vectors=[]
    def add(name,hero,heading,ts,walls=(),weapon=1,ticks=416):
        vectors.append(dict(name=name,hero=hero,heading=heading,targets=ts,walls=list(walls),weapon=weapon,ticks=ticks))
    # actor IDs deliberately disagree with spatial order.
    add('near-target-before-low-id',[17000,15000],90,[target(a,14400,15000),target(b,15600,15000)])
    add('overlap-keeps-id-order',[17000,15000],90,[target(b,15600,15000),target(a,15600,15000)])
    add('dead-front',[17000,15000],90,[target(a,15600,15000,dead=True),target(b,14400,15000)])
    add('removed-front',[17000,15000],90,[target(a,15600,15000,removed=True),target(b,14400,15000)])
    add('moving-within-same-grid-cell',[17000,15000],90,[target(a,14401,14401)])
    for weapon in (1,2):
        for ticks in (416,832,3328):
            for name,cell,opened in (('wall-before',[32,31],False),('wall-after',[29,31],False),
                                     ('door-closed',[32,31],False),('door-open',[32,31],True)):
                add(name,[17000,15000],90,[target(a,14400,15000)],
                    [dict(name=name,cell=cell,door=name.startswith('door'),open=opened)],weapon,ticks)
            add('no-target-before-map-exit',[150,150],90,[],weapon=weapon,ticks=ticks)
            add('actor-before-map-exit',[1100,240],90,[target(a,240,240)],weapon=weapon,ticks=ticks)
            for dz in (-241,-240,-239,239,240,241):
                add('edge-graze',[17000,14400+dz],90,[target(a,14400,14400)],weapon=weapon,ticks=ticks)
    rng=random.Random(210101)
    for n in range(180):
        ts=[target(id,14000+rng.randrange(5000),13000+rng.randrange(5000),
                   dead=n%17==0 and id==a,removed=n%19==0 and id==b) for id in targets]
        walls=[] if n%3 else [dict(name='wall',cell=[30+rng.randrange(10),28+rng.randrange(10)])]
        add(f'random-{n}',[15000+rng.randrange(2000),14000+rng.randrange(2000)],rng.randrange(360),ts,walls,
            weapon=1+n%2,ticks=(416,832,3328)[n%3])
    return vectors


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    trig=list(map(int,re.findall(r'-?\d+',(ROOT/'ports/gba/src/renderer_trig.inc').read_text().split('{',1)[1])))
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    model=json.loads(Path('research/evidence/runtime/ru/LEVEL1-runtime.json').read_text())
    vectors=cases(model);p=GameProbe(core,args.build_dir);rows=[]
    try:
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        wall=next(o for o in model['objects'] if o['class']==2)
        door_id,door=next((i,o) for i,o in enumerate(model['objects']) if o['class']==7)
        world=p.symbols['g_render_world']
        for language in (0,1):
            p.language(language)
            for vector in vectors:
                setup_open(p,model)
                for id,definition in enumerate(model['objects']):
                    if definition['class']==9:
                        p.write(world+4+id*36,struct.pack('<H',definition['flags']|0x400))
                actor_pose(p,model,model['player_index']-1,*vector['hero'],vector['heading'])
                for target in vector['targets']:
                    id=target['id'];x,z=target['position']
                    actor_pose(p,model,id,x,z)
                    p.write(world+4+id*36+16,struct.pack('<hh',0 if target.get('dead') else 100,1))
                    p.write(world+4+id*36,struct.pack('<H',0x400 if target.get('removed') else model['objects'][id]['flags']))
                for obstruction in vector['walls']:
                    x,z=obstruction['cell'];glyph=door['glyph'] if obstruction.get('door') else wall['glyph']
                    p.write(world+MAP_OFFSET+4096+z*64+x,bytes([glyph]))
                    if obstruction.get('door'):
                        p.write(world+4+door_id*36,struct.pack('<H',door['flags']|8 if obstruction.get('open') else door['flags']&~8))
                p.control(1,vector['weapon'],100);p.control(3)
                _,initial=p.active_slots()[0];d=p.poll(initial['last']+vector['ticks'])
                damaged=[t['id'] for t in vector['targets'] if not t.get('dead') and struct.unpack('<h',p.frontend.read(world+4+t['id']*36+16,2))[0]<100]
                expected=expected_hit(vector,trig)
                if isinstance(expected,int):
                    assert damaged==[expected] and d['hits']==1 and not d['misses'],(language,vector,expected,damaged,d)
                else:
                    assert not damaged and not d['hits'] and bool(d['misses'])==(expected is not None),(language,vector,expected,damaged,d)
                rows.append(dict(language=language,**vector,expected=expected,damaged=damaged,hits=d['hits'],misses=d['misses']))
            # Candidate IDs may be cached for a poll; health/flags must remain
            # live after its first impact. A second simultaneous shot continues
            # through the newly dead front actor to the next target.
            for weapon,damage in ((1,14),(2,4)):
                setup_open(p,model)
                near,far=[i for i,o in enumerate(model['objects']) if o['class']==9 and o['placements']][:2]
                hero=model['player_index']-1
                actor_pose(p,model,hero,17000,15000,90)
                actor_pose(p,model,near,15600,15000)
                actor_pose(p,model,far,14400,15000)
                for id,hp in ((near,damage),(far,100)):
                    p.write(world+4+id*36,struct.pack('<H',model['objects'][id]['flags']))
                    p.write(world+4+id*36+16,struct.pack('<hh',hp,1))
                p.restart()
                for _ in range(2):
                    assert p.control(5,hero,ord('0')+weapon)['result']==damage
                _,initial=p.active_slots()[0];d=p.poll(initial['deadline'])
                health=[struct.unpack('<h',p.frontend.read(world+4+id*36+16,2))[0] for id in (near,far)]
                assert health==[0,100-damage] and d['hits']==2 and not d['misses'],(language,weapon,health,d)
                rows.append(dict(language=language,name='volley-live-health',weapon=weapon,targets=[near,far],health=health,hits=d['hits']))
    finally:
        p.close()
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(dict(status='passed',rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),
        script_sha256=sha(__file__),core_sha256=sha(core),cases=len(rows),rows=rows,scope=__doc__),indent=2)+'\n',encoding='utf-8')
    print('Centered player-shot oracle:',len(rows),'cases passed')


if __name__=='__main__':main()
