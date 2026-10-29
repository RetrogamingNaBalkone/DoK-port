"""N10 frontend-only controls and read-only ARM9 geometry/world verification.

Actual original x86 instructions independently verify live motion, collision,
MMap and door counters. No guest RAM writes, savestates or clock injection.
"""
from __future__ import annotations
import argparse
import ctypes as C
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import sys
import traceback

import make_assets_fixture as fat
import run_diag_emulator as transport
from run_movement_emulator import fields, observe, ram, u64, signed, trunc, native, verify_memory

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/nds/n10'
CORE_SHA = 'c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef'
PACKAGE_SHA = '2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511'
BUTTONS = {name: 1 << retro for name, retro, _ in transport.BUTTONS}
SIGNED = {'x','y','z','heading','angle_parameter','speed','distance','turn_step','linear_step',
          'last_turn','movement_accumulator','turn_accumulator','accel_sign','upper_y','lower_y'}


def safe_output(path):
    path = Path(path).absolute()
    if '..' in path.parts or path == OUT or not path.is_relative_to(OUT):
        raise ValueError('N10 output must stay under build/nds/n10')
    for item in (path, *path.parents):
        if item == ROOT: break
        if item.is_symlink() or (item.exists() and item.is_junction()): raise ValueError('N10 output alias')
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1): raise ValueError('N10 output is not a regular unique file')
    return path


def directory(path):
    safe_output(path / 'reserved')
    if path.exists():
        for item in path.rglob('*'):
            if item.is_symlink() or item.is_junction(): raise ValueError('N10 emulator directory alias')
            if item.is_file() and item.stat().st_nlink != 1: raise ValueError('N10 emulator hardlink')
    path.mkdir(parents=True, exist_ok=True)


def capture(frontend, stem):
    for suffix in ('.png','-top.png','-bottom.png'): safe_output(frontend.directory / (stem + suffix))
    return frontend.capture(stem)


def video(frontend, packet, level):
    width,height,rgb = frontend.rgb()
    assert (width,height) == (256,384), 'N10 dual screen dimensions'
    count = struct.unpack_from('<I',packet,12)[0]
    base = 64 + count * 64 + (level-1)*10240
    palette = struct.unpack_from('<256H',packet,base)
    indices = packet[base+512:base+10240]
    actual=[]
    for offset in range(154*256*3,192*256*3,3):
        r,g,b=rgb[offset:offset+3]
        actual.append((r>>3)|((g>>3)<<5)|((b>>3)<<10))
    assert actual == [palette[index]&0x7fff for index in indices], 'N10 accepted HUD pixels differ'
    scene=bytes(rgb[:154*256*3])
    assert len(set(scene))>1, 'N10 scene blank'
    return dict(hud_rgb555_pixels=len(actual),scene_rgb_sha256=hashlib.sha256(scene).hexdigest())


class Route:
    def __init__(self):
        self.level=0; self.phase='waiting'; self.since=0; self.events=[]
        self.latest_input=None; self.latest_callback=None
    def input(self, frame, probe):
        if probe is None or probe['status'] in (0,1,4,5): return 0,None
        if probe['level_id'] != self.level:
            self.level=probe['level_id']; self.phase='route'; self.since=frame
        active=probe['frame_id']; age=frame-self.since
        held=BUTTONS['R']|BUTTONS['X']|BUTTONS['B']
        if self.phase=='route' and active>=95:
            self.phase='pause-press'; self.since=frame
        if self.phase=='pause-press' and probe['status']==3:
            self.phase='paused'; self.since=frame
            self.events.append(dict(kind='paused',level=self.level,frontend_frame=frame))
        elif self.phase=='paused' and probe['status']==2:
            assert age>=40, 'N10 held Start resumed without release'
            self.phase='suppressed'; self.since=frame
            self.events.append(dict(kind='resumed',level=self.level,frontend_frame=frame))
        age=frame-self.since
        if self.phase=='pause-press': return held|BUTTONS['Start'],(64,80)
        if self.phase=='paused': return held|(BUTTONS['Start'] if age<20 or age>=45 else 0),(64+min(age,40),80)
        if self.phase=='suppressed':
            if age<16: return held,(104,80)
            if age<24: return 0,None
            if age<30: return held,(120,100)
            self.phase='after'; self.since=frame
        if active<10: return 0,None
        if active<40: return BUTTONS['Up']|BUTTONS['L']|BUTTONS['R'],None
        if active<52: return BUTTONS['Y']|BUTTONS['Right'],None
        if active<65: return BUTTONS['Down']|BUTTONS['A'],None
        if active<78: return BUTTONS['Left'],None
        if active<80: return BUTTONS['X'],None
        if active<84: return 0,(80,60)
        if active<86: return BUTTONS['B'],(120,90)
        if active<90: return BUTTONS['Select'],None
        if active<95: return 0,None
        if 130<=active<160: return BUTTONS['Up']|BUTTONS['L'],None
        if 160<=active<175: return BUTTONS['Right']|BUTTONS['A'],None
        return 0,None


def snapshot(frontend, probe, row, names):
    ordinal=row['snapshot_ordinal']; slot=(ordinal-1)%4
    assert ordinal and row['snapshot_slot']==slot, 'N10 snapshot link invalid'
    address=probe['snapshot_address']+slot*17728
    first=ram(frontend,address+12,4)
    data=ram(frontend,address,17728)
    last=ram(frontend,address+12,4)
    h=dict(zip(names,struct.unpack('<16I',data[:64])))
    if first!=last or struct.unpack('<I',first)[0]!=h['sequence'] or h['sequence']&1:
        raise AssertionError('N10 snapshot seqlock unstable at published trace')
    assert (h['magic'],h['abi_version'],h['size_bytes'],h['ordinal'],h['trace_ordinal']) == (0x30534b44,1,17728,ordinal,row['ordinal']), 'N10 snapshot ABI/link differs'
    for key in ('level_id','world_epoch','callback_total','hero_glyph','hero_id','hero_generation'):
        assert h[key]==row[key], ('N10 snapshot/trace identity',key)
    return data


def decode_snapshot(data):
    maps=[data[64+4096*p:64+4096*(p+1)] for p in range(4)]
    flags=list(struct.unpack_from('<256H',data,16448))
    counters=list(struct.unpack_from('<256H',data,16960))
    known=data[17472:17728]
    return maps,flags,counters,known


def verify_trace(rows, snapshots):
    from audit_collision import Original, fixture
    from audit_movement import FIELDS
    ratio=Fraction(1193182*64,119*33513982)
    previous=None; original=None; current=None; current_level=0
    presses,releases,held,uses,blocked,touch_blocked=[0]*12,[0]*12,0,0,0,False
    callbacks=[]; history=[]; pauses=[]; resumes=[]; raw_samples=[]; levels={}
    clock_checks=input_checks=camera_checks=world_checks=0
    all_maps_bytes=all_counter_bytes=0
    for row in rows:
        if previous is not None and row['action']!=6:
            delta=u64(row,'clock_last_counter')-u64(previous,'clock_last_counter')
            assert delta>=0, 'N10 raw clock regressed'
            ticks,fraction=previous['native_now'],u64(previous,'fraction')
            if not previous['paused']:
                whole,fraction=divmod(delta*ratio.numerator+fraction,ratio.denominator)
                ticks=(ticks+whole)&0xffffffff
            assert (ticks,fraction)==(row['native_now'],u64(row,'fraction')), ('N10 rational clock',row['ordinal'])
            clock_checks+=1
        action=row['action']
        if action==6:
            current_level=row['level_id']
            assert current_level not in levels and current_level in (1,2,3), 'N10 level sequence'
            f=fixture('ru',current_level)
            maps,flags,counters,known=decode_snapshot(snapshots[row['snapshot_ordinal']])
            model=json.loads((ROOT/f'research/evidence/nds/n05/oracle/level{current_level}-lang1.json').read_text('utf8'))
            assert maps==[bytes.fromhex(x) for x in model['world']['maps']], 'N10 initial maps differ from N05 source world'
            assert flags==f['flags'] and known==bytes(f['known']) and counters==[0]*256, 'N10 constructor collision fields differ'
            f['maps']=[bytearray(p) for p in maps[:3]]
            original=Original('ru',f)
            for glyph in range(256): original.counter(glyph,counters[glyph])
            current={name:signed(row[name]) if name in SIGNED else row[name] for name,_,_ in FIELDS}
            assert [current[k] for k in ('x','y','z','heading')]==[f['pose'][k] for k in ('x','y','z','heading')], 'N10 hero source seed differs'
            history=[(row['native_now'],[current['x'],current['y'],current['z'],current['heading']%360*65536])]
            levels[current_level]=dict(callbacks=0,renders=0,adjusted=0,substeps=0,epoch=row['world_epoch'])
            presses,releases,held,uses=[0]*12,[0]*12,0,0
            blocked |= row['raw_held']; touch_blocked |= bool(row['raw_touch']&1)
        elif action==1:
            raw_samples.append(row)
            if len(raw_samples)>1:
                old=raw_samples[-2]
                assert u64(row,'raw_sequence')==u64(old,'raw_sequence')+1 and u64(row,'sample_counter')>=u64(old,'sample_counter'), 'N10 input order'
                assert row['raw_pressed']==row['raw_held']&~old['raw_held'] and row['raw_released']==old['raw_held']&~row['raw_held'], 'N10 raw fronts'
            expected=[row[key]&~blocked for key in ('raw_held','raw_pressed','raw_released')]
            assert expected==[row[key] for key in ('physical_held','physical_pressed','physical_released')], ('N10 UI suppression',row['ordinal'],expected)
            assert (0 if touch_blocked else row['raw_touch'])==row['physical_touch'], 'N10 touch suppression'
            blocked &= row['raw_held']
            if not row['raw_touch']&1: touch_blocked=False
            if row['status'] in (1,3) or row['raw_pressed']&8:
                blocked |= row['raw_held']; touch_blocked |= bool(row['raw_touch']&1)
                presses,releases,held,uses=[0]*12,[0]*12,0,0
            else:
                held=expected[0]
                for bit in range(12):
                    presses[bit]+=bool(expected[1]&(1<<bit));releases[bit]+=bool(expected[2]&(1<<bit))
                uses+=bool(expected[2]&((1<<10)|(1<<1)))
            input_checks+=1
        elif action==2:
            assert original is not None and row['level_id']==current_level
            assert not row['paused'] and row['native_interval']==416 and row['native_now']>=row['native_deadline'], 'N10 event boundary'
            pressed=sum(1<<bit for bit in range(12) if presses[bit]);released=sum(1<<bit for bit in range(12) if releases[bit])
            command=native(held|pressed)|(0x800 if uses else 0)
            assert [held,pressed,released,command,int(bool(uses)),int(bool(pressed&4))]==[row[k] for k in ('physical_held','physical_pressed','physical_released','native_commands','use_triggered','next_weapon_pressed')], ('N10 command latch',row['ordinal'])
            presses=[max(0,v-1) for v in presses];releases=[max(0,v-1) for v in releases];uses=max(0,uses-1)
            flags=(current['flags']&~0x00182a3c)|command
            assert row['input_flags']==flags, 'N10 before-service flags'
            result=original.run_motion(dict(current,flags=flags,elapsed=row['service_elapsed']))
            assert row['collision_rejected']==0, 'N10 live route hit a guard: add per-substep independent guard proof before accepting it'
            for name,_,_ in FIELDS:
                actual=signed(row[name]) if name in SIGNED else row[name]
                assert result['state'][name]==actual, ('N10 original scalar/collision mismatch',row['ordinal'],name,result['state'][name],actual)
            assert (result['moved'],result['substeps'])==(row['moved'],row['substeps']), 'N10 original substeps'
            maps,flags,counters,known=decode_snapshot(snapshots[row['snapshot_ordinal']])
            size=original.f['width']*original.f['height']
            for plane in range(3):
                native_map=bytes(original.cpu.mem_read(0x30000+original.p['mmap']+4356*plane,size))
                assert maps[plane][:size]==native_map, ('N10 original mutable map differs',row['ordinal'],plane)
            assert maps[3]==bytes(4096), 'N10 visited changed before N20'
            native_counts=[struct.unpack('<H',original.cpu.mem_read(0x81000+g*0x80+0x56,2))[0] for g in range(256)]
            assert counters==native_counts and flags==original.f['flags'] and known==bytes(original.f['known']), ('N10 native counter/flags/known mismatch',row['ordinal'])
            all_maps_bytes+=4*4096; all_counter_bytes+=512; world_checks+=1
            level=levels[current_level]
            first=level['callbacks']==0
            assert bool(row['first_service'])==first and (not first or row['service_elapsed']==0), 'N10 first native stopwatch'
            prior=next((r for r in reversed(callbacks) if r['level_id']==current_level),None)
            expected=0 if prior is None else (row['service_tick']-prior['watch_start'])&0xffffffff
            assert row['service_elapsed']==expected and row['watch_start']==row['native_now'] and row['watch_started']==1, 'N10 stopwatch after work'
            current=result['state']; pose=[current['x'],current['y'],current['z'],current['heading']%360*65536]
            if row['collision_adjusted']: history=[]
            if history and history[-1][0]==row['native_now']: history=[]
            history.append((row['native_now'],pose)); history=history[-2:]
            callbacks.append(row);level['callbacks']+=1;level['substeps']+=row['substeps'];level['adjusted']+=row['collision_adjusted']
        elif action==3:
            level=levels[current_level];level['renders']+=1
            assert 0<=row['camera_alpha_q16']<=65536, 'N10 camera interpolation alpha'
            now,pose=history[-1]
            expected=list(pose)
            if len(history)>1:
                old_tick,old=history[-2];alpha=row['camera_alpha_q16']
                interval=(now-old_tick)&0xffffffff
                expected_alpha=min(((row['native_now']-now)&0xffffffff)*65536//interval,65536) if interval else 65536
                assert alpha==expected_alpha, ('N10 interpolation time fraction',row['ordinal'],alpha,expected_alpha)
                for i in range(3): expected[i]=old[i]+trunc((pose[i]-old[i])*alpha,65536)
                angle=pose[3]-old[3]
                if angle>180*65536: angle-=360*65536
                if angle< -180*65536: angle+=360*65536
                expected[3]=(old[3]+trunc(angle*alpha,65536))%(360*65536)
            # Independently execute source footprint with only this actor's
            # current MMap cell removed. The observed guest world is read-only;
            # this temporary byte belongs exclusively to the source CPU.
            self_cell=(current['z']//original.f['side'])*original.f['width']+current['x']//original.f['side']
            address=0x30000+original.p['mmap']+self_cell
            old_byte=bytes(original.cpu.mem_read(address,1));original.cpu.mem_write(address,b' ')
            r=original.f['radius'];sx=original.f['width']*original.f['side'];sz=original.f['height']*original.f['side']
            valid=expected[0]-r>=0 and expected[2]-r>=0 and expected[0]+r<sx and expected[2]+r<sz
            clear=valid and original.query((0,expected[0],expected[2],0,0))[0]!=0
            original.cpu.mem_write(address,old_byte)
            assert bool(row['camera_fallback'])==(not clear), ('N10 camera fallback admission',row['ordinal'])
            assert bool(row['camera_admitted'])==bool(clear), ('N10 camera source footprint',row['ordinal'])
            if row['camera_fallback']: expected=list(pose)
            actual=[signed(row[k]) for k in ('camera_x','camera_y','camera_z')]+[row['camera_heading_q16']]
            assert actual==expected, ('N10 pure camera interpolation/admission',row['ordinal'],actual,expected)
            camera_checks+=1
        elif action==4: pauses.append(row)
        elif action==5: resumes.append(row)
        if current is not None and action not in (2,6):
            assert all(signed(row[k])==current[k] for k in ('x','y','z','heading')), 'N10 presentation/input changed simulation'
        previous=row
    assert list(levels)==[1,2,3], 'N10 missing navigation level'
    assert len(pauses)==len(resumes)==3, 'N10 needs one pause per level'
    for p,r in zip(pauses,resumes,strict=True):
        assert p['level_id']==r['level_id'] and r['vblank_count']-p['vblank_count']>=40, 'N10 pause duration'
        assert (p['native_now'],u64(p,'fraction'),p['callback_total'])==(r['native_now'],u64(r,'fraction'),r['callback_total']), 'N10 paused simulation advanced'
    for level in levels.values(): assert level['callbacks']>=50 and level['renders']>=200 and level['substeps']>=50, 'N10 insufficient level navigation'
    return dict(original_x86_callbacks=len(callbacks),input_samples=input_checks,presentation_interpolations=camera_checks,
                rational_clock_transitions=clock_checks,world_snapshots=world_checks,mutable_map_bytes=all_maps_bytes,
                door_counter_bytes=all_counter_bytes,pauses=3,resumes=3,levels=levels,
                uses=sum(r['use_triggered'] for r in callbacks),next_weapon=sum(r['next_weapon_pressed'] for r in callbacks))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,required=True)
    parser.add_argument('--rom',type=Path,default=OUT/'dok_nds_collision.nds')
    parser.add_argument('--elf',type=Path,default=OUT/'dok_nds_collision.elf')
    args=parser.parse_args()
    path=OUT/'emulator';directory(path);target=safe_output(path/'result.json')
    report=dict(task='N10',passed=False,command=sys.argv,limitations=[
        'DS-mode emulator only; physical DS/DS Lite and actual FPS remain N24.',
        'Static original blockers; full door state machine N11, billboard actors N12, floor gameplay effects N13.',
        'Bottom screen diagnostic text until N20; no game saves or FAT writes.'])
    frontend=fixture_path=None
    try:
        assert transport.sha(args.core)==CORE_SHA, 'N10 unpinned emulator core'
        header=ROOT/'ports/nds/platform/collision_probe/collision_probe.h'
        pf,tf,sf=fields(header,'NdsCollisionProbe'),fields(header,'NdsCollisionTraceRecord'),fields(header,'NdsCollisionSnapshotHeader')
        address,size=transport.elf_symbol(args.elf,'g_nds_collision_probe')
        assert size==len(pf)*4, 'N10 ELF/header ABI'
        host_path=ROOT/'research/evidence/nds/n10/host-check.json'
        host=json.loads(host_path.read_text('utf8'))
        assert host.get('passed') and 'arm9_build' in host, 'N10 matching host/ARM9 gate required'
        pins=host['sources']+host['arm9_build']['artifacts']
        for row in pins: assert transport.sha(ROOT/row['path'])==row['sha256'], 'N10 gate changed '+row['path']
        package=OUT/'fixture/assets.ndp';packet=OUT/'fixture/scene.dat'
        assert transport.sha(package)==PACKAGE_SHA and packet.read_bytes()==(ROOT/'build/nds/n07/scene.dat').read_bytes(), 'N10 fixture pins'
        report.update(rom_sha256=transport.sha(args.rom),elf_sha256=transport.sha(args.elf),core_sha256=CORE_SHA,
                      harness_sha256=transport.sha(Path(__file__)),abi_sha256=transport.sha(header),host_gate_sha256=transport.sha(host_path))
        frontend=transport.Frontend(args.core,path,'happy')
        fixture_path=safe_output(frontend.save/'melonDS DS/dldi_sd_card.bin')
        fat.write_image(fixture_path,[('assets.ndp',b'ASSETS  NDP',package.read_bytes()),('scene.dat',b'SCENE   DAT',packet.read_bytes())])
        frontend.load(args.rom);transport.flush_native_stdio()
        report['fat_after_load_sha256']=transport.sha(fixture_path)
        rows=[];snapshots={};observations=[];inputs=[];images=[];route=Route();probe=None;completed=0
        for frame in range(5000):
            joypad,touch=route.input(frame,probe);frontend.joypad,frontend.touch=joypad,touch
            if not inputs or (inputs[-1]['joypad'],inputs[-1]['touch'])!=(joypad,touch):
                inputs.append(dict(frontend_frame=frame,joypad=joypad,touch=touch,phase=route.phase,level=route.level))
            if frontend.frame_time and frontend.frame_time.callback: frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run();frontend.frames+=1
            if frontend.callback_error or frontend.shutdown: raise RuntimeError(frontend.callback_error or 'Core shutdown')
            probe=observe(frontend,address,pf,0x30434b44)
            if probe is None: continue
            report['last_observation']=probe
            assert probe['status']!=5 and not probe['error_bits'] and not probe['failures'], 'N10 ARM9 failure'
            while probe['trace_address']:
                ordinal=len(rows)+1
                row=observe(frontend,probe['trace_address']+((ordinal-1)%256)*len(tf)*4,tf,0x30564b44)
                if row is None or row['ordinal']<ordinal: break
                assert row['ordinal']==ordinal, 'N10 trace overwritten'
                if row['action'] in (2,6): snapshots[row['snapshot_ordinal']]=snapshot(frontend,probe,row,sf)
                row['observed_frontend_frame']=frame;rows.append(row)
                if row['action']==1: route.latest_input=row
                if row['action']==2: route.latest_callback=row
            observations.append(probe)
            assert frontend.core.retro_get_memory_size(2)==4194304 and not probe['is_dsi'], 'N10 ordinary DS mode'
            level=probe['level_id']
            for suffix,condition in [('start',probe['status']==2 and probe['frame_id']>=3),
                                     ('moving',probe['status']==2 and probe['frame_id']>=40),('paused',probe['status']==3)]:
                name=f'level{level}-{suffix}'
                if level and condition and not any(x['name']==name for x in images):
                    images.append(dict(name=name,files=capture(frontend,name),video=video(frontend,packet.read_bytes(),level)))
            if probe['status']==4:
                completed+=1
                if completed>=3: break
        assert probe and probe['status']==4 and len(rows)==probe['trace_total'], 'N10 did not complete all records'
        assert (probe['trace_capacity'],probe['trace_record_bytes'],probe['snapshot_capacity'],probe['snapshot_record_bytes'])==(256,len(tf)*4,4,17728), 'N10 ring ABI'
        assert probe['buttons_seen']&0xfff==0xfff and probe['touch_seen']&1, 'N10 missing controls'
        assert probe['total_frames']>=600 and not probe['render_overflows'] and not probe['matrix_errors'], 'N10 3D route'
        report['trace_checks']=verify_trace(rows,snapshots)
        report['memory']=verify_memory(probe,args.elf)
        assert probe['collision_checks']==host['shared_suites']['collision']['checks'], 'N10 host/ARM unit count'
        assert probe['replay_checks']==sum(r['checks'] for r in host['shared_suites']['routes']['levels'] if r['language']==1), 'N10 host/ARM route count'
        assert (report['trace_checks']['uses'],report['trace_checks']['next_weapon'])==(probe['use_total'],probe['next_weapon_total']), 'N10 action totals'
        for row in pins: assert transport.sha(ROOT/row['path'])==row['sha256'], 'N10 gate changed during runtime'
        payload=b''.join(snapshots[index] for index in sorted(snapshots))
        payload_path=safe_output(path/'snapshot-worlds.bin');payload_path.write_bytes(payload)
        report.update(traces=rows,observations=observations,inputs=inputs,route_events=route.events,images=images,
                      snapshot_file=dict(path=payload_path.relative_to(ROOT).as_posix(),bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()),
                      core_name=frontend.info.library_name.decode(),core_version=frontend.info.library_version.decode())
        transport.flush_native_stdio()
        assert transport.sha(fixture_path)==report['fat_after_load_sha256'], 'N10 runtime wrote FAT'
        report['passed']=True
    except Exception:
        report['error']=traceback.format_exc()
        if 'rows' in locals(): report['traces']=rows
        if 'inputs' in locals(): report['inputs']=inputs
    finally:
        if frontend:
            report['frontend_frames']=frontend.frames
            report['core_options']={key:value.decode() for key,value in frontend.variables.items()}
            if not report['passed'] and frontend.last_video: report['failure_images']=capture(frontend,'failure')
            try:
                frontend.close();transport.flush_native_stdio()
                if fixture_path and 'fat_after_load_sha256' in report:
                    report['fat_after_unload_sha256']=transport.sha(fixture_path)
                    assert report['fat_after_unload_sha256']==report['fat_after_load_sha256'], 'N10 unload wrote FAT'
            except Exception:
                report['cleanup_error']=traceback.format_exc();report['passed']=False
        target.write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
    print(f"N10 emulator: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report['passed'] else 1


if __name__=='__main__': raise SystemExit(main())
