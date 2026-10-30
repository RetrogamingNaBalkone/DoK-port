"""N22 ordinary-input playthrough driver; all emulated memory is read-only.

Navigation decisions use recovered original map cells. Expected completion is
never injected: the production FINISH/state, real AI, projectile/audio/map
callbacks and actual displayed presentations must report their own results.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct
import sys
import traceback
import time

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
import run_diag_emulator as transport
import run_renderer_emulator as render
import run_frontend_emulator as front
from frontend_fat_oracle import volume_files, audit_changes
from plan_routes import Level, pin, field
from analyze_tactics import aim_targets,pickup_lists
from tactical_controller import choose_buttons
from read_live_ai import LiveAiReader

# Libretro physical joypad indices; no direct simulation commands.
B,Y,SELECT,START,UP,DOWN,LEFT,RIGHT,A,X,L,R = range(12)
BUTTON_NAMES=('B','Y','SELECT','START','UP','DOWN','LEFT','RIGHT','A','X','L','R')


class ManualActionStopped(Exception):
    """An ordinary input action reached its reviewed safety boundary."""


def manual_choice(choice):
    """Validate host JSON before resuming native PAUSE; never touches RAM."""
    modes=[name for name in ('face_heading','button_steps') if name in choice]
    if not modes:return None
    assert len(modes)==1,'Use one manual action per review checkpoint'
    assert not any(name in choice for name in ('goal','wait_callbacks','abort')),'Manual actions are separate from navigation/wait/abort'
    mode=modes[0]
    if mode=='face_heading':
        heading=choice[mode];budget=choice.get('hero_callbacks',240)
        assert type(heading) is int and 0<=heading<360,'face_heading must be integer 0..359'
        assert type(budget) is int and 1<=budget<=240,'hero_callbacks must be integer 1..240'
        return dict(kind=mode,heading=heading,budget=budget)
    steps=choice[mode]
    assert isinstance(steps,list) and 1<=len(steps)<=240,'button_steps must be a nonempty bounded list'
    normalized=[]
    for step in steps:
        assert isinstance(step,dict) and set(step)=={'buttons','hero_callbacks'},'Each button step needs only buttons and hero_callbacks'
        count=step['hero_callbacks'];buttons=step['buttons']
        assert type(count) is int and 1<=count<=240,'hero_callbacks must be integer 1..240'
        assert isinstance(buttons,list) and len(buttons)<=12,'buttons must be a list of names or indices'
        ids=[]
        for button in buttons:
            if isinstance(button,str):
                assert button.upper() in BUTTON_NAMES,('Unknown button',button)
                button=BUTTON_NAMES.index(button.upper())
            assert type(button) is int and 0<=button<12,('Unknown button index',button)
            assert button not in ids,'Duplicate button'
            ids.append(button)
        normalized.append(dict(buttons=ids,hero_callbacks=count))
    budget=sum(step['hero_callbacks'] for step in normalized)
    assert budget<=240,'Total manual callback budget must be <=240'
    return dict(kind=mode,steps=normalized,budget=budget)


def validate_review_choice(choice, checkpoint, level):
    """Validate the complete host envelope and action before native resume."""
    assert isinstance(choice,dict),'Review command must be a JSON object'
    assert type(choice.get('checkpoint')) is int and choice['checkpoint']==checkpoint,'Review checkpoint mismatch or missing'
    assert type(choice.get('level')) is int and choice['level']==level,'Review level mismatch or missing'
    for name in ('combat','avoid_monsters','pause_after_goal','abort'):
        if name in choice:assert type(choice[name]) is bool,f'{name} must be boolean'
    for name in ('health_reserve','ammo_reserve'):
        if name in choice:assert type(choice[name]) is int and choice[name]>=0,f'{name} must be a nonnegative integer'
    modes=[name for name in ('goal','wait_callbacks','face_heading','button_steps','abort') if name in choice and (name!='abort' or choice[name])]
    assert len(modes)<=1,'Choose at most one review action'
    if 'goal' in choice:
        goal=choice['goal']
        assert isinstance(goal,list) and len(goal)==2 and all(type(v) is int and 0<=v<64 for v in goal),'goal must contain two integer cells 0..63'
    if 'wait_callbacks' in choice:
        assert type(choice['wait_callbacks']) is int and 1<=choice['wait_callbacks']<=240,'wait_callbacks must be integer 1..240'
    manual_choice(choice)
    return choice


class RouteObserver(transport.Frontend):
    def __init__(self, core, target, directory):
        super().__init__(core,directory,'happy')
        self.options['melonds_homebrew_readonly']='disabled'
        self.target=target
        elf=target/'dok_nds_gameplay.elf'
        self.front_address,_=transport.elf_symbol(elf,'g_nds_frontend_probe')
        self.render_address,_=transport.elf_symbol(elf,'g_nds_render_probe')
        self.game_address,_=transport.elf_symbol(elf,'g_nds_gameplay_probe')
        self.map_address,_=transport.elf_symbol(elf,'g_nds_map_probe')
        self.audio_address,_=transport.elf_symbol(elf,'g_nds_audio_probe')
        try: self.flow_address,_=transport.elf_symbol(elf,'g_nds_flow_probe')
        except Exception: self.flow_address=None
        self.flow=None
        self.render_fields=render.scalar_fields(ROOT/'ports/nds/platform/render_probe/render_probe.h','NdsRenderProbe')
        self.probe=None
        self.renderer=None
        self.game=None
        self.last_pose=None
        self.rows=[]
        self.input_steps=[]
        self.screens=[]
        self.key_captures=set()
        self.low_health_captures=set()
        self.previous_touch=None
        self.review_pauses=False
        self._in_review=False
        self.pending_choice=None
        self.level_data=None
        self.review_health=60
        self.review_ammo=32
        self.explicit_health_reserve=False
        self.explicit_ammo_reserve=False
        self.previous_charge=0
        self.force_no_combat=False
        self.force_avoid_monsters=True
        self.serial=0
        self.last_record=0
        try:self.ai_reader=LiveAiReader(elf)
        except (AssertionError,OSError) as error:
            self.ai_reader=None;self.ai_reader_reason=str(error)
        source_pins=[]
        sources=[Path(__file__)]+[ROOT/'ports/nds/tools/flow'/name for name in ('plan_routes.py','analyze_tactics.py','tactical_controller.py','read_live_ai.py')]
        if self.ai_reader:
            sources.extend(path for name in ('candidate01-ai-layout.json','candidate01-en-ai-layout.json',
                                             'candidate02-ai-layout.json','candidate02-en-ai-layout.json')
                if (path:=ROOT/'research/evidence/nds/n22'/name).exists())
        for source in sources:
            raw=source.read_bytes();digest=transport.sha(source)
            saved=directory/'sources'/(digest+'-'+source.name)
            saved.parent.mkdir(parents=True,exist_ok=True);saved.write_bytes(raw)
            source_pins.append(dict(original=pin(source),snapshot=pin(saved)))
        self.report=dict(source_snapshots=source_pins,task='N22',passed=False,completed_playthrough=False,
             method='Real libretro joypad callbacks; read-only RAM probes; original-map navigation aid',
             driver=pin(Path(__file__)), core=pin(core) if core.is_relative_to(ROOT) else dict(path=str(core),sha256=transport.sha(core)),
             rom=pin(target/'dok_nds_gameplay.nds'),elf=pin(elf), input_steps=self.input_steps,
             rows=self.rows,screens=self.screens, limitations=['Emulator only; physical DS/DS Lite remains N24'])

    def tick(self,count=1):
        for _ in range(count):
            if self.frame_time and self.frame_time.callback:
                self.frame_time.callback(round(1000000/self.av.timing.fps))
            self.core.retro_run();self.frames+=1
            assert not self.callback_error and not self.shutdown,self.callback_error
            if self.probe and self.probe[3]==8:
                stats=self.report.setdefault('play_audio',dict(video_frames=0,samples=0,nonzero_frames=0))
                stats['video_frames']+=1;stats['samples']+=len(self.pcm)//4
                stats['nonzero_frames']+=bool(any(self.pcm))
            self.pcm.clear()
            p=struct.unpack('<64I',render.ram(self,self.front_address,256))
            if p[0]==0x21504b44 and not p[2]&1 and p[3]:
                assert not p[30],('Frontend fault',p)
                self.probe=p
            if self.flow_address:
                flow=struct.unpack('<32I',render.ram(self,self.flow_address,128))
                if flow[0]==0x22504b44 and not flow[2]&1:self.flow=flow
            r=render.observe(self,self.render_address,self.render_fields,0x37524b44)
            if r:
                if r['status']==3 or r['error_bits']:self.record_fault()
                assert r['status']!=3 and not r['error_bits'],('Actual renderer failure',r)
                self.renderer=r
            g=struct.unpack('<64I',render.ram(self,self.game_address,256))
            if g[0]==0x17504b44:
                assert not any(g[10:13]),('Actual gameplay fault',g)
                self.game=g
            if self.level_data and self.flow and self.flow[3]:self.map_snapshot(self.level_data)
            audio=struct.unpack('<64I',render.ram(self,self.audio_address,256))
            if audio[0]==0x18504b44 and not audio[63]&1:
                assert not audio[8] and not audio[9] and not audio[22],('Actual audio fault',audio)
                self.report['last_audio_probe']=list(audio)
            if self.probe and self.probe[3]==8 and self.renderer:
                p=self.probe;r=self.renderer
                heading=round(math.degrees(math.atan2(render.signed32(r['sin_q15']),render.signed32(r['cos_q15']))))%360
                pose=(render.signed32(p[46]),render.signed32(r['camera_y']),render.signed32(p[47]),heading)
                if self.flow and self.flow[3]:
                    pose=tuple(render.signed32(self.flow[n]) for n in (8,9,10,11))
                self.last_pose=pose
                if r['frame_id']!=self.last_record:
                    self.last_record=r['frame_id']
                    row=dict(video_frame=self.frames,presented=r['presented_frame_id'],frame=r['frame_id'],
                         state=p[3],level=p[6],epoch=p[45],serial=p[44],ticks=p[40],
                         hero_callbacks=p[41],ai_callbacks=p[42],combat_callbacks=p[43],
                         pose=list(pose),health=render.signed32(p[48]),charge=render.signed32(p[49]),keys=p[50],weapon=p[51],
                         ai_dead=self.game[23] if self.game else 0,
                         all_projectile_shots=self.game[21] if self.game else 0,
                         lives=render.signed32(self.flow[7]) if self.flow else None,
                         deaths=self.flow[13] if self.flow else None,retries=self.flow[14] if self.flow else None)
                    self.rows.append(row)
                    life=self.flow[7] if self.flow else 0
                    if render.signed32(p[48])<=25 and life not in self.low_health_captures:
                        self.low_health_captures.add(life);self.screens.append(self.capture(f'low-health-life-{life}'))
                    if p[50] and p[50] not in self.key_captures:
                        self.key_captures.add(p[50]);self.screens.append(self.capture(f'key-mask-{p[50]}'))
                    if len(self.rows)%120==0:self.progress()
            if self.review_pauses and not self._in_review and self.pending_choice is None and self.level_data and self.probe and self.probe[3]==8 and self.flow and self.flow[3]:
                charge=render.signed32(self.flow[25]);weapon=self.probe[51]
                if charge>self.previous_charge+4 and not self.explicit_ammo_reserve:self.review_ammo=42 if weapon==1 else 32
                self.previous_charge=charge
                health_alarm=0<render.signed32(self.flow[6])<=self.review_health
                ammo_alarm=weapon>0 and 0<charge<=self.review_ammo
                if health_alarm or ammo_alarm:
                    self.pause_reason=dict(kind='resource_alarm',health_alarm=health_alarm,
                        ammo_alarm=ammo_alarm,health=render.signed32(self.flow[6]),
                        charge=charge,health_threshold=self.review_health,ammo_threshold=self.review_ammo)
                    self._in_review=True
                    try:self.pending_choice=self.pause_review(self.level_data)
                    finally:self._in_review=False
            if self.pending_choice and not self._in_review:return self.probe
        return self.probe

    def pause_review(self,data):
        previous_review=self._in_review;self._in_review=True
        try:
            while True:
                choice=self._pause_review_once(data)
                action=manual_choice(choice)
                if action:
                    self.execute_manual_action(action,self.report['review_pauses'][-1])
                    continue
                if not choice.get('wait_callbacks'):return choice
                requested=int(choice['wait_callbacks'])
                record=self.report['review_pauses'][-1]
                initial_retry=record['retry_before_resume']
                initial_serial=record['serial_before_resume']
                before=self.flow[15];actual=0;steps=0;reason='callbacks_completed'
                for _ in range(requested+1):
                    if not self.probe or self.probe[3]!=8:reason='menu';break
                    if not self.flow or not self.flow[3] or self.flow[4]!=initial_serial:reason='world_changed';break
                    if self.flow[14]!=initial_retry:reason='retry';break
                    if actual>=requested:break
                    self.game_step([]);steps+=1
                    current=self.flow[15]
                    if current<before:reason='counter_reset';break
                    actual+=current-before;before=current
                else:reason='step_limit'
                record['wait_result']=dict(requested_callbacks=requested,actual_callbacks=actual,
                    step_calls=steps,stop_reason=reason,end_video_frame=self.frames,
                    method='Actual empty joypad hero steps; callback batching can exceed requested boundary')
                self.pause_reason=dict(kind='wait_callbacks_complete',**record['wait_result'])
                # The next iteration requests a new real Start pause and host
                # choice. No target code, clock or world state is injected.
        finally:self._in_review=previous_review

    def manual_boundary(self):
        context=getattr(self,'_manual_context',None)
        if not context:return
        if not self.probe or self.probe[3]!=8:raise ManualActionStopped('menu')
        if not self.flow or not self.flow[3] or self.flow[4]!=context['serial']:raise ManualActionStopped('world_changed')
        if self.flow[14]!=context['retry']:raise ManualActionStopped('retry')
        if self.flow[15]<context['before']:raise ManualActionStopped('counter_reset')
        if self.flow[15]-context['before']>=context['budget']:raise ManualActionStopped('callback_budget')
        if self.frames-context['frame']>=context['budget']*13:raise ManualActionStopped('video_deadline')
        if render.signed32(self.flow[6])<=self.review_health:raise ManualActionStopped('health_reserve')
        if context['firing'] and self.probe[51] and render.signed32(self.flow[25])<=self.review_ammo:
            raise ManualActionStopped('ammo_reserve')

    def execute_manual_action(self,action,record):
        context=dict(before=self.flow[15],serial=record['serial_before_resume'],
            retry=record['retry_before_resume'],budget=action['budget'],frame=self.frames,
            firing=action['kind']=='button_steps' and any(R in step['buttons'] for step in action['steps']))
        result=dict(action=action,start_video_frame=self.frames,start_pose=list(self.last_pose),
            hero_before=context['before'],steps=[],stop_reason='completed')
        record['manual_result']=result
        self._manual_context=context
        try:
            self.manual_boundary()
            if action['kind']=='face_heading':
                self.turn_precise(action['heading'])
                result['stop_reason']='heading_reached'
            else:
                for step in action['steps']:
                    start=self.flow[15];frame=self.frames
                    row=dict(**step,start_video_frame=frame,hero_before=start)
                    result['steps'].append(row)
                    try:
                        while self.flow[15]-start<step['hero_callbacks']:
                            self.game_step(step['buttons'])
                    finally:
                        row.update(actual_callbacks=max(0,self.flow[15]-start),end_video_frame=self.frames)
        except ManualActionStopped as stopped:result['stop_reason']=str(stopped)
        except BaseException:
            result['stop_reason']='fault_or_exception'
            raise
        finally:
            self._manual_context=None
            # Release every held bit without advancing time. The next real
            # Start/input poll observes R released; native movement inertia stays.
            self.hold([],0)
            result.update(end_video_frame=self.frames,end_pose=list(self.last_pose),
                hero_after=self.flow[15],actual_callbacks=max(0,self.flow[15]-context['before']),
                callback_batch_overshoot=max(0,self.flow[15]-context['before']-action['budget']),
                release_buttons=[],release_video_frame=self.frames)
            if action['kind']=='face_heading':result['heading_error']=(action['heading']-self.heading()+180)%360-180
            self.pause_reason=dict(kind='manual_action_complete',manual_result=result)

    def _pause_review_once(self,data):
        ordinal=len(self.report.get('review_pauses',[]))+1
        self.joypad=0;self.touch=None
        before=self.capture(f'review-{ordinal}-before')
        if self.probe and self.probe[3]==10:
            self.report['completed_playthrough']=True
            return {}
        if self.probe and self.probe[3]==8:self.tap(START)
        for _ in range(90):
            if self.probe and self.probe[3]==9 and not self.probe[9]:break
            if self.probe and self.probe[3]==10:
                self.report['completed_playthrough']=True
                return {}
            if self.probe and self.probe[3] not in (8,9):
                record=dict(video_frame=self.frames,presentation=self.probe[3],
                    flow=list(self.flow) if self.flow else None,screenshots=before)
                self.report.setdefault('review_pause_ended_world',[]).append(record)
                raise AssertionError(('World ended during ordinary pause admission',self.probe[3],self.last_pose))
            self.tick()
        else:raise AssertionError(('Pause admission timeout',self.probe))
        maps=self.map_snapshot(data)
        checkpoint=dict(level=data.number,ordinal=ordinal,video_frame=self.frames,
            controller_capabilities=['review_start','prefix_without_finish_tail','wait_callbacks','automatic_path_safety_pause','face_heading','button_steps'],
            reason=getattr(self,'pause_reason',dict(kind='unspecified')),
            pose=list(self.last_pose),flow=list(self.flow),frontend=list(self.probe),
            navigation_goal=getattr(self,'navigation_goal',None),
            mmap=bytes(maps[0]).hex() if maps else None,tmap=bytes(maps[1]).hex() if maps else None,
            map_interpretation='Published map snapshot; dead actors and fully open door markers normalized only in observer copies',
            advisory_ai=self.sample_ai(maps),
            rom=self.report['rom'])
        point=self.directory/f'checkpoint-{ordinal}.json'
        command=self.directory/f'command-{ordinal}.json'
        assert not command.exists(),'Use a fresh run-id for reviewed routes'
        point.write_text(json.dumps(checkpoint,ensure_ascii=False,indent=2)+'\n','utf8')
        self.progress(waiting_for_command=str(command),checkpoint=str(point))
        rejected=0
        while True:
            while not command.exists():time.sleep(.05)
            try:
                # Older non-atomic publishers may briefly expose partial JSON.
                for read_attempt in range(60):
                    raw=command.read_bytes()
                    try:choice=json.loads(raw);break
                    except (json.JSONDecodeError,UnicodeDecodeError):
                        if read_attempt==59:raise
                        time.sleep(.05)
                validate_review_choice(choice,ordinal,data.number)
                break
            except (AssertionError,KeyError,TypeError,ValueError,UnicodeDecodeError) as error:
                # Invalid HOST data cannot consume an otherwise live game.
                # Preserve its exact bytes and stay in the same native PAUSE;
                # no tick, input, target RAM or clock operation occurs here.
                rejected+=1
                quarantine=self.directory/f'rejected-command-{ordinal}-{rejected}.json'
                assert not quarantine.exists()
                command.replace(quarantine)
                self.report.setdefault('rejected_review_commands',[]).append(dict(
                    checkpoint=ordinal,command=pin(quarantine),error=f'{type(error).__name__}: {error}',
                    video_frame=self.frames,method='Quarantined host packet; native PAUSE and emulated time unchanged'))
                self.progress(waiting_for_command=str(command),checkpoint=str(point),
                    rejected_command=pin(quarantine),rejection=str(error))
        if choice.get('abort'):raise AssertionError(('Reviewed route stopped',ordinal))
        record=dict(checkpoint=pin(point),command=pin(command),choice=choice,
                    screenshots=before,pause_frame=self.frames)
        self.report.setdefault('review_pauses',[]).append(record)
        self.pause_reason=None
        self.review_health=render.signed32(self.probe[48])-10
        self.review_ammo=max(0,render.signed32(self.probe[49])-(14 if self.probe[51]==1 else 4))
        self.explicit_ammo_reserve='ammo_reserve' in choice
        self.explicit_health_reserve='health_reserve' in choice
        if 'ammo_reserve' in choice:
            self.review_ammo=max(0,min(render.signed32(self.probe[49])-1,int(choice['ammo_reserve'])))
        if 'health_reserve' in choice:
            self.review_health=max(0,min(render.signed32(self.probe[48])-1,int(choice['health_reserve'])))
        self.force_no_combat=choice.get('combat') is False
        self.force_avoid_monsters=choice.get('avoid_monsters',True)
        record['retry_before_resume']=self.flow[14]
        record['serial_before_resume']=self.flow[4]
        self.tap(START)
        for _ in range(90):
            if self.probe and self.probe[3]==8 and not self.probe[9]:break
            if self.probe and self.probe[3]==10:
                self.report['completed_playthrough']=True
                return {}
            if self.probe and self.probe[3] not in (8,9):
                self.report.setdefault('review_pause_ended_world',[]).append(dict(video_frame=self.frames,
                    presentation=self.probe[3],flow=list(self.flow) if self.flow else None,screenshots=before))
                raise AssertionError(('World ended during ordinary pause resume',self.probe[3],self.last_pose))
            self.tick()
        else:raise AssertionError(('Pause resume timeout',self.probe))
        record['resume_frame']=self.frames
        return choice

    def sample_ai(self,maps=None):
        if not self.ai_reader:
            return dict(accepted=False,reason=getattr(self,'ai_reader_reason','Reader unavailable'),enemies=[])
        sample=self.ai_reader.sample(lambda address,size:render.ram(self,address,size),
            mmap=maps[0] if maps else None)
        if sample.get('accepted') and self.flow:
            self.advisory_dying={e['glyph']:e['cell'] for e in sample['enemies']
                if e['health']<=0 and e['occupancy_match']}
            self.advisory_dying_serial=self.flow[4]
        return sample

    def dying_targets(self):
        if not self.flow or getattr(self,'advisory_dying_serial',None)!=self.flow[4]:return set()
        return {(glyph,cell) for glyph,cell in getattr(self,'advisory_dying',{}).items()}

    def record_fault(self):
        result={}
        for name in ('g_nds_effect_probe','g_nds_gameplay_probe','g_nds_flow_probe','g_nds_audio_probe','g_nds_map_probe','g_nds_sprite_probe'):
            try:
                address,size=transport.elf_symbol(self.target/'dok_nds_gameplay.elf',name)
                result[name]=list(struct.unpack('<'+'I'*(size//4),render.ram(self,address,size)))
            except Exception as error:result[name]=str(error)
        self.report['fault_snapshot']=result
        if 'fault_memory_dump' not in self.report:
            try:
                dump=self.directory/'renderer-fault-main-ram.bin'
                dump.write_bytes(render.ram(self,0x02000000,4*1024*1024))
                self.report['fault_memory_dump']=dict(file=pin(dump),base=0x02000000,
                    video_frame=self.frames,joypad=self.joypad,touch=self.touch,
                    scope='Actual renderer fault, before unload; raw emulator model RAM, no emulated writes or cache flush')
            except Exception as error:self.report['fault_memory_error']=str(error)

    def progress(self,**extra):
        (self.directory/'progress.json').write_text(json.dumps(dict(video_frame=self.frames,last=self.rows[-1] if self.rows else None,navigation_goal=getattr(self,'navigation_goal',None),**extra),indent=2),'utf8')

    def hold(self,buttons,count=1):
        if self.flow and self.flow[3] and self.probe and self.probe[3]==8 and (Y in buttons or A in buttons):
            self.last_turn_input_callback=self.flow[15]
        held=sum(1<<i for i in buttons)
        if held!=self.joypad or self.touch!=self.previous_touch:
            self.input_steps.append(dict(video_frame=self.frames,buttons=buttons,touch=list(self.touch) if self.touch else None))
            self.previous_touch=self.touch
        self.joypad=held;self.tick(count)

    def wait_for(self,state,selection=None,limit=12000):
        self.joypad=0
        for _ in range(limit):
            self.tick()
            p=self.probe
            if p and p[3]==state and not p[9] and (selection is None or p[5]==selection):
                return p
        raise AssertionError(('State timeout',state,self.probe))

    def tap(self,button,hold=2,release=6):
        self.hold([button],hold);self.hold([],release)

    def choose(self,state,selection):
        self.wait_for(state)
        for _ in range(8):
            if self.probe[5]==selection:return
            self.tap(DOWN)
        raise AssertionError(('Menu focus',selection,self.probe))

    def start_level(self,level):
        self.wait_for(1);self.tap(A);self.wait_for(2)
        self.choose(2,0);self.tap(A);self.wait_for(3)
        self.choose(3,level-1);self.tap(A)
        p=self.wait_for(7)
        for page in range(p[8]):
            self.wait_for(7);self.tap(A)
        self.wait_for(8);self.tick(6)

    def heading(self):
        assert self.last_pose
        return self.last_pose[3]

    def turn_to(self,target,limit=100):
        for _ in range(limit):
            if self.pending_choice or not self.probe or self.probe[3]!=8:return
            angle=(target-self.heading()+180)%360-180
            if abs(angle)<=8:
                self.game_step([])
                return
            self.game_step([Y if angle>0 else A])
        raise AssertionError(('Turn failed',target,self.last_pose))

    def turn_precise(self,target,limit=240):
        for _ in range(limit):
            if self.pending_choice or self.probe[3]!=8:return
            angle=(target-self.heading()+180)%360-180
            if abs(angle)<=1:
                self.game_step([]);self.game_step([])
                if abs((target-self.heading()+180)%360-180)<=1:return
                continue
            button=Y if angle>0 else A
            if abs(angle)>18:self.game_step([button])
            else:
                self.game_step([]);self.game_step([])
                old=self.heading()
                for _pulse in range(8):
                    self.game_step([button])
                    if self.heading()!=old:break
                self.game_step([]);self.game_step([])
        raise AssertionError(('Quiet cardinal alignment failed',target,self.last_pose))

    def game_step(self,buttons):
        self.manual_boundary()
        if self.pending_choice:return
        before=self.flow[15] if self.flow and self.flow[3] else self.probe[41]
        self.hold(buttons,1)
        self.manual_boundary()
        if self.pending_choice:return
        for _ in range(12):
            if self.pending_choice:return
            now=self.flow[15] if self.flow and self.flow[3] else self.probe[41]
            if now!=before or self.probe[3]!=8:return
            self.tick()
            self.manual_boundary()

    def movement(self,dx,dz):
        h=math.radians(self.heading())
        # Forward=(-sin,+cos), right=(+cos,+sin).
        forward=-math.sin(h)*dx+math.cos(h)*dz
        right=math.cos(h)*dx+math.sin(h)*dz
        if abs(forward)>=abs(right):return UP if forward>0 else DOWN
        return RIGHT if right>0 else LEFT

    def movement_buttons(self,dx,dz,precise=False):
        h=math.radians(self.heading())
        forward=-math.sin(h)*dx+math.cos(h)*dz
        right=math.cos(h)*dx+math.sin(h)*dz
        threshold=1 if precise else max(5,max(abs(forward),abs(right))*.15)
        result=[]
        if abs(forward)>threshold:result.append(UP if forward>0 else DOWN)
        if abs(right)>threshold:result.append(RIGHT if right>0 else LEFT)
        return result or [self.movement(dx,dz)]

    def map_snapshot(self,data):
        p=struct.unpack('<64I',render.ram(self,self.map_address,256))
        if p[0]!=0x20504b44 or p[2]&1 or p[3]!=data.number or not p[25]:
            previous=getattr(self,'last_good_map',None)
            if previous and previous['level']==data.number and self.flow and previous['serial']==self.flow[4] and self.frames-previous['video_frame']<=24:
                stats=self.report.setdefault('map_snapshot_fallbacks',dict(count=0))
                stats.update(count=stats['count']+1,last_video_frame=self.frames,
                    last_age=self.frames-previous['video_frame'],scope='Retained coherent observer copy; native collision authoritative')
                return previous['maps']
            return None
        assert p[31]==0,('Actual map fault',p)
        previous=getattr(self,'last_good_map',None)
        if previous and self.flow and previous['level']==data.number and previous['serial']==self.flow[4] and previous.get('update_ordinal')==p[33]:
            previous['video_frame']=self.frames
            return previous['maps']
        mmap=bytearray(render.ram(self,p[25],data.width*data.height))
        tmap=bytearray(render.ram(self,p[24],data.width*data.height))
        glyphs=render.ram(self,p[27],1024)
        for cell,g in enumerate(tmap):
            obj=data.objects[g]
            if obj['class']==9 and glyphs[g*4+3]==4:tmap[cell]=32
            if obj['class']==7 and glyphs[g*4+2]==((~obj['marker'])&255):
                tmap[cell]=32
                if g not in {r['glyph'] for r in self.report.get('observed_open_doors',[])}:
                    self.report.setdefault('observed_open_doors',[]).append(dict(glyph=g,source_marker=obj['marker'],
                        observed_marker=glyphs[g*4+2],map_frame=p[5],video_frame=self.frames,
                        method='Complement marker published after OPEN completion; local observer LOS copy only'))
        for cell,g in enumerate(mmap):
            if data.objects.get(g,{}).get('class')==9 and glyphs[g*4+3]==4:mmap[cell]=32
        self.last_good_map=dict(level=data.number,serial=self.flow[4] if self.flow else None,
            video_frame=self.frames,update_ordinal=p[33],maps=(bytes(mmap),bytes(tmap)))
        return mmap,tmap

    def dynamic_monsters(self,data):
        maps=self.map_snapshot(data)
        return {i for i,g in enumerate(maps[0]) if data.objects.get(g,{}).get('class')==9} if maps else set()

    def assist_combat(self,data,dx,dz,buttons):
        if self.force_no_combat:return [b for b in buttons if b!=R]
        maps=self.map_snapshot(data)
        if not maps:return [b for b in buttons if b!=R]
        last=getattr(self,'last_ai_sample',-100000)
        if self.ai_reader and self.flow and self.flow[15]-last>=16:
            self.last_ai_sample=self.flow[15]
            sample=self.sample_ai(maps)
            if sample.get('accepted'):
                x,_,z,_=self.last_pose
                sample['actor_count']=len(sample['enemies'])
                sample['enemies']=sorted((e for e in sample['enemies'] if not e['corpse']),
                    key=lambda e:(e['x']-x)**2+(e['z']-z)**2)[:8]
            self.report.setdefault('advisory_ai_samples',[]).append(dict(video_frame=self.frames,
                hero_callbacks=self.flow[15],sample=sample))
        if data.number==1 and self.probe[51] and self.probe[49]:
            x,_,z,h=self.last_pose
            gauntlet=data.number==1 or (x>53*data.side and 48*data.side<z<55*data.side) or (49*data.side<x<55*data.side and 11*data.side<z<24*data.side)
            for target in aim_targets(data,*maps,x,z,limit_cells=2.8,excluded_targets=self.dying_targets()):
                if not gauntlet or (data.number==1 and field(data.objects[target['glyph']],0xa4)>24):continue
                tx,tz=[(q+.5)*data.side for q in target['xy']]
                denominator=math.hypot(dx,dz)*max(target['distance'],1)
                if not denominator or ((tx-x)*dx+(tz-z)*dz)/denominator<.8:continue
                angle=(target['heading']-h+180)%360-180
                tolerance=max(3,min(12,math.degrees(math.atan2(180,target['distance']))))
                result=[self.movement(dx,dz),L]
                result.append((Y if angle>0 else A) if abs(angle)>tolerance else R)
                return self.conserve_dying_fire(result,target)
        if data.number==1 and self.probe[51] and self.probe[49]:
            x,_,z,h=self.last_pose
            targets=aim_targets(data,*maps,x,z,limit_cells=5,excluded_targets=self.dying_targets())
            if not targets:return buttons
            target=targets[0];angle=(target['heading']-h+180)%360-180
            tolerance=max(3,min(12,math.degrees(math.atan2(180,target['distance']))))
            result=[b for b in buttons if b not in (Y,A,R)]
            if target['distance']<600:
                tx,tz=[(q+.5)*data.side for q in target['xy']]
                result=[self.movement(x-tx,z-tz),L]
            if abs(angle)>tolerance:result.append(Y if angle>0 else A)
            else:result.append(R)
            return self.conserve_dying_fire(result,target)
        x,_,z,_=self.last_pose
        guard_defense=(data.number==3 and not self.probe[50]&2 and x>54*data.side and 46*data.side<z<61*data.side)
        decision=choose_buttons(data,*maps,self.last_pose,
            render.signed32(self.probe[49]),self.probe[51],preferred=() if guard_defense else buttons,
            phase=self.flow[15] if self.flow else self.frames,limit_cells=7 if guard_defense else 5,
            excluded_targets=self.dying_targets())
        if not decision:return buttons
        x,_,z,h=self.last_pose
        tx,tz=[(q+.5)*data.side for q in decision['target']['xy']]
        length=math.hypot(dx,dz)*max(decision['target']['distance'],1)
        aligned=length and ((tx-x)*dx+(tz-z)*dz)/length>.8
        # A short source guard line blocks the LEVEL3 key2 chamber. Keep
        # ordinary forward pressure and fire through one guard, then enter
        # the real healing/item cells behind it instead of backing to a wall.
        rush=(False and data.number==3 and not self.probe[50]&2 and x>53*data.side
              and 48*data.side<z<55*data.side and aligned)
        if rush:
            result=[self.movement(dx,dz),L]
            if abs(decision['angle'])>decision['tolerance']:result.append(Y if decision['angle']>0 else A)
            else:result.append(R)
            decision['buttons']=result;decision['guard_line_push']=True
        if len(self.rows)%120==0:
            self.report.setdefault('combat_aim',[]).append(dict(frame=self.frames,**decision))
        return self.conserve_dying_fire(decision['buttons'],decision['target'])

    def conserve_dying_fire(self,buttons,target):
        # A dying actor remains a real LOS/collision blocker until native
        # retirement. Only release R; never clear its map cell or shoot through.
        if R in buttons and self.flow and self.flow[15]-getattr(self,'last_turn_input_callback',-10000)<=2:
            stats=self.report.setdefault('aim_turn_release_waits',dict(count=0))
            stats.update(count=stats['count']+1,last_video_frame=self.frames,
                method='Two actual hero callbacks with turn released before R; recompute target on each callback')
            return [button for button in buttons if button!=R]
        known=getattr(self,'advisory_dying',{})
        if R in buttons and self.flow and getattr(self,'advisory_dying_serial',None)==self.flow[4] and known.get(target['glyph'])==target['cell']:
            self.report.setdefault('dying_fire_releases',[]).append(dict(video_frame=self.frames,
                glyph=target['glyph'],cell=target['cell'],reason='Accepted advisory HP<=0; retain native map occupancy'))
            return [button for button in buttons if button!=R]
        return buttons

    def plan_current(self,data,sensor_flags,avoid=()):
        x,_,z,_=self.last_pose
        start=int(z)//data.side*data.width+int(x)//data.side
        inventory=self.probe[50]|sensor_flags|(16 if self.probe[51] else 0)
        pickups=pickup_lists(data);maps=self.map_snapshot(data)
        remaining=lambda rows:{r['cell'] for r in rows if not maps or maps[1][r['cell']]==r['glyph']}
        health=render.signed32(self.probe[48]);charge=render.signed32(self.probe[49])
        choices=[]
        if not inventory&16:choices.append(('weapon',data.weapons))
        else:
            if health<(60 if data.number==3 else 45) and (charge>=20 or health<25):choices.append(('health',remaining(pickups['health'])))
            if charge<60:choices.append(('charge',remaining(pickups['charge']+pickups['weapons'])))
            if health<75:choices.append(('health',remaining(pickups['health'])))
            if self.probe[51]==2 and (self.probe[50]&7)!=7:
                choices.append(('strong_weapon',remaining([r for r in pickups['weapons'] if r['slot']==1])))
        for kind,goals in choices:
            if not goals:continue
            try:prefix,_=data.route(start=start,inventory=inventory,targets=goals,required_weapon=False,avoid=avoid)
            except ValueError:continue
            limit=100 if kind=='charge' and charge<10 else 70 if (kind=='health' and health<25) or kind=='strong_weapon' else 35
            if kind!='weapon' and len(prefix)>limit:continue
            # A snapshot may still show a just-consumed pickup under the hero.
            if len(prefix)<=1:continue
            tail,_=data.route(start=prefix[-1][0],inventory=prefix[-1][1],avoid=avoid)
            self.report.setdefault('resource_detours',[]).append(dict(frame=self.frames,kind=kind,
                health=health,charge=charge,path=[data.xy(c) for c,f in prefix]))
            return prefix+tail[1:]
        return data.route(start=start,inventory=inventory,avoid=avoid)[0]

    def forced_current(self,data,goal,include_tail=True):
        current=Level(data.number)
        maps=self.map_snapshot(data)
        if not maps:
            previous=getattr(self,'last_good_map',None)
            if previous and previous['level']==data.number and self.flow and previous['serial']==self.flow[4] and self.frames-previous['video_frame']<=120:
                maps=previous['maps']
                self.report.setdefault('reviewed_map_fallbacks',[]).append(dict(video_frame=self.frames,
                    snapshot_frame=previous['video_frame'],age_video_frames=self.frames-previous['video_frame'],
                    reason='Current map publication unavailable; retained coherent observer copy, native collision authoritative'))
            else:raise ValueError('No coherent same-world published map snapshot for reviewed goal')
        mmap,tmap=maps
        opened={glyph for glyph,obj in current.objects.items() if obj['class']==7 and
            any(before==glyph and after==32 for before,after in zip(current.maps[1],tmap))}
        current.maps[0]=bytes(mmap);current.maps[1]=bytes(tmap)
        x,_,z,_=self.last_pose;start=int(z)//data.side*data.width+int(x)//data.side
        inventory=self.probe[50]|getattr(self,'sensor_flags',0)|(16 if self.probe[51] else 0)
        for glyph,bit in current.sensor_bits.items():
            if glyph in opened and glyph not in getattr(self,'sensor_observed_targets',set()):inventory|=bit
        actors={i for i,g in enumerate(mmap) if current.objects[g]['class']==9}-{start}
        prefix,_=current.route(start=start,inventory=inventory,targets={goal},required_weapon=False,
                               opened=opened,avoid=actors if self.force_avoid_monsters else ())
        # navigate begins at index 1. A goal already in the hero's current
        # cell still needs its own waypoint (centering/pickup/pause), rather
        # than silently skipping it and running the following main route.
        waypoints=prefix if len(prefix)>1 else prefix+prefix
        if not include_tail:return waypoints
        tail,_=current.route(start=prefix[-1][0],inventory=prefix[-1][1],opened=opened)
        return waypoints+tail[1:]

    def reviewed_path(self,data,goal,include_tail=True):
        try:return self.forced_current(data,goal,include_tail=include_tail)
        except ValueError as error:
            self.pause_reason=dict(kind='reviewed_path_block',goal=data.xy(goal),error=str(error))
            self.report.setdefault('reviewed_path_blocks',[]).append(dict(video_frame=self.frames,
                goal=data.xy(goal),pose=list(self.last_pose),error=str(error)))
            if self.probe[3]!=8:raise AssertionError(('World ended during reviewed path',self.probe[3]))
            self._in_review=True
            try:self.pending_choice=self.pause_review(data)
            finally:self._in_review=False
            return None

    def automatic_path(self,data,sensor_flags,avoid=()):
        try:return self.plan_current(data,sensor_flags,avoid)
        except ValueError:
            try:return self.plan_current(data,sensor_flags)
            except ValueError as error:
                if not self.review_pauses:raise
                self.report.setdefault('automatic_path_blocks',[]).append(dict(video_frame=self.frames,
                    pose=list(self.last_pose),error=str(error)))
                self.pause_reason=dict(kind='automatic_path_block',error=str(error))
                self.pending_choice=self.pause_review(data)
                return None

    def navigate(self,level,cell_limit=None):
        data=Level(level);self.level_data=data
        first,_=data.route(targets=data.weapons)
        rest,_=data.route(start=first[-1][0],inventory=first[-1][1])
        path=first+rest[1:]
        self.report['planned_path']=[dict(xy=data.xy(c),flags=f) for c,f in path]
        self.report['source_map_pins']=data.pins
        self.report['replans']=[]
        sensor_flags=0;self.sensor_flags=0;self.sensor_observed_targets=set()
        previous_floor=data.maps[2][data.start]
        completed=0;idx=1;retry=self.flow[14] if self.flow else 0
        replans=0;last_health=render.signed32(self.probe[48]);forced_goal=None;pause_after_goal=False;path_prefix_only=False
        if getattr(self,'review_start',False):
            self.pause_reason=dict(kind='review_start')
            self.pending_choice=self.pause_review(data)
        while idx<len(path):
            if self.pending_choice:
                choice=self.pending_choice;self.pending_choice=None
                if choice.get('goal'):
                    gx,gz=choice['goal'];forced_goal=gz*data.width+gx
                    pause_after_goal=bool(choice.get('pause_after_goal'))
                    new=self.reviewed_path(data,forced_goal,include_tail=not pause_after_goal)
                    if new is None:continue
                    path=new;idx=1;path_prefix_only=pause_after_goal
                elif path_prefix_only and forced_goal is None:
                    new=self.automatic_path(data,sensor_flags,self.dynamic_monsters(data))
                    if new is None:continue
                    path=new;idx=1;path_prefix_only=False
            if cell_limit and completed>=cell_limit:break
            cell,flags=path[idx]
            tx,tz=[(q+.5)*data.side for q in data.xy(cell)]
            self.navigation_goal=dict(index=idx,xy=data.xy(cell),completed=completed,replans=replans)
            tile=data.objects[data.maps[1][cell]]
            is_door=tile['class']==7
            if is_door:
                x,_,z,_=self.last_pose
                desired=(270 if tx>x else 90) if abs(tx-x)>=abs(tz-z) else (0 if tz>z else 180)
                self.turn_to(desired)
            old=None;stalled=0;replan=False;override=False;best=1e30;unchanged=0
            for attempt in range(320):
                if self.probe[3]!=8:
                    if self.probe[3]==10:
                        self.report['completed_playthrough']=True
                        break
                    raise AssertionError(('Gameplay interrupted',self.probe[3],idx,self.last_pose))
                current_health=render.signed32(self.probe[48])
                if self.pending_choice:override=True;break
                if self.review_pauses and not self.explicit_health_reserve and current_health>self.review_health+30:self.review_health=60
                resource_alarm=data.number==3 and current_health>0 and (last_health>=60>current_health or last_health>=25>current_health)
                last_health=current_health
                if self.flow and self.flow[14]!=retry:
                    previous_retry=retry;retry=self.flow[14]
                    if self.review_pauses:
                        self.pause_reason=dict(kind='native_retry',retry_before=previous_retry,retry_after=retry,
                            method='Actual native respawn; request real Start pause before further navigation')
                        self.pending_choice=self.pause_review(data)
                        override=True;break
                    replan=True;break
                if resource_alarm:replan=True;break
                x,_,z,h=self.last_pose
                actual_cell=int(z)//data.side*data.width+int(x)//data.side
                floor=data.maps[2][actual_cell]
                if floor!=previous_floor and actual_cell in data.sensors:
                    sensor_flags^=data.sensor_bits[data.sensors[actual_cell]]
                    self.sensor_observed_targets.add(data.sensors[actual_cell])
                    self.report.setdefault('observed_sensor_entries',[]).append(dict(video_frame=self.frames,
                        cell=data.xy(actual_cell),floor=floor,expected_open_bits=sensor_flags,
                        method='Actual committed hero floor transition; source toggle prediction, native collision authoritative'))
                previous_floor=floor;self.sensor_flags=sensor_flags
                dx,dz=tx-x,tz-z
                if abs(dx)<70 and abs(dz)<70:
                    completed+=1
                    if cell==forced_goal:
                        self.force_no_combat=False;forced_goal=None
                        if pause_after_goal:
                            pause_after_goal=False
                            self.pause_reason=dict(kind='goal_reached',goal=data.xy(cell))
                            # Let the native floor/pickup callback complete before
                            # requesting the ordinary pause menu.
                            self._in_review=True
                            try:
                                self.game_step([])
                                if self.probe[3]==8:self.pending_choice=self.pause_review(data)
                            finally:self._in_review=False
                            override=bool(self.pending_choice)
                    break
                metric=abs(dx)+abs(dz)
                if metric<best-5:best=metric;unchanged=0
                else:unchanged+=1
                if old==(x,z):stalled+=1
                else:stalled=0
                old=(x,z)
                if stalled==6 or unchanged==15:
                    maps=self.map_snapshot(data)
                    threats=aim_targets(data,*maps,x,z,limit_cells=5) if maps else []
                    if not threats:
                        desired=(270 if dx>0 else 90) if abs(dx)>=abs(dz) else (0 if dz>0 else 180)
                        if abs((desired-self.heading()+180)%360-180)>1:
                            self.turn_precise(desired);old=None;stalled=0;unchanged=0;continue
                buttons=self.movement_buttons(dx,dz,precise=stalled>3 or unchanged>8)
                if max(abs(dx),abs(dz))>240:buttons.append(L)
                if stalled>=12 and self.probe[49]:buttons.append(R)
                if is_door and attempt%12<4:buttons.append(X)
                buttons=self.assist_combat(data,dx,dz,buttons)
                self.game_step(buttons)
                if stalled==12:
                    desired=(270 if dx>0 else 90) if abs(dx)>=abs(dz) else (0 if dz>0 else 180)
                    self.turn_to(desired)
                if stalled>35 or unchanged>65:
                    replan=True;break
            else:replan=True
            if self.report['completed_playthrough']:break
            if self.probe[3]!=8:
                if self.probe[3]==10:self.report['completed_playthrough']=True;break
                raise AssertionError(('World ended before route replan',self.probe[3]))
            if override:continue
            if replan:
                replans+=1
                if replans>160:raise AssertionError(('Excess blocked-route replans',level,self.last_pose))
                x,_,z,_=self.last_pose
                start=int(z)//data.side*data.width+int(x)//data.side
                inventory=self.probe[50]|sensor_flags|(16 if self.probe[51] else 0)
                avoid=self.dynamic_monsters(data)-{start}
                if forced_goal is not None:
                    new=self.reviewed_path(data,forced_goal,include_tail=not pause_after_goal)
                    if new is None:continue
                else:
                    new=self.automatic_path(data,sensor_flags,avoid)
                    if new is None:continue
                self.report['replans'].append(dict(video_frame=self.frames,retry=retry,pose=list(self.last_pose),
                    inventory=inventory,avoided_monsters=[data.xy(c) for c in sorted(avoid)],
                    path=[dict(xy=data.xy(c),flags=f) for c,f in new]))
                path=new;idx=1;path_prefix_only=bool(forced_goal is not None and pause_after_goal);continue
            idx+=1
            if completed%25==0:
                if forced_goal is not None:
                    new=self.reviewed_path(data,forced_goal,include_tail=not pause_after_goal)
                    if new is None:continue
                else:
                    new=self.automatic_path(data,sensor_flags,self.dynamic_monsters(data)-{cell})
                    if new is None:continue
                path=new;idx=1;path_prefix_only=bool(forced_goal is not None and pause_after_goal)
            if completed%15==0:self.progress(route_index=idx,cells_reached=completed,cell=data.xy(cell))
            if completed%70==0:
                before=self.probe[41];self.touch=(210,175)
                self.game_step([]);self.touch=None;self.game_step([])
                assert self.probe[41]>before,'Gameplay stopped during map touch'
                self.report.setdefault('touch',[]).append(dict(video_frame=self.frames,hero_before=before,hero_after=self.probe[41],xy=[210,175]))
        self.hold([],12)
        self.report['route_cells_reached']=completed
        self.report['route_cells_total']=len(self.report['planned_path'])-1
        if cell_limit:
            self.report['bootstrap_only']=True
        else:
            if not self.report['completed_playthrough']:
                for _ in range(90):
                    self.tick()
                    if self.probe[3]==10:self.report['completed_playthrough']=True;break
            assert self.report['completed_playthrough'],('Path reached without production FINISH',self.last_pose,self.probe)
            self.report['finish_probe']=list(self.probe)
            self.report['finish_flow']=list(self.flow) if self.flow else None
            p=self.wait_for(10);self.tick(4)
            self.screens.append(self.capture('finish'))
            while self.probe[3]==10:self.tap(A)
            self.wait_for(3)
            self.report['level_select_after_finish']=list(self.probe)
            assert self.game and self.game[7]>0 and self.report['last_audio_probe'][0]==0x18504b44


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target',type=Path,required=True)
    p.add_argument('--core',type=Path,default=front.CORE)
    p.add_argument('--level',type=int,choices=(1,2,3),required=True)
    p.add_argument('--language',type=int,choices=(1,2),default=1)
    p.add_argument('--cell-limit',type=int)
    p.add_argument('--run-id',default='current')
    p.add_argument('--review-pauses',action='store_true')
    p.add_argument('--review-start',action='store_true',help='Real Start review checkpoint before ordinary navigation')
    args=p.parse_args();target=args.target.resolve()
    assert target.is_relative_to(ROOT/'build/nds/n22')
    directory=target/f'route-{args.language}-level{args.level}-{args.run_id}'
    directory.mkdir(parents=True,exist_ok=True)
    f=None;report=dict(task='N22',passed=False)
    try:
        assert transport.sha(args.core)==render.CORE_SHA
        f=RouteObserver(args.core,target,directory);report=f.report
        report['route_arguments']={key:str(value) if isinstance(value,Path) else value
            for key,value in vars(args).items()}
        f.review_pauses=args.review_pauses or args.review_start
        f.review_start=args.review_start
        image=f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image,front.settings_bytes(args.language))
        f.load(target/'dok_nds_gameplay.nds');transport.flush_native_stdio()
        before=volume_files(image)
        f.start_level(args.level);f.navigate(args.level,args.cell_limit)
        report['passed']=bool(report.get('bootstrap_only') or report['completed_playthrough'])
    except Exception:
        report['error']=traceback.format_exc()
        if f and f.last_video:report['failure_images']=f.capture('failure')
    finally:
        if f:
            f.close();transport.flush_native_stdio()
            report['last_probe']=list(f.probe) if f.probe else None
            if 'before' in locals():report['disk']=audit_changes(before,volume_files(image),['/DOK/SETTINGS.DAT'])
        (directory/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k in ('passed','error','completed_playthrough','route_cells_reached')},ensure_ascii=False))
    return 0 if report.get('passed') else 1

from route_profile import install_route_profiling
install_route_profiling(RouteObserver)

if __name__=='__main__':raise SystemExit(main())
