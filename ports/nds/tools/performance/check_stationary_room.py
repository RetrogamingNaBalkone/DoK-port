"""Sample stationary first-room views reached through ordinary input only."""
import argparse,json,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from run_routes import RouteObserver,ROOT,front,transport,render,pin
from route_profile import stats

def main():
    p=argparse.ArgumentParser();p.add_argument('--target',type=Path,required=True)
    p.add_argument('--cells',type=int,default=7);p.add_argument('--frames',type=int,default=120)
    p.add_argument('--angles',type=int,nargs='+',default=list(range(0,360,45)))
    p.add_argument('--point',type=int,nargs=2,help='World X/Z target reached with real movement keys')
    p.add_argument('--run-id',required=True);args=p.parse_args()
    target=args.target.resolve();directory=target/('stationary-'+args.run_id)
    assert not directory.exists();directory.mkdir()
    f=None;report=dict(passed=False);samples=[]
    try:
        f=RouteObserver(front.CORE,target,directory);report=f.report
        image=f.save/'melonDS DS/dldi_sd_card.bin';front.write_fixture(image,front.settings_bytes(1))
        f.load(target/'dok_nds_gameplay.nds');f.start_level(1);f.navigate(1,args.cells)
        if args.point:
            for _ in range(600):
                dx=args.point[0]-f.last_pose[0];dz=args.point[1]-f.last_pose[2]
                if max(abs(dx),abs(dz))<=48:break
                f.game_step(f.movement_buttons(dx,dz,precise=True))
            else:raise AssertionError(('Point approach blocked',args.point,f.last_pose))
            f.hold([],12)
        for angle in args.angles:
            assert f.probe[3]==8,('Gameplay ended',f.last_pose)
            f.turn_to(angle);f.hold([],12);start=f.last_pose;f.phase=f'stationary-{angle}'
            first=f.frames;f.tick(args.frames)
            assert f.last_pose[:3]==start[:3],('Hero moved in stationary sample',start,f.last_pose)
            history=[r for r in report['history_records']if r['observer_video_frame']>first]
            rows=[r for r in report['profile_rows']if r['observer_video_frame']>first]
            assert rows and history
            hz=f.av.timing.fps
            sample=dict(angle=angle,pose=list(start),health=render.signed32(f.probe[48]),
                frames=args.frames,presentations=len(history),fps=len(history)*hz/sum(r['vblank_end']-r['vblank_start']for r in history),
                stages_ms={k:stats([r[k]*1000/r['timer_hz']for r in rows])for k in ('cpu_excluding_wait','simulation','geometry','sprites','discovery','effects','submit_including_wait','map','map_raster','wait_vblank')},
                quads=stats([r['submitted_quads']for r in history]),geometry_wait=stats([r['geometry_wait_ticks']*1000/33513982 for r in history]),
                upload_bytes=sum(r['upload_bytes']for r in history),captures=f.capture('angle-'+str(angle)))
            samples.append(sample);(directory/'samples.json').write_text(json.dumps(samples,indent=2)+'\n','utf8')
            f.phase='turn'
        report.update(passed=True,samples=samples,arguments={k:str(v)if isinstance(v,Path)else v for k,v in vars(args).items()},
            observer=pin(Path(__file__)),scope='Real-input stationary LEVEL1 views; no model/VRAM writes; emulator only')
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close();transport.flush_native_stdio()
        (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=report['passed'],samples=[dict(angle=s['angle'],pose=s['pose'],fps=s['fps'])for s in samples],error=report.get('error'))))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
