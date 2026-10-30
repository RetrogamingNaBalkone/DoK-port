"""Verify bounded input smoke evidence; does not start or access an emulator."""
import argparse
import json
from pathlib import Path
from types import SimpleNamespace

from plan_routes import ROOT,pin
from run_routes import RouteObserver,ManualActionStopped,manual_choice,R,UP,L,Y,A


def check(directory):
    report_path=directory/'report.json'
    report=json.loads(report_path.read_text('utf8'))
    assert not report['passed'] and not report['completed_playthrough']
    assert 'Reviewed route stopped' in report['error'],'Smoke must close from native pause'
    assert report['last_probe'][3]==9 and report['last_probe'][30]==0
    assert report['last_renderer']['error_bits']==0 and report['last_renderer']['status']!=3
    assert not any(report['last_audio_probe'][i] for i in (8,9,22))
    for row in report['source_snapshots']:
        saved=row['snapshot'];assert pin(ROOT/saved['path'])==saved
    pauses=report['review_pauses']
    results=[row['manual_result'] for row in pauses if 'manual_result' in row]
    assert len(results)==3
    face,pulse,deadline=results
    assert face['action']['kind']=='face_heading' and face['stop_reason']=='heading_reached'
    assert abs(face['heading_error'])<=1 and 1<=face['actual_callbacks']<=240
    assert face['start_pose'][:3]==face['end_pose'][:3]
    assert pulse['action']['steps']==[dict(buttons=[R,UP,L],hero_callbacks=8)]
    assert pulse['actual_callbacks']==8 and pulse['callback_batch_overshoot']==0
    assert pulse['start_pose'][:3]!=pulse['end_pose'][:3]
    assert pulse['start_pose'][3]==pulse['end_pose'][3]
    assert deadline['action']['budget']==deadline['actual_callbacks']==1
    assert deadline['stop_reason']=='callback_budget' and abs(deadline['heading_error'])>1
    for entry in pauses:
        result=entry.get('manual_result')
        if not result:continue
        assert result['release_buttons']==[]
        ordinal=entry['choice']['checkpoint']+1
        following=json.loads((directory/f'checkpoint-{ordinal}.json').read_text('utf8'))
        assert following['frontend'][3]==9
    inputs=report['input_steps']
    held=[row for row in inputs if row['video_frame']==pulse['start_video_frame']]
    assert any(row['buttons']==[R,UP,L] for row in held)
    assert all(Y not in row['buttons'] and A not in row['buttons'] for row in inputs
        if pulse['start_video_frame']<=row['video_frame']<pulse['end_video_frame'])
    assert any(row['buttons']==[] and row['video_frame']==pulse['release_video_frame'] for row in inputs)
    before=json.loads((directory/'checkpoint-3.json').read_text('utf8'))
    after=json.loads((directory/'checkpoint-4.json').read_text('utf8'))
    assert before['flow'][25]-after['flow'][25]==8
    assert report['rows'][-1]['all_projectile_shots']==2
    assert before['flow'][6]==after['flow'][6]==90
    invalid=[{'face_heading':360},{'face_heading':True},
        {'face_heading':90,'goal':[1,1]},
        {'button_steps':[{'buttons':['R'],'hero_callbacks':241}]},
        {'button_steps':[{'buttons':[12],'hero_callbacks':8}]},
        {'button_steps':[{'buttons':['R','R'],'hero_callbacks':8}]},
        {'button_steps':[{'buttons':['R'],'hero_callbacks':200},{'buttons':[],'hero_callbacks':41}]}]
    for choice in invalid:
        try:manual_choice(choice)
        except AssertionError:pass
        else:raise AssertionError(('Invalid manual command accepted',choice))
    safety=[]
    for reason,change in (
        ('menu',lambda f:f.probe.__setitem__(3,3)),
        ('world_changed',lambda f:f.flow.__setitem__(4,2)),
        ('retry',lambda f:f.flow.__setitem__(14,3)),
        ('counter_reset',lambda f:f.flow.__setitem__(15,9)),
        ('callback_budget',lambda f:f.flow.__setitem__(15,18)),
        ('video_deadline',lambda f:setattr(f,'frames',104)),
        ('health_reserve',lambda f:f.flow.__setitem__(6,20)),
        ('ammo_reserve',lambda f:f.flow.__setitem__(25,10))):
        f=SimpleNamespace(probe=[0]*64,flow=[0]*32,frames=0,review_health=20,review_ammo=10,
            _manual_context=dict(serial=1,retry=2,before=10,budget=8,frame=0,firing=True))
        f.probe[3]=8;f.probe[51]=2
        for key,value in ((3,1),(4,1),(6,100),(14,2),(15,10),(25,100)):f.flow[key]=value
        change(f)
        try:RouteObserver.manual_boundary(f)
        except ManualActionStopped as stopped:assert str(stopped)==reason
        else:raise AssertionError(('Missing manual safety boundary',reason))
        safety.append(reason)
    return dict(task='N22',passed=True,accepted_playthrough=False,
        scope='Bounded actual-button helper smoke; host-aborted at native pause, no FINISH claim',
        producer=pin(Path(__file__)),report=pin(report_path),profile=pin(directory/'runtime-profile.json'),
        rom=report['rom'],elf=report['elf'],driver=report['driver'],manual_results=results,
        validation_rejections=len(invalid),host_boundary_checks=safety,
        actual_input=dict(buttons=[R,UP,L],callbacks=8,projectile_shots=2,charge_before=150,charge_after=142),
        limitation='Callback batching may exceed a requested boundary by one native dispatch; observed overshoot0. Native pause/resume and movement inertia remain real.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=check(args.directory.resolve())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=result['passed'],output=pin(args.output.resolve()))))
