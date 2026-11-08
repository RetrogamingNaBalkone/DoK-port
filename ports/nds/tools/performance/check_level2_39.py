"""LEVEL2 cost fixtures via temporary respawn positions, never a playthrough.

The normal retry helper moves occupancy and the hero. Native AI/effects remain
active. This isolates views without claiming the user's preceding AI history.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from check_death39_emulator import layout,put
from run_routes import RouteObserver,front,render,transport
from route_profile import stats


def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path)
    parser.add_argument('--frames',type=int,default=90)
    parser.add_argument('--angles',type=int,nargs='+',default=[0,60,120,180,240,300])
    parser.add_argument('--room-only',action='store_true');parser.add_argument('--run-id',default='level2-39')
    args=parser.parse_args();assert args.frames>0 and args.run_id.replace('-','').isalnum()
    target=args.target.resolve();elf=target/'dok_nds_gameplay.elf';offset=layout(elf)
    map_offset=layout(elf,'NdsMapRuntime')
    directory=target/args.run_id;directory.mkdir(exist_ok=True)
    f=RouteObserver(front.CORE,target,directory);f.options['melonds_homebrew_sdcard']='disabled'
    samples=[];report=dict(passed=False,scope=__doc__,samples=samples)
    try:
        f.load(target/'dok_nds_gameplay.nds');f.start_level(2)
        storage=struct.unpack('<I',render.ram(f,transport.elf_symbol(elf,'storage')[0],4))[0]
        spawn=storage+offset('game','spawn');health=storage+offset('game','items','inventory','health')
        lives=storage+offset('game','lives');saved=render.ram(f,spawn,48)
        points=[('baba-ammo',15,42)] if args.room_only else [('river-north',11,18),('river-south',11,40),('baba-ammo',15,42)]
        for name,x,z in points:
            for angle in args.angles:
                put(f,storage+offset('game','spawn','x'),struct.pack('<i',x*480+240))
                put(f,storage+offset('game','spawn','y'),struct.pack('<i',-300))
                put(f,storage+offset('game','spawn','z'),struct.pack('<i',z*480+240))
                put(f,storage+offset('game','spawn','heading'),struct.pack('<h',angle));put(f,lives,struct.pack('<h',3))
                put(f,health,struct.pack('<h',0));retries=f.flow[14]
                for _ in range(60):
                    f.tick()
                    if f.probe[3]==13:
                        f.joypad=0;f.tick(3);f.tap(front.A)
                    if f.flow[14]>retries:break
                assert f.flow[14]>retries,('Retry failed',f.flow)
                put(f,spawn,saved);f.hold([],8)
                first=f.frames;pose=f.last_pose;f.tick(args.frames)
                rows=[r for r in f.report['profile_rows']if r['observer_video_frame']>first]
                history=[r for r in f.report['history_records']if r['observer_video_frame']>first]
                assert f.probe[3]==8,(name,'unexpected death',f.flow)
                sample=dict(name=name,angle=angle,pose=pose,final_pose=f.last_pose,
                    fps=len(history)*f.av.timing.fps/sum(r['vblank_end']-r['vblank_start']for r in history),
                    stages_ms={k:stats([r[k]*1000/r['timer_hz']for r in rows])for k in
                        ('cpu_excluding_wait','simulation','geometry','sprites','discovery','effects','submit_including_wait','map','map_raster')},
                    quads=stats([r['submitted_quads']for r in history]))
                intervals=[r['vblank_end']-r['vblank_start']for r in history]
                sample['intervals']={str(n):intervals.count(n)for n in set(intervals)}
                hz=f.av.timing.fps
                for seconds in (.5,1):
                    windows=[]
                    for start in range(len(intervals)):
                        total=0
                        for end in range(start,len(intervals)):
                            total+=intervals[end]
                            if total>=hz*seconds:
                                windows.append((end-start+1)*hz/total);break
                    sample[f'min_fps_{seconds}s']=min(windows) if windows else None
                sample['peaks']=sorted(rows,key=lambda r:r['cpu_excluding_wait'],reverse=True)[:8]
                map_at=struct.unpack('<I',render.ram(f,transport.elf_symbol(elf,'map_ui')[0],4))[0]
                sample['static_cache']={k:struct.unpack('<I',render.ram(f,map_at+map_offset('discovery_cache',k),4))[0]
                                        for k in ('count','valid','hits','misses')}
                if angle in (0,180):sample['captures']=f.capture(name+'-'+str(angle))
                samples.append(sample)
        report.update(passed=True,rom_sha256=transport.sha(target/'dok_nds_gameplay.nds'))
    finally:
        f.close();transport.flush_native_stdio()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,samples=[dict(name=s['name'],angle=s['angle'],fps=s['fps'],cpu=s['stages_ms']['cpu_excluding_wait']['mean'])for s in samples])))


if __name__=='__main__':main()
