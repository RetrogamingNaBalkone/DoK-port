"""Replay recorded physical joypad events on a current N22 production ROM.

The source input report may belong to another ROM. This tool never writes
emulator RAM and makes no claim of deterministic pose equivalence across ROMs.
It records all current probes at an actual runtime failure.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import traceback

from run_routes import RouteObserver,ROOT,pin,front,transport,render
from frontend_fat_oracle import volume_files,audit_changes


def reconstruct_touches(recording):
    if all('touch' in row for row in recording['input_steps']):return [],{}
    source=[]
    for row in recording.get('source_snapshots',[]):
        if row['original']['path'].endswith('/run_routes.py'):
            path=ROOT/row['snapshot']['path']
            assert pin(path)==row['snapshot']
            text=path.read_text('utf8')
            assert 'self.touch=(210,175)' in text and 'self.game_step([]);self.touch=None;self.game_step([])' in text
            source.append(row['snapshot'])
    assert source,'Touch reconstruction requires the pinned source implementation'
    intervals=[];events={}
    for touch in recording.get('touch',[]):
        end=touch['video_frame'];before=touch['hero_before'];after=touch['hero_after']
        assert after==before+2,('Unexpected callback span',touch)
        candidates=[row for row in recording['input_steps'] if row['video_frame']<end]
        press=candidates[-1]
        assert press['buttons']==[] and end-press['video_frame']<24
        boundaries=[row for row in recording['rows'] if press['video_frame']<row['video_frame']<=end and row['hero_callbacks']>before]
        first=boundaries[0]
        assert first['hero_callbacks']==before+1
        release=first['video_frame']
        assert press['video_frame']<release<end
        events[press['video_frame']]=tuple(touch['xy']);events[release]=None
        intervals.append(dict(press_frame=press['video_frame'],release_frame=release,summary_frame=end,
                              xy=touch['xy'],hero_before=before,hero_release=first['hero_callbacks'],hero_after=after))
    return intervals,dict(source=source,events=events,
        method='Pinned source has touch press -> game_step([]) -> release -> game_step([]). Press is the last buttons=[] event before summary. Release is the first recorded committed hero callback greater than hero_before; summary confirms exactly two callbacks.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--source-report',type=Path,required=True)
    parser.add_argument('--run-id',default='fixed-input')
    parser.add_argument('--frames',type=int)
    parser.add_argument('--language',type=int,choices=(1,2),default=1)
    parser.add_argument('--core',type=Path,default=front.CORE)
    args=parser.parse_args()
    target=args.target.resolve();source=args.source_report.resolve()
    assert target.is_relative_to(ROOT/'build/nds/n22')
    assert source.is_relative_to(ROOT/'build/nds/n22') or source.is_relative_to(ROOT/'research/evidence/nds/n22')
    recording=json.loads(source.read_text('utf8'))
    inputs=recording['input_steps']
    events={row['video_frame']:row for row in inputs}
    touch_intervals,touch_reconstruction=reconstruct_touches(recording)
    assert len(events)==len(inputs),'Ambiguous multiple changes at one video frame'
    count=args.frames or max([row['video_frame'] for row in inputs]+[row['video_frame'] for row in recording['rows']])+128
    directory=target/('replay-'+args.run_id);directory.mkdir(parents=True,exist_ok=True)
    f=None;report=dict(task='N22',passed=False)
    try:
        assert transport.sha(args.core)==render.CORE_SHA
        f=RouteObserver(args.core,target,directory);report=f.report
        report.update(touch_replay_complete=all('touch' in row for row in inputs) or bool(touch_intervals),
                      touch_reconstructed_intervals=touch_intervals,touch_reconstruction=touch_reconstruction,replay_source=pin(source),source_rom=recording['rom'],requested_video_frames=count,
                      replay_driver=pin(Path(__file__)),completion_claim=False)
        saved=directory/'sources'/(transport.sha(Path(__file__))+'-'+Path(__file__).name)
        saved.write_bytes(Path(__file__).read_bytes());report['replay_driver_snapshot']=pin(saved)
        image=f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image,front.settings_bytes(args.language))
        f.load(target/'dok_nds_gameplay.nds');transport.flush_native_stdio();before=volume_files(image)
        for frame in range(count):
            assert f.frames==frame
            if frame in events:
                buttons=events[frame]['buttons']
                f.touch=tuple(events[frame]['touch']) if events[frame].get('touch') else None
                f.joypad=sum(1<<b for b in buttons)
                f.input_steps.append(dict(video_frame=frame,buttons=buttons))
            if frame in touch_reconstruction.get('events',{}):f.touch=touch_reconstruction['events'][frame]
            f.tick()
        report['passed']=True
    except Exception:
        report['error']=traceback.format_exc()
        if f:
            f.record_fault()
            if f.last_video:report['failure_images']=f.capture('failure')
    finally:
        if f:
            report['actual_video_frames']=f.frames
            report['last_probe']=list(f.probe) if f.probe else None
            report['last_flow']=list(f.flow) if f.flow else None
            f.close();transport.flush_native_stdio()
            if 'before' in locals():report['disk']=audit_changes(before,volume_files(image),['/DOK/SETTINGS.DAT'])
        (directory/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps({k:report.get(k) for k in ('passed','actual_video_frames','error')},ensure_ascii=False))
    return 0 if report.get('passed') else 1

if __name__=='__main__':raise SystemExit(main())
