"""Read-only LEVEL1 turn workload on a production ROM, with native map oracle."""
import argparse,json,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_joint_frontend_emulator as joint
front=joint.front

def stats(values):
    values=sorted(values)
    return dict(samples=len(values),mean=sum(values)/len(values),p95=values[int((len(values)-1)*.95)],maximum=values[-1]) if values else None

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();directory=target/'turning-observer-v2';directory.mkdir(exist_ok=True)
    f=None;report=dict(passed=False)
    try:
        f=joint.JointFrontendObserver(front.CORE,target,directory);report=f.report
        f.options['melonds_homebrew_sdcard']='disabled'
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8);f.wait_for(8)
        report['workload_start']=f.frames
        # NDS turns with Y/A; D-pad left/right strafes.
        for phase,keys,frames in [('idle',0,120),('right',1<<front.Y,480),('left',1<<front.A,480),('settle',0,60)]:
            f.phase=phase;f.joypad=keys;f.tick(frames)
            assert f.probe[3]==8,('Left gameplay during workload',phase,f.probe)
            f.capture(phase)
        f.joypad=0
        summaries={}
        for phase in ('idle','right','left','settle'):
            rows=[r for r in report['profile_rows']if r['phase']==phase]
            history=[r for r in report['history_records']if r['phase']==phase]
            hz=f.av.timing.fps
            summaries[phase]=dict(presentations=len(history),fps=len(history)*hz/sum(r['vblank_end']-r['vblank_start']for r in history),
                cpu_ms=stats([r['cpu_excluding_wait']*1000/r['timer_hz']for r in rows]),
                map_raster_ms=stats([r['map_raster']*1000/r['timer_hz']for r in rows if r['map_raster']]),
                map_total_ms=stats([r['map_total']*1000/r['timer_hz']for r in rows]),
                maximum_vblanks=max(r['vblank_end']-r['vblank_start']for r in history))
        report.update(passed=True,summary=summaries,core_sha256=front.transport.sha(front.CORE),
            scope='Same video-duration real joypad workload, no RAM writes. Simulation poses may differ across ROMs; FPS is workload evidence, not an identical-input raster microbenchmark.')
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close();front.transport.flush_native_stdio()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:report.get(k)for k in ('passed','summary','error')}))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
