"""Bounded adaptive ordinary-input samples on N23; no emulator memory writes."""
import argparse
import json
from pathlib import Path
import sys
import threading
import time
import traceback
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from run_routes import RouteObserver,ROOT,front,transport,render,pin,START
from frontend_fat_oracle import audit_changes,volume_files

class SampleComplete(Exception):pass

class Observer(RouteObserver):
    def progress(self,**extra):
        if self.game:
            extra.setdefault('ai_active',self.game[5])
        extra.setdefault('max_naturally_active_ai',getattr(self,'max_ai',0))
        return super().progress(**extra)

    def tick(self,count=1):
        for _ in range(count):
            if self.frames>=getattr(self,'video_limit',10**9):raise SampleComplete('video_budget')
            super().tick()
            if self.game:self.max_ai=max(getattr(self,'max_ai',0),self.game[5])
        return self.probe

    def _pause_review_once(self,data):
        if self.external_review:
            try:
                return super()._pause_review_once(data)
            except AssertionError as error:
                if error.args and isinstance(error.args[0],tuple)and error.args[0][0]=='Reviewed route stopped':
                    raise SampleComplete('parent_stopped_sample')
                raise
        ordinal=len(self.report.get('review_pauses',[]))+1
        point=self.directory/f'checkpoint-{ordinal}.json'
        command=self.directory/f'command-{ordinal}.json'
        failure=[]
        def publish():
            try:
                for _ in range(6000):
                    if point.exists():break
                    if self.publisher_stop.is_set():return
                    time.sleep(.01)
                else:raise AssertionError('Host policy checkpoint timeout')
                saved=json.loads(point.read_text('utf8'))
                choice=self.policy_choice(saved)
                choice.update(checkpoint=ordinal,level=data.number)
                temporary=command.with_suffix('.tmp');temporary.write_text(json.dumps(choice)+'\n','utf8');temporary.replace(command)
            except Exception:failure.append(traceback.format_exc())
        thread=threading.Thread(target=publish,daemon=True);thread.start()
        try:
            return super()._pause_review_once(data)
        except AssertionError as error:
            if error.args and isinstance(error.args[0],tuple)and error.args[0][0]=='Reviewed route stopped':raise SampleComplete(getattr(self,'policy_end','reviewed_goals_completed'))
            raise
        finally:
            self.publisher_stop.set();thread.join(timeout=1);self.publisher_stop.clear()
            assert not failure,failure

    def policy_choice(self,saved):
        """Keep an intent until reached; an alarm never consumes a key goal."""
        reason=saved['reason'];kind=reason['kind'];keys=saved['flow'][24]
        weapon=saved['frontend'][51]
        if kind=='native_retry':
            if keys&3==3:
                self.pressure_route=True;self.stage=0
            else:
                # Keys/inventory survive the real retry. Leave an already
                # acquired key2 chamber by the perimeter, rather than enter it again.
                self.stage=5 if keys&2 else (1 if weapon else 0)
            self.blocked=0
        elif kind=='goal_reached':
            route=self.pressure_stages if self.pressure_route else self.early_stages
            if self.stage<len(route)and reason.get('goal')==route[self.stage]['goal']:
                self.stage+=1;self.blocked=0
        if not self.pressure_route:
            if keys&3==3:self.stage=max(self.stage,7)
            elif keys&2:self.stage=max(self.stage,4)
        route=self.pressure_stages if self.pressure_route else self.early_stages
        if self.stage>=len(route):
            self.policy_end='bounded_pressure_route_completed'if self.pressure_route else'bounded_key_and_W_route_completed'
            choice=dict(abort=True)
        elif kind=='reviewed_path_block':
            self.blocked+=1
            if self.blocked>4:
                self.policy_end='unresolved_blocked_goal';choice=dict(abort=True)
            else:
                # Advance the live AI with physical empty input, preserving
                # the goal. Do not loop on an unchanged paused map indefinitely.
                choice=dict(wait_callbacks=8,health_reserve=0,ammo_reserve=0)
        else:
            stage=route[self.stage]
            choice=dict(goal=stage['goal'],pause_after_goal=True,
                avoid_monsters=True,combat=stage.get('combat',False),health_reserve=0,ammo_reserve=0)
        self.report.setdefault('host_policy_choices',[]).append(dict(checkpoint=saved['ordinal'],reason=kind,
            keys=keys,weapon=weapon,pressure_route=self.pressure_route,stage=self.stage,choice=choice.copy()))
        return choice

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--level',type=int,choices=(1,2,3),required=True)
    parser.add_argument('--language',type=int,choices=(1,2),default=1)
    parser.add_argument('--cells',type=int,default=180)
    parser.add_argument('--video-budget',type=int,default=18000)
    parser.add_argument('--west-pressure',action='store_true')
    parser.add_argument('--review-external',action='store_true',help='Real native Start checkpoints controlled by review_commands.py')
    parser.add_argument('--ai-layout',type=Path,help='Optional read-only binding for this exact ELF')
    parser.add_argument('--run-id',required=True)
    args=parser.parse_args();target=args.target.resolve()
    assert target.is_relative_to(ROOT/'build/nds/n23')
    directory=target/f'ordinary-level{args.level}-{args.run_id}';assert not directory.exists();directory.mkdir()
    report=dict(task='N23',passed=False);f=None
    try:
        f=Observer(front.CORE,target,directory);report=f.report
        if args.ai_layout:
            from read_live_ai import LiveAiReader
            source=args.ai_layout.resolve();f.ai_reader=LiveAiReader(target/'dok_nds_gameplay.elf',source)
            destination=directory/'sources'/(pin(source)['sha256']+'-'+source.name);destination.write_bytes(source.read_bytes())
            report['ai_layout_binding']=dict(source=pin(source),snapshot=pin(destination))
        arguments={key:str(value) if isinstance(value,Path) else value for key,value in vars(args).items()}
        report.update(task='N23',completion_claim=False,sample_arguments=arguments,runner=pin(Path(__file__)))
        runner_copy=directory/'sources'/(transport.sha(Path(__file__))+'-'+Path(__file__).name)
        runner_copy.write_bytes(Path(__file__).read_bytes());report['runner_snapshot']=pin(runner_copy)
        f.publisher_stop=threading.Event();f.external_review=args.review_external
        f.pressure_route=False;f.stage=0;f.blocked=0
        f.early_stages=[dict(goal=xy,combat=(i==1))for i,xy in enumerate(
            [[36,60],[48,58],[52,58],[52,52],[58,48],[60,27],[51,8],[50,13],[49,14],[48,15],[13,24],[13,26],[17,30]])]
        f.pressure_stages=[dict(goal=xy)for xy in [[3,58],[17,30],[4,35],[4,22],[13,22]]]
        image=f.save/'melonDS DS/dldi_sd_card.bin';front.write_fixture(image,front.settings_bytes(args.language))
        f.load(target/'dok_nds_gameplay.nds');transport.flush_native_stdio();before=volume_files(image)
        f.start_level(args.level);f.video_limit=f.frames+args.video_budget
        if args.west_pressure:assert args.level==3
        if args.west_pressure or args.review_external:f.review_pauses=True;f.review_start=True
        try:f.navigate(args.level,None if args.west_pressure or args.review_external else args.cells)
        except SampleComplete as end:report['sample_end']=str(end)
        report['passed']=True
    except Exception:
        report['error']=traceback.format_exc()
        if f:
            import struct
            address,size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_effect_probe')
            report['failure_effect_probe']=list(struct.unpack('<32I',render.ram(f,address,size)))
            address,size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_sprite_probe')
            fields=render.scalar_fields(ROOT/'ports/nds/platform/sprites_probe/sprites_probe.h','NdsSpriteProbe')
            report['failure_sprite_probe']=dict(zip(fields,struct.unpack('<'+'I'*(size//4),render.ram(f,address,size))))
        if f and f.last_video:report['failure_images']=f.capture('failure')
    finally:
        if f:
            # Disable the host-only stop before flushing the observer rings.
            f.video_limit=10**9
            report.update(actual_video_frames=f.frames,max_naturally_active_ai=getattr(f,'max_ai',0),last_probe=list(f.probe)if f.probe else None,
                last_flow=list(f.flow)if f.flow else None)
            f.close();transport.flush_native_stdio()
            if 'before'in locals():report['disk']=audit_changes(before,volume_files(image),['/DOK/SETTINGS.DAT'])
        (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:report.get(k)for k in('passed','sample_end','error','actual_video_frames','max_naturally_active_ai')}))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
