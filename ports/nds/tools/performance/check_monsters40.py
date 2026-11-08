"""N23 LEVEL3 crowd route and explicit diagnostic RAM fixtures.

Hero spawn/HP are diagnostic fixtures; enemies, doors and projectiles are live.
The approach section also grants key8 and asserts that the k door opened.
The firstroom section surveys two early LEVEL1 rooms without claiming the
user's route or enemy history.
The optional quiet fixture resets burst clocks before every video frame; the
silent fixture disconnects combat audio notifications. Neither is gameplay.
"""
import argparse
import json
import struct
import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'flow'))
from check_death39_emulator import layout, put
from run_routes import RouteObserver, front, render, transport, X, LEFT
from route_profile import stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('target', type=Path)
    p.add_argument('--mode', choices=('live', 'quiet', 'silent'), default='live')
    p.add_argument('--frames', type=int, default=180)
    p.add_argument('--run-id', default='monsters40')
    p.add_argument('--section', choices=('crowd','shotgun','exit','approach','firstroom','lure'), default='crowd')
    p.add_argument('--angles',type=int,nargs='+',default=[0,45,90,135,180,225,270,315])
    p.add_argument('--rooms',nargs='+',choices=('south-room','east-room'),default=['south-room','east-room'])
    p.add_argument('--exact-heading',action='store_true',help='Diagnostic hero-heading RAM fixture for paired camera poses')
    args = p.parse_args()
    assert args.frames > 0 and args.run_id.replace('-', '').isalnum()
    target = args.target.resolve()
    elf = target / 'dok_nds_gameplay.elf'
    off = layout(elf)
    enemy = layout(elf, 'dok_enemy')
    door_offset = layout(elf, 'dok_interaction_door')
    map_offset = layout(elf, 'NdsMapRuntime')
    directory = target / (args.run_id + '-' + args.section + '-' + args.mode)
    directory.mkdir(exist_ok=False)
    f = RouteObserver(front.CORE, target, directory)
    f.options['melonds_homebrew_sdcard'] = 'disabled'
    report = dict(passed=False, scope=__doc__, mode=args.mode, section=args.section, samples=[], route=[])
    def read(at, fmt='<I'):
        return struct.unpack(fmt, render.ram(f, at, struct.calcsize(fmt)))[0]
    def face(angle):
        if args.exact_heading:
            f.hold([],8)
            put(f,base+off('game','hero','heading'),struct.pack('<h',angle))
            f.hold([],8)
            assert f.last_pose[3]==angle%360,(angle,f.last_pose)
        else:f.turn_to(angle)
    def sample(name, buttons=()):
        first = f.frames
        before = list(f.game)
        pose = f.last_pose
        load=[]
        f.joypad = sum(1 << b for b in buttons)
        for _ in range(args.frames):
            if args.mode == 'quiet':
                # Source ticks are 10000 Hz. Keep bursts below their threshold.
                for n in range(read(base + off('game', 'ai', 'count'))):
                    put(f, base + off('game', 'ai', 'enemies') + n * stride + enemy('burst_started'),
                        struct.pack('<I', f.probe[40]))
            f.tick()
            assert f.probe[3] == 8, ('Unexpected frontend', f.probe[3])
            if args.section=='lure':
                load.append(dict(video_frame=f.frames,active_projectiles=f.game[6],ai_active=f.game[5],shots=f.game[21],ranged=f.game[25]))
        f.joypad = 0
        rows = [r for r in f.report['profile_rows'] if r['observer_video_frame'] > first]
        history = [r for r in f.report['history_records'] if r['observer_video_frame'] > first]
        intervals = [(r['vblank_end'] - r['vblank_start']) & 0xffffffff for r in history]
        windows={}
        for seconds in (.25,.5,1):
            rates=[]
            for start in range(len(intervals)):
                ticks=0
                for end in range(start,len(intervals)):
                    ticks+=intervals[end]
                    if ticks>=f.av.timing.fps*seconds:
                        rates.append((end-start+1)*f.av.timing.fps/ticks);break
            windows[str(seconds)]=min(rates) if rates else None
        map_at=read(transport.elf_symbol(elf,'map_ui')[0])
        report['samples'].append(dict(name=name, pose=pose, final_pose=f.last_pose,
            static_cache={k:read(map_at+map_offset('discovery_cache',k)) for k in ('count','valid','hits','misses')},
            fps=len(intervals)*f.av.timing.fps/sum(intervals),
            minimum_window_fps=windows,
            intervals={str(n): intervals.count(n) for n in set(intervals)},
            stages_ms={k: stats([r[k]*1000/r['timer_hz'] for r in rows]) for k in
                ('cpu_excluding_wait','simulation','geometry','sprites','discovery',
                 'entry_audio','post_map_audio','effects','submit_including_wait','map')},
            callback_ticks={k: (f.game[i]-before[i]) & 0xffffffff for k,i in
                            (('ai',14),('combat',15),('hero',16))},
            shots=f.game[21]-before[21], ranged=f.game[25]-before[25],
            callbacks={k:f.game[i]-before[i] for k,i in (('ai',7),('combat',8),('hero',9))},
            active_projectiles=f.game[6], ai_active=f.game[5], ai_dead=f.game[23],
            peaks=sorted(rows,key=lambda r:r['cpu_excluding_wait'],reverse=True)[:8],
            captures=f.capture(name)))
        if load:report['samples'][-1]['load_samples']=load
        print(json.dumps({k:v for k,v in report['samples'][-1].items() if k in
                          ('name','fps','pose','final_pose','shots','ranged','callback_ticks','ai_active')}), flush=True)
    try:
        f.load(target / 'dok_nds_gameplay.nds')
        f.start_level(1 if args.section=='firstroom' else 3)
        base = read(transport.elf_symbol(elf, 'storage')[0])
        # DWARF member offset in the array's containing structure is independent
        # of the enemy size; the two adjacent array fields determine its size.
        stride = (off('game','ai','glyph_index') - off('game','ai','enemies')) // 128
        put(f, base + off('game','items','inventory','health'), struct.pack('<h',30000))
        if args.mode == 'silent':
            put(f, base + off('game','combat','services','sink'), bytes(4))
        def go(x,z,heading):
            if args.section=='lure':f.turn_precise(heading)
            else:f.turn_to(heading)
            for _ in range(260):
                dx=x*480+240-f.last_pose[0]; dz=z*480+240-f.last_pose[2]
                if max(abs(dx),abs(dz))<65:break
                f.game_step(f.movement_buttons(dx,dz,precise=args.section!='lure')+[X])
            else:raise AssertionError(('Route blocked',x,z,f.last_pose))
            f.hold([],8)
            report['route'].append(dict(cell=[x,z],pose=f.last_pose,ai_active=f.game[5],dead=f.game[23],shots=f.game[21]))
            print(json.dumps(report['route'][-1]),flush=True)
        # Fixture entry beyond the starting corridor; normal retry owns occupancy.
        # This is a cost reproduction, not the user's complete playthrough.
        def reposition(x,z,heading):
            for name,value,fmt in [('x',x*480+240,'<i'),('z',z*480+240,'<i'),('heading',heading,'<h')]:
                put(f,base+off('game','spawn',name),struct.pack(fmt,value))
            put(f,base+off('game','lives'),struct.pack('<h',3))
            put(f,base+off('game','items','inventory','health'),bytes(2))
            for _ in range(90):
                f.tick()
                if f.probe[3]==13:break
            assert f.probe[3]==13
            f.hold([],3);f.tap(front.A);f.wait_for(8);f.hold([],6)
            put(f,base+off('game','items','inventory','max_health'),struct.pack('<h',30000))
            put(f,base+off('game','items','inventory','health'),struct.pack('<h',30000))
        if args.section=='lure':
            room_glyphs=(128,129,130,160,161,162)
            executioner_glyphs=(194,195,196,197)
            def actors(glyphs):
                rows=[]
                for glyph in glyphs:
                    n=read(base+off('game','ai','glyph_index')+glyph*2,'<H')
                    assert n,('Missing enemy glyph',glyph)
                    at=base+off('game','ai','enemies')+(n-1)*stride
                    rows.append(dict(glyph=glyph,x=read(at+enemy('motion','x'),'<i'),
                        z=read(at+enemy('motion','z'),'<i'),health=read(at+enemy('health'),'<h'),
                        state=read(at+enemy('state'),'<B')))
                return rows
            reposition(60,56,180)
            put(f,base+off('game','items','inventory','keys'),struct.pack('<H',8))
            report['fixture_keys']=8
            report['initial_group']=actors(room_glyphs+executioner_glyphs)
            go(60,49,180)
            report['triggered_executioners']=actors(executioner_glyphs)
            assert all(e['state']>0 for e in report['triggered_executioners']),report['triggered_executioners']
            # The ordinary navigator can stall in the live crowd. Retry moves
            # only the hero/occupancy; enemy states and their history persist.
            reposition(57,51,90)
            report['lure_hero_repositions']=[[60,56],[57,51],[60,54]]
            door_stride=(off('game','interactions','previous_floor')-off('game','interactions','doors'))//256
            door_at=base+off('game','interactions','doors')+107*door_stride
            def door():
                return {k:read(door_at+door_offset(k),'<h' if k in ('phase','extent') else '<B')for k in ('phase','extent','state','ready')}
            # FCMap f triggers k on entry. Do not press Use a second time
            # while that automatic opening is already in progress.
            for _ in range(600):
                f.hold([],1)
                if door()['ready']:break
            if door()['state']==2:
                f.hold([X],30);f.hold([],8)
            for _ in range(600):
                f.hold([],1)
                d=door()
                if d['ready'] and d['state']==1 and d['phase']==d['extent']-1:break
            else:raise AssertionError(('Door did not finish opening',door()))
            report['open_door']=door()
            door_flags=read(base+off('game','collision','flags')+107*2,'<H')
            assert door_flags&0x10,('Key-room door did not open',door_flags)
            report['opened_door_flags']=door_flags
            reposition(60,54,135)
            report['lure_history']=[]
            for _ in range(30):
                f.hold([],60)
                rows=actors(room_glyphs+executioner_glyphs)
                report['lure_history'].append(dict(video_frame=f.frames,hero=f.last_pose,actors=rows,shots=f.game[21],door=door()))
                if all(e['x']//480>=57 for e in rows[:6]):break
            report['corridor_group']=actors(room_glyphs+executioner_glyphs)
            report['all_room_enemies_out']=all(e['x']//480>=57 for e in report['corridor_group'][:6])
            report['all_executioners_alive']=all(e['health']>0 for e in report['corridor_group'][6:])
            assert report['all_room_enemies_out'],('Room enemies stayed behind the door',report['corridor_group'])
            assert report['all_executioners_alive'],report['corridor_group']
            sample('corridor-firing-135')
            for angle in (90,180,45):
                face(angle);sample('corridor-firing-'+str(angle))
            report['final_group']=actors(room_glyphs+executioner_glyphs)
        elif args.section=='firstroom':
            # Independent cost fixtures around the early LEVEL1 rooms. These
            # do not claim to reproduce the user's route from the blaster.
            for name,x,z in [('south-room',46,54),('east-room',58,52)]:
                if name not in args.rooms:continue
                reposition(x,z,0)
                for angle in args.angles:
                    face(angle);sample(name+'-'+str(angle))
        elif args.section=='approach':
            # Explicit fixture: the user's route already owns the key8 needed
            # by k. The older crowd fixture never checked that Use opened it.
            reposition(60,56,180)
            go(60,47,180)
            sample('executioners')
            reposition(57,51,90)
            sample('closed-key-room')
            put(f,base+off('game','items','inventory','keys'),struct.pack('<H',8))
            f.hold([X],30);f.hold([],30)
            door_flags=read(base+off('game','collision','flags')+107*2,'<H')
            assert door_flags&0x10,('Key-room door did not open',door_flags)
            report['fixture_keys']=8;report['opened_door_flags']=door_flags
            sample('open-key-room')
            for angle in (45,135,180,270):
                face(angle);sample('door-angle-'+str(angle))
        elif args.section=='crowd':
            reposition(60,56,180)
            go(60,47,180)
            sample('executioners')
            # The awakened crowd can block retreat. Reposition through retry
            # without killing it. Without key8 this Use leaves k closed; the
            # approach section explicitly tests the open door separately.
            reposition(57,51,90);f.hold([X],30);f.hold([],8)
            sample('key-room')
            f.turn_to(270); sample('crowd-strafe',[LEFT])
        elif args.section=='shotgun':
            for name,x,z,angle in [('start-weapon',48,59,270),('north-weapon',18,30,270)]:
                reposition(x,z,angle);f.hold([],60);sample(name)
                f.turn_to((angle+180)%360);sample(name+'-reverse')
        else:
            reposition(37,19,180);f.hold([],120)
            sample('exit-crowd');f.turn_to(90);sample('exit-west')
            f.turn_to(270);sample('exit-east')
        report.update(passed=True,rom_sha256=transport.sha(target/'dok_nds_gameplay.nds'),
                      elf_sha256=transport.sha(elf),enemy_stride=stride,
                      original_balance_unchanged=True,exact_heading_fixture=args.exact_heading)
    except Exception:
        report['error']=traceback.format_exc()
        if f.last_video:report['failure_capture']=f.capture('failure')
        print(report['error'],flush=True)
    finally:
        f.close();transport.flush_native_stdio()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
