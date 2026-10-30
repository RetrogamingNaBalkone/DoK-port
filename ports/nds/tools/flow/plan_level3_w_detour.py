"""Read-only N22 LEVEL3 W detour v2: movement paths are not ballistic cover."""
import argparse,json
from pathlib import Path

from plan_routes import ROOT,Level,field,pin
from inspect_checkpoint import inspect


def current_route(checkpoint,start,goal,avoid=()):
    level=Level(3)
    original=level.maps[1]
    level.maps[0]=bytes.fromhex(checkpoint['mmap'])
    level.maps[1]=bytes.fromhex(checkpoint['tmap'])
    opened={g for g,obj in level.objects.items() if obj['class']==7 and
        any(a==g and b==32 for a,b in zip(original,level.maps[1]))}
    inventory=checkpoint['flow'][24]|16
    for glyph,bit in level.sensor_bits.items():
        if glyph in opened:inventory|=bit
    path,cost=level.route(start=start[1]*64+start[0],inventory=inventory,
        targets={goal[1]*64+goal[0]},required_weapon=False,opened=opened,avoid=avoid)
    return dict(steps=len(path)-1,cost=cost,path=[level.xy(i) for i,f in path],
        actors=[dict(xy=level.xy(i),glyph=level.maps[0][i]) for i,f in path
            if level.objects[level.maps[0][i]]['class']==9])


def build(directory):
    historical=ROOT/'research/evidence/nds/n22/level3-w-detour.json'
    assert pin(historical)['sha256']=='e7053fac8d5989f9e8202d5a938f18c12fa697258b88a552cf28cfe2c248eb5f'
    historical_data=json.loads(historical.read_text('utf8'))
    sources=ROOT/'research/evidence/nds/n22/level3-w-detour-sources'
    snapshots=[]
    for source in (Path(__file__),ROOT/'ports/nds/core/combat.c',ROOT/'ports/nds/core/ai.c',ROOT/'ports/nds/core/collision.c'):
        original=pin(source);saved=sources/(original['sha256']+'-'+source.name)
        raw=source.read_bytes();sources.mkdir(parents=True,exist_ok=True)
        assert not saved.exists() or saved.read_bytes()==raw
        saved.write_bytes(raw);snapshots.append(dict(original=original,snapshot=pin(saved)))
    old_source=sources/(historical_data['tool']['sha256']+'-plan_level3_w_detour.py')
    assert pin(old_source)['sha256']==historical_data['tool']['sha256']
    p27=directory/'checkpoint-27.json';p29=directory/'checkpoint-29.json';p43=directory/'checkpoint-43.json'
    old=json.loads(p27.read_text('utf8'));prefix=json.loads(p29.read_text('utf8'));current=json.loads(p43.read_text('utf8'))
    w=inspect(p27,0,(17,30),False)['goal_route']
    key4=inspect(p27,0,(23,25),False)['goal_route']
    common=0
    for a,b in zip(w['path'],key4['path']):
        if a!=b:break
        common+=1
    after_w=current_route(old,[17,30],[23,25])
    assert common==129 and w['path'][common-1]==[13,24]
    assert (w['steps'],key4['steps'],after_w['steps'])==(138,173,55)
    assert not w['live_actors'] and not after_w['actors']
    prefix_w=inspect(p29,0,(17,30),True)['goal_route']
    prefix_food=inspect(p29,0,(13,26),True)['goal_route']
    food_w=current_route(prefix,[13,26],[17,30])
    assert prefix_w['steps']==prefix_food['steps']+food_w['steps']==24
    assert not prefix_food['live_actors'] and not food_w['actors']
    assert dict(xy=[13,26],category=0,value=15) in prefix_food['pickups']
    level=Level(3)
    doors=[]
    for x,z in w['path']:
        cell=z*64+x;obj=level.objects[level.maps[1][cell]]
        if obj['class']==7:doors.append(dict(xy=[x,z],glyph=chr(obj['glyph']),key_mask=field(obj,0x5c)))
    separator=[]
    for z in range(14,35):
        obj=level.objects[level.maps[1][z*64+22]]
        assert (obj['glyph'],obj['class'],obj['flags'])==(75,4,8)
        separator.append(dict(xy=[22,z],glyph=75,character='K',class_id=4,flags=8,
            movement_blocked=(obj['flags']&0x10)==0,
            projectile_contact=(obj['flags']&0x18)==0,sight_blocked=(obj['flags']&0x18)==0))
    live_directory=directory.parent/'route-1-level3-root-ru-l3-w-prefix-v5'
    live_samples=[]
    for ordinal in (1,2,3,4,5):
        path=live_directory/f'checkpoint-{ordinal}.json';sample=json.loads(path.read_text('utf8'))
        advisory=sample.get('advisory_ai',{})
        live_samples.append(dict(checkpoint=pin(path),pose=sample['pose'],health=sample['flow'][6],
            charge=sample['flow'][25],world_serial=sample['flow'][4],retries=sample['flow'][14],
            ai_accepted=advisory.get('accepted'),npc220=[e for e in advisory.get('enemies',[]) if e['glyph']==220]))
    assert live_samples[3]['health']==44 and live_samples[4]['health']==23
    assert all(row['ai_accepted'] and row['npc220'][0]['state']==0 for row in live_samples)
    source_gates=[]
    for destination,avoid in (([51,8],52*64+52),([17,30],8*64+51)):
        try:level.route(inventory=16,targets={destination[1]*64+destination[0]},avoid={avoid})
        except ValueError:source_gates.append(dict(destination=destination,excluded_key_cell=level.xy(avoid),reachable=False))
        else:raise AssertionError(('Expected key dependency',destination))
    recovery=[]
    start=[current['pose'][0]//480,current['pose'][2]//480]
    for excluded in ((),(58*64+44,),(58*64+44,58*64+45),(58*64+44,58*64+45,57*64+46)):
        try:route=current_route(current,start,[17,30],excluded)
        except ValueError:route=dict(reachable=False)
        else:route['reachable']=True
        recovery.append(dict(avoid=[level.xy(i) for i in excluded],route=route))
    assert not recovery[-1]['route']['reachable']
    advisory=current.get('advisory_ai',{})
    return dict(task='N22',revision=2,analysis_only=True,completed_playthrough=False,
        scope='Archived maps and original source graph; no live emulator access or commands',
        correction=dict(historical=pin(historical),historical_tool=pin(old_source),
            invalidated_claim='Opaque solid wall separates W room from eastern boar fire',
            corrected_fact='K75/class4/flags8 blocks movement but permits native sight and projectile passage',
            route_lengths_unchanged=True),
        source_snapshots=snapshots,live_v5_samples=live_samples,
        source_contracts=[pin(ROOT/'docs/nds/spec/N15-combat.md'),pin(ROOT/'docs/nds/spec/N17-ai-combat.md')],
        semantics=dict(movement='collision.c: traversable only if flags&0x10',
            projectile='combat.c contact: ordinary class4 accepts impact only if !(flags&0x18)',
            sight='ai.c sight: flags&0x18 permits ray to continue',
            implication='Eastern NPCs can see/fire across K; grid movement barrier does not imply shelter or sleeping army'),
        tool=pin(Path(__file__)),inputs=[pin(p27),pin(p29),pin(p43),*level.pins],
        coordinate_system='64x64 recovered runtime map, x/z cells of480, not LEVEL file order',
        source_key_dependencies=source_gates,
        cp27=dict(pose=old['pose'],health=old['flow'][6],lives=old['flow'][7],keys=old['flow'][24],charge=old['flow'][25]),
        common_prefix_steps=common-1,branch=[13,24],
        branch_to_w=w['path'][common-1:],branch_to_key4=key4['path'][common-1:],
        cp27_to_w=w,cp27_to_key4=key4,w_to_key4=after_w,
        extra_steps_for_w=w['steps']+after_w['steps']-key4['steps'],
        cp29=dict(pose=prefix['pose'],health=prefix['flow'][6],lives=prefix['flow'][7],keys=prefix['flow'][24],charge=prefix['flow'][25]),
        cp29_to_w=prefix_w,cp29_to_food13_26=prefix_food,food13_26_to_w=food_w,
        doors_on_w_route=doors,movement_separator_from_boar_corridor=separator,
        west_chamber_source_actor=dict(xy=[16,16],glyph=220,health=field(level.objects[220],0xa4)),
        current_cp43=dict(pose=current['pose'],health=current['flow'][6],lives=current['flow'][7],
            keys=current['flow'][24],charge=current['flow'][25],advisory_accepted=advisory.get('accepted'),
            near_actors=[e for e in advisory.get('enemies',[]) if e['glyph'] in (128,195,197,233)]),
        recovery_paths=recovery,
        limitation='Actor-free movement paths are not fire-free paths. K permits source sight and projectiles; W room is exposed to eastern NPCs. Live v5 lost44->23HP with NPC220 inactive. Exact projectile owner is not decoded by these checkpoints. W detour is a graph strategy, not a safety or acceptance proof.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory',type=Path,default=ROOT/'build/nds/n22/candidate-partial02-ru/route-1-level3-root-ru-l3-v4')
    parser.add_argument('--output',type=Path,default=ROOT/'research/evidence/nds/n22/level3-w-detour-v2.json')
    args=parser.parse_args()
    assert args.output.resolve()!=(ROOT/'research/evidence/nds/n22/level3-w-detour.json').resolve(),'Historical v1 must remain immutable'
    result=build(args.directory.resolve())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(dict(branch=result['branch'],extra_steps=result['extra_steps_for_w'],output=pin(args.output.resolve()))))
