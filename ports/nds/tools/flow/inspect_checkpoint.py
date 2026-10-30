"""Inspect an ordinary-input safety checkpoint; never controls the emulator."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from plan_routes import Level,field,pin
from analyze_tactics import pickup_lists,aim_targets


def inspect(path,top=8,goal=None,avoid=False):
    checkpoint=json.loads(path.read_text('utf8'))
    level=Level(checkpoint['level'])
    mmap=bytes.fromhex(checkpoint['mmap']);tmap=bytes.fromhex(checkpoint['tmap'])
    x,y,z,h=checkpoint['pose'];start=int(z)//level.side*level.width+int(x)//level.side
    inventory=checkpoint['flow'][24]|(16 if checkpoint['frontend'][51] else 0)
    # The observer already normalized only corpses and fully-open door markers.
    opened={glyph for glyph,obj in level.objects.items() if obj['class']==7 and
            any(original==glyph and current==32 for original,current in zip(level.maps[1],tmap))}
    for glyph,bit in level.sensor_bits.items():
        if glyph in opened:inventory|=bit
    resources=pickup_lists(level)
    # Navigation aid uses the current published map copies, not initial actors.
    level.maps[0]=mmap;level.maps[1]=tmap
    actors={i for i,g in enumerate(mmap) if level.objects[g]['class']==9}
    avoid_cells=actors-{start} if avoid else ()
    result=dict(checkpoint=pin(path),level=level.number,pose=checkpoint['pose'],
        health=checkpoint['flow'][6],lives=checkpoint['flow'][7],keys=checkpoint['flow'][24],
        charge=checkpoint['flow'][25],weapon=checkpoint['frontend'][51],
        inferred_open_doors=sorted(opened),avoid_live_actors=avoid,
        visible_targets=aim_targets(level,mmap,tmap,x,z,limit_cells=10),resources={},
        limitation='Current snapshot and source grid planning are advisory; native collision, sensor toggles and actors remain authoritative.')
    def route(cell):
        path,cost=level.route(start=start,inventory=inventory,targets={cell},required_weapon=False,
                              opened=opened,avoid=avoid_cells)
        pickups=[]
        for i,flags in path:
            obj=level.objects[tmap[i]]
            if obj['class'] in (3,10):pickups.append(dict(xy=level.xy(i),category=field(obj,0x54),value=field(obj,0x56)))
        return dict(steps=len(path)-1,cost=cost,goal=level.xy(cell),path=[level.xy(i) for i,f in path],
                    live_actors=[dict(xy=level.xy(i),glyph=mmap[i]) for i,f in path if i in actors],pickups=pickups)
    for kind,rows in resources.items():
        if kind=='hazards':continue
        if not top:
            result['resources'][kind]=[]
            continue
        candidates=[]
        for row in rows:
            if tmap[row['cell']]!=row['glyph']:continue
            try:candidate=route(row['cell'])
            except ValueError:continue
            candidate.pop('path',None)
            candidate.update(value=row['value'],glyph=row['glyph']);candidates.append(candidate)
        result['resources'][kind]=sorted(candidates,key=lambda row:(row['steps'],row['cost']))[:top]
    if goal:
        try:result['goal_route']=route(goal[1]*level.width+goal[0])
        except ValueError as error:result['goal_route']=dict(error=str(error))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('checkpoint',type=Path)
    p.add_argument('--top',type=int,default=8)
    p.add_argument('--goal',type=int,nargs=2)
    p.add_argument('--avoid-monsters',action='store_true')
    args=p.parse_args();print(json.dumps(inspect(args.checkpoint.resolve(),args.top,args.goal,args.avoid_monsters),ensure_ascii=False,indent=2))

if __name__=='__main__':main()
