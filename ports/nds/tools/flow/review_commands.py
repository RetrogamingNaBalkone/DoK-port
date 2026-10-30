"""Publish bounded host-side choices to an already physically paused input run.

This controls only command JSON files. It never accesses emulated memory or
advances time: the ordinary driver consumes choices through real Start/input.
"""
import argparse,json,time
from pathlib import Path
from plan_routes import ROOT,pin
from run_routes import validate_review_choice


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    parser.add_argument('--level',type=int,required=True)
    parser.add_argument('--goal',type=int,nargs=2)
    parser.add_argument('--combat',choices=('true','false'),default='true')
    parser.add_argument('--avoid-monsters',choices=('true','false'),default='true')
    parser.add_argument('--pause-after-goal',action='store_true')
    parser.add_argument('--ammo-reserve',type=int)
    parser.add_argument('--health-reserve',type=int)
    parser.add_argument('--wait-callbacks',type=int)
    parser.add_argument('--face-heading',type=int,help='Manual real-button alignment to heading 0..359, then a new native pause')
    parser.add_argument('--buttons',nargs='+',help='Manual held joypad names/indices, e.g. R UP L; no combat assist')
    parser.add_argument('--hero-callbacks',type=int,help='Manual callback budget 1..240; required for --buttons, default240 for facing')
    parser.add_argument('--step',nargs='+',action='append',help='One bounded multi-step segment: N BUTTON...; repeat --step')
    parser.add_argument('--abort',action='store_true',help='Stop an existing control from its real pause checkpoint')
    parser.add_argument('--until-glyph-gone',type=int)
    parser.add_argument('--until-glyph-dead',type=int)
    parser.add_argument('--until-keys',type=int)
    parser.add_argument('--until-retries',type=int)
    parser.add_argument('--max-commands',type=int,default=1)
    parser.add_argument('--timeout',type=float,default=25)
    parser.add_argument('--health-min',type=int,default=1)
    parser.add_argument('--charge-min',type=int,default=0)
    args=parser.parse_args();run=args.run.resolve()
    assert any(run.is_relative_to(ROOT/'build/nds'/task) for task in ('n22','n23')) and 1<=args.max_commands<=32
    assert args.wait_callbacks is None or (1<=args.wait_callbacks<=240 and not args.goal),'wait-callbacks 1..240 is separate from goal'
    assert not args.abort or (not args.goal and args.wait_callbacks is None),'abort is separate from navigation/wait'
    assert sum((args.goal is not None,args.wait_callbacks is not None,args.abort,args.face_heading is not None,args.buttons is not None,args.step is not None))<=1,'Choose one navigation/wait/abort/manual action'
    assert args.hero_callbacks is None or 1<=args.hero_callbacks<=240,'hero-callbacks must be 1..240'
    assert args.hero_callbacks is None or args.face_heading is not None or args.buttons is not None,'hero-callbacks requires a manual action'
    if args.face_heading is not None:assert 0<=args.face_heading<360,'face-heading must be 0..359'
    if args.buttons is not None:
        assert args.hero_callbacks is not None,'buttons requires hero-callbacks'
        names=('B','Y','SELECT','START','UP','DOWN','LEFT','RIGHT','A','X','L','R')
        args.buttons=[int(value) if value.isdecimal() else names.index(value.upper()) for value in args.buttons]
        assert len(args.buttons)==len(set(args.buttons)) and all(0<=button<12 for button in args.buttons),'Unknown or duplicate button'
    if args.step:
        names=('B','Y','SELECT','START','UP','DOWN','LEFT','RIGHT','A','X','L','R')
        args.step=[dict(hero_callbacks=int(row[0]),buttons=[int(v) if v.isdecimal() else names.index(v.upper()) for v in row[1:]]) for row in args.step]
    source=Path(__file__);snapshot=run/'sources'/(pin(source)['sha256']+'-'+source.name)
    snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_bytes(source.read_bytes())
    stop=time.monotonic()+args.timeout;sent=[];reason='timeout'
    while time.monotonic()<stop:
        if (run/'report.json').exists():reason='run_finished';break
        try:progress=json.loads((run/'progress.json').read_text('utf8'))
        except (FileNotFoundError,json.JSONDecodeError):time.sleep(.05);continue
        if not progress.get('waiting_for_command'):time.sleep(.05);continue
        command=Path(progress['waiting_for_command'])
        if command.exists():time.sleep(.05);continue
        checkpoint=json.loads(Path(progress['checkpoint']).read_text('utf8'))
        flow=checkpoint['flow'];ordinal=checkpoint['ordinal']
        assert checkpoint['level']==args.level
        if args.wait_callbacks is not None:
            assert 'wait_callbacks' in checkpoint.get('controller_capabilities',()),'This loaded controller has no wait_callbacks support'
        for option in ('face_heading','button_steps'):
            requested=args.face_heading is not None if option=='face_heading' else args.buttons is not None or args.step is not None
            if requested:assert option in checkpoint.get('controller_capabilities',()),f'This loaded controller has no {option} support'
        if not args.abort and (flow[6]<args.health_min or flow[25]<args.charge_min):reason='resource_boundary';break
        if args.until_keys is not None and flow[24]&args.until_keys==args.until_keys:
            reason='keys_reached';break
        if args.until_retries is not None and flow[14]>=args.until_retries:
            reason='retry_boundary';break
        if args.until_glyph_gone is not None and args.until_glyph_gone not in bytes.fromhex(checkpoint['mmap']):
            reason='target_retired';break
        advisory=checkpoint.get('advisory_ai',{})
        if args.until_glyph_dead is not None and advisory.get('accepted') and any(
                e['glyph']==args.until_glyph_dead and e['health']<=0 for e in advisory['enemies']):
            reason='target_advisory_hp_zero';break
        choice=dict(checkpoint=ordinal,level=args.level,combat=args.combat=='true',
            avoid_monsters=args.avoid_monsters=='true',pause_after_goal=args.pause_after_goal,
            host_controller=pin(snapshot),note='Bounded ordinary-input host choice; no emulated memory writes')
        if args.goal:choice['goal']=args.goal
        if args.wait_callbacks is not None:choice['wait_callbacks']=args.wait_callbacks
        if args.face_heading is not None:choice.update(face_heading=args.face_heading,hero_callbacks=args.hero_callbacks or 240)
        if args.buttons is not None:choice['button_steps']=[dict(buttons=args.buttons,hero_callbacks=args.hero_callbacks)]
        if args.step is not None:choice['button_steps']=args.step
        if args.abort:
            assert checkpoint['frontend'][3]==9,'Control abort requires an actual PAUSE checkpoint'
            choice.update(abort=True,note='Host requested consolidation: stop CONTROL from actual PAUSE, not FINISH')
        for key in ('ammo_reserve','health_reserve'):
            value=getattr(args,key)
            if value is not None:choice[key]=value
        validate_review_choice(choice,ordinal,args.level)
        temporary=command.with_suffix('.tmp')
        temporary.write_text(json.dumps(choice,ensure_ascii=False,indent=2)+'\n','utf8');temporary.replace(command)
        sent.append(ordinal)
        if len(sent)>=args.max_commands:reason='command_limit';break
    print(json.dumps(dict(sent=sent,reason=reason)))


if __name__=='__main__':main()
