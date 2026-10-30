"""Read-only N22 route aid: source pickups, hazards and conservative aiming.

This module does not execute an emulator, edit RAM, or certify a playthrough.
Coordinates are recovered runtime map x,z cells, not original LEVEL text rows.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
from plan_routes import Level, field


def pickup_lists(level):
    result = dict(health=[], charge=[], hazards=[], weapons=[])
    for cell, glyph in enumerate(level.maps[1]):
        obj = level.objects[glyph]
        if obj['class'] not in (3,10):continue
        category, value = field(obj, 0x54), field(obj, 0x56)
        row = dict(cell=cell, xy=level.xy(cell), glyph=glyph, value=value)
        if obj['class'] == 10:
            result['weapons'].append(dict(**row, slot=field(obj, 0x81)))
        elif obj['class'] == 3 and category in (0, 1):
            result['hazards' if category == 0 and value < 0 else 'health' if category == 0 else 'charge'].append(row)
    return result


def clear_line(level, mmap, tmap, source_x, source_z, target_cell):
    """Conservative source cell test at 80-unit spacing.

    Actual closed/open door flags are not supplied here: every door blocks this
    aid. Source projectile code remains the authority. A nearer actor blocks
    firing at a farther one, avoiding charge wasted through intervening walls.
    """
    tx, tz = [(v + .5) * level.side for v in level.xy(target_cell)]
    dx, dz = tx-source_x, tz-source_z
    count=max(1, math.ceil(math.hypot(dx,dz)/80))
    origin=(int(source_z)//level.side)*level.width+int(source_x)//level.side
    previous=origin
    for step in range(1,count+1):
        x,z=source_x+dx*step/count,source_z+dz*step/count
        cx,cz=int(x)//level.side,int(z)//level.side
        if not 0<=cx<level.width or not 0<=cz<level.height:return False
        cell=cz*level.width+cx
        if cell==previous:continue
        previous=cell
        if cell==target_cell:return True
        obj=level.objects[mmap[cell]]
        if obj['class'] in (0,8):obj=level.objects[tmap[cell]]
        if obj['class']==1:continue
        if obj['class'] in (7,9) or not obj['flags']&0x18:return False
    return True


def aim_targets(level,mmap,tmap,x,z,limit_cells=6,excluded_targets=()):
    excluded_targets=set(excluded_targets)
    result=[]
    for cell,glyph in enumerate(mmap):
        obj=level.objects[glyph]
        if obj['class']!=9 or (glyph,cell) in excluded_targets:continue
        tx,tz=[(v+.5)*level.side for v in level.xy(cell)]
        dx,dz=tx-x,tz-z;distance=math.hypot(dx,dz)
        if distance>limit_cells*level.side or not clear_line(level,mmap,tmap,x,z,cell):continue
        result.append(dict(cell=cell,xy=level.xy(cell),glyph=glyph,distance=distance,
                           heading=math.degrees(math.atan2(-dx,dz))%360))
    return sorted(result,key=lambda row:row['distance'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--level',type=int,choices=(1,2,3),required=True)
    parser.add_argument('--near',type=int,nargs=2)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();level=Level(args.level)
    report=dict(level=args.level,source_pins=level.pins,pickups=pickup_lists(level),
                weapon_damage=[dict(slot=slot,initial=field(level.objects[ord('0')+slot],0x5c),
                               floor=field(level.objects[ord('0')+slot],0x5c)//2)
                               for slot in (1,2)],
                coordinate_system='Recovered original runtime x,z cells; no world mutations')
    if args.near:
        x,z=args.near
        for rows in report['pickups'].values():rows.sort(key=lambda row:sum(abs(a-b) for a,b in zip(row['xy'],(x,z))))
    encoded=json.dumps(report,indent=2)
    if args.output:
        root=Path(__file__).resolve().parents[4]
        output=args.output.resolve()
        assert output.is_relative_to(root/'research/evidence/nds/n22')
        output.parent.mkdir(parents=True,exist_ok=True);output.write_text(encoded+'\n',encoding='utf-8')
    print(encoded)


if __name__=='__main__':main()
