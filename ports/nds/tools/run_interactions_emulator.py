#!/usr/bin/env python3
"""N11 DS interaction fixtures through frontend input and read-only ARM9 ABI."""
from __future__ import annotations
import argparse
import ctypes as C
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import traceback

import make_assets_fixture as fat
import run_diag_emulator as transport
from run_movement_emulator import observe, ram, u64, signed, trunc, verify_memory

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n11'
CORE_SHA='c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef'
PACKAGE_SHA='2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511'
BUTTONS={'A':1<<8,'B':1<<0,'X':1<<9,'Y':1<<1,'L':1<<10,'R':1<<11,
         'Select':1<<2,'Start':1<<3,'Up':1<<4,'Down':1<<5,'Left':1<<6,'Right':1<<7}


def fields(header,name):
    """Decode scalar/constant-array uint32 observation layout from its C header."""
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',header.read_text('utf8'),flags=re.S)
    constants={k:int(v,0) for k,v in re.findall(r'#define\s+(\w+)\s+(0x[0-9a-fA-F]+|[0-9]+)u?\b',text)}
    m=re.search(r'typedef\s+struct\s+'+re.escape(name)+r'\s*\{(.*?)\}',text,re.S)
    if not m: raise ValueError('Missing N11 ABI struct '+name)
    result=[]
    for declaration in m[1].split(';'):
        declaration=declaration.strip()
        if not declaration:continue
        if not declaration.startswith('uint32_t '): raise ValueError('N11 scalar trace/probe requires uint32')
        for item in declaration[9:].split(','):
            item=item.strip();a=re.fullmatch(r'(\w+)\[(\w+)\]',item)
            names=[f'{a[1]}_{i}' for i in range(constants[a[2]] if a[2] in constants else int(a[2]))] if a else [item]
            for n in names:
                if not n.isidentifier() or n in result:raise ValueError('Invalid N11 ABI field '+n)
                result.append(n)
    assert result[:4]==['magic','abi_version','size_bytes','sequence']
    return result


def native_words(row,name,count):
    return [signed(row[f'{name}_{i}']) for i in range(count)]

def safe_output(path):
    path = Path(path).absolute()
    if '..' in path.parts or path == OUT or not path.is_relative_to(OUT):
        raise ValueError('N11 output must stay under build/nds/n11')
    for item in (path, *path.parents):
        if item == ROOT: break
        if item.is_symlink() or (item.exists() and item.is_junction()): raise ValueError('N11 output alias')
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1): raise ValueError('N11 output is not a regular unique file')
    return path


def directory(path):
    safe_output(path / 'reserved')
    if path.exists():
        for item in path.rglob('*'):
            if item.is_symlink() or item.is_junction(): raise ValueError('N11 emulator directory alias')
            if item.is_file() and item.stat().st_nlink != 1: raise ValueError('N11 emulator hardlink')
    path.mkdir(parents=True, exist_ok=True)


def capture(frontend, stem):
    for suffix in ('.png','-top.png','-bottom.png'): safe_output(frontend.directory / (stem + suffix))
    return frontend.capture(stem)


def video(frontend, packet, level):
    width,height,rgb = frontend.rgb()
    assert (width,height) == (256,384), 'N11 dual screen dimensions'
    count = struct.unpack_from('<I',packet,12)[0]
    base = 64 + count * 64 + (level-1)*10240
    palette = struct.unpack_from('<256H',packet,base)
    indices = packet[base+512:base+10240]
    actual=[]
    for offset in range(154*256*3,192*256*3,3):
        r,g,b=rgb[offset:offset+3]
        actual.append((r>>3)|((g>>3)<<5)|((b>>3)<<10))
    assert actual == [palette[index]&0x7fff for index in indices], 'N11 accepted HUD pixels differ'
    scene=bytes(rgb[:154*256*3])
    assert len(set(scene))>1, 'N11 scene blank'
    return dict(hud_rgb555_pixels=len(actual),scene_rgb_sha256=hashlib.sha256(scene).hexdigest())


def snapshot(frontend, probe, row, names):
    ordinal=row['snapshot_ordinal']
    capacity=probe['snapshot_capacity'];size=probe['snapshot_record_bytes']
    assert ordinal and capacity and size==19520, 'N11 snapshot ABI sizes'
    slot=(ordinal-1)%capacity
    assert row['snapshot_slot']==slot, 'N11 snapshot link slot'
    address=probe['snapshot_address']+slot*size
    first=ram(frontend,address+12,4);data=ram(frontend,address,size);last=ram(frontend,address+12,4)
    h=dict(zip(names,struct.unpack('<16I',data[:64]),strict=True))
    assert first==last and struct.unpack('<I',first)[0]==h['sequence'] and not h['sequence']&1, 'N11 unstable snapshot'
    assert (h['magic'],h['abi_version'],h['size_bytes'],h['ordinal'],h['trace_ordinal'])==(0x31534b44,1,size,ordinal,row['ordinal']), ('N11 snapshot identity',h['ordinal'],ordinal,h['trace_ordinal'],row['ordinal'],h['magic'],h['size_bytes'])
    for key in ('level_id','world_epoch','callback_total','hero_glyph','hero_id','hero_generation','case_id','stage_sequence'):
        assert h[key]==row[key], ('N11 snapshot/trace identity',key)
    return data


def decode_snapshot(data):
    return dict(maps=[data[64+4096*p:64+4096*(p+1)] for p in range(4)],
                flags=list(struct.unpack_from('<256H',data,16448)),
                counters=list(struct.unpack_from('<256H',data,16960)),
                known=data[17472:17728],phase=list(struct.unpack_from('<256h',data,17728)),
                extent=list(struct.unpack_from('<256h',data,18240)),
                state=data[18752:19008],ready=data[19008:19264],marker=data[19264:19520])


def verify_common_trace(rows,snapshots):
    """Independent clock, ABI identity and snapshot state cross-checks.

    Native operation comparisons and coverage gates are added separately;
    these checks alone do not establish interaction correctness.
    """
    ratio=Fraction(1193182*64,119*33513982)
    previous=None;clock_checks=0;snapshot_checks=0;pauses=[];resumes=[];cases={}
    input_rows=[]
    for row in rows:
        if previous is not None and row['world_epoch']==previous['world_epoch']:
            delta=u64(row,'clock_last_counter')-u64(previous,'clock_last_counter')
            assert delta>=0, 'N11 raw clock regressed'
            ticks,fraction=previous['native_now'],u64(previous,'fraction')
            if not previous['paused']:
                whole,fraction=divmod(delta*ratio.numerator+fraction,ratio.denominator)
                ticks=(ticks+whole)&0xffffffff
            assert (ticks,fraction)==(row['native_now'],u64(row,'fraction')), ('N11 rational clock',row['ordinal'])
            clock_checks+=1
        if row['action']==1:
            if input_rows:
                old=input_rows[-1]
                assert u64(row,'raw_sequence')==u64(old,'raw_sequence')+1, 'N11 input ordering'
                assert row['raw_pressed']==row['raw_held']&~old['raw_held'], 'N11 SDK make edge'
                assert row['raw_released']==old['raw_held']&~row['raw_held'], 'N11 SDK break edge'
            input_rows.append(row)
        if row['snapshot_ordinal']:
            s=decode_snapshot(snapshots[row['snapshot_ordinal']]);glyph=row['door_glyph']
            assert row['snapshot_ordinal']==snapshot_checks+1, 'N11 snapshots incomplete'
            assert glyph<256 and all(x in (0,1) for x in s['known']), 'N11 invalid runtime table'
            if row['native_valid'] and row['native_target_glyph']<256:
                g=row['native_target_glyph'];a=native_words(row,'native_after',18)
                assert [s['flags'][g],s['phase'][g],s['state'][g],s['ready'][g],(s['counters'][g] if s['counters'][g]<32768 else s['counters'][g]-65536),s['extent'][g],s['marker'][g]]==[a[i] for i in (0,1,2,3,4,5,8)], ('N11 operation/snapshot state',row['ordinal'])
            snapshot_checks+=1
        if row['action']==4:pauses.append(row)
        if row['action']==5:resumes.append(row)
        if row['case_id']:
            c=cases.setdefault(row['case_id'],dict(level=row['level_id'],renders=0,callbacks=0,stages=[]))
            assert c['level']==row['level_id'], 'N11 case changed source level'
            if row['stage_id'] not in c['stages']:c['stages'].append(row['stage_id'])
            c['renders']+=row['action']==3;c['callbacks']+=row['action']==2
        previous=row
    assert len(pauses)==len(resumes) and pauses, 'N11 pause coverage absent'
    for p,r in zip(pauses,resumes,strict=True):
        assert p['case_id']==r['case_id'] and r['vblank_count']-p['vblank_count']>=40, 'N11 pause span'
        assert (p['native_now'],u64(p,'fraction'),p['callback_total'],p['door_callbacks'],p['interaction_revision'])==(r['native_now'],u64(r,'fraction'),r['callback_total'],r['door_callbacks'],r['interaction_revision']), 'N11 paused interaction changed'
    assert snapshot_checks==len(snapshots), 'N11 unlinked snapshots'
    return dict(rational_clock_transitions=clock_checks,input_samples=len(input_rows),world_snapshots=snapshot_checks,
                pauses=len(pauses),resumes=len(resumes),pause_vblanks=[r['vblank_count']-p['vblank_count'] for p,r in zip(pauses,resumes,strict=True)],cases=cases)


class Route:
    """Finite button-only operator for the explicitly labelled guest fixtures."""
    def __init__(self):
        self.key=None;self.since=0;self.events=[];self.pulse=None;self.used=set()
        self.pause='none';self.pause_since=0;self.pause_uses=None;self.busy_use=False

    def start_pulse(self,frame,buttons,kind,probe):
        self.pulse=(frame,buttons,kind)
        self.events.append(dict(kind=kind,frontend_frame=frame,case_id=probe['case_id'],
                                stage_id=probe['stage_id'],stage_sequence=probe['stage_sequence']))

    def input(self,frame,probe):
        if probe is None or probe['status'] in (0,1,4,5):return 0,None
        key=(probe['case_id'],probe['stage_id'],probe['stage_sequence'])
        if key!=self.key:
            self.key=key;self.since=frame
            self.events.append(dict(kind='stage',frontend_frame=frame,case_id=key[0],stage_id=key[1],stage_sequence=key[2]))
        held=BUTTONS['X']|BUTTONS['B']
        if self.pause=='press' and probe['status']==3:
            self.pause='held';self.pause_since=frame;self.pause_uses=probe['use_total']
            self.events.append(dict(kind='paused',frontend_frame=frame,case_id=probe['case_id']))
        if self.pause=='held':
            age=frame-self.pause_since
            if probe['status']==2:
                assert age>=40,'N11 held Start resumed'
                self.pause='suppressed';self.pause_since=frame
                self.events.append(dict(kind='resumed',frontend_frame=frame,case_id=probe['case_id']))
            else:return held|(BUTTONS['Start'] if age<16 or age>=45 else 0),(64,80)
        if self.pause=='press':return held|BUTTONS['Start'],(64,80)
        if self.pause=='suppressed':
            age=frame-self.pause_since
            assert probe['use_total']==self.pause_uses,'N11 stale Use escaped UI boundary'
            if age<10:return held,(64,80)
            if age<18:return 0,None
            self.pause='done'
        if self.pulse is not None:
            age=frame-self.pulse[0]
            if age<3:return self.pulse[1],None
            if age<7:return 0,None
            self.pulse=None
        case,stage,_=key
        if case==1 and stage==4 and probe['door_phase']>=1 and not self.busy_use:
            self.busy_use=True
            self.start_pulse(frame,BUTTONS['X'],'repeat-use-while-opening',probe)
            return BUTTONS['X'],None
        if case==1 and stage==4 and self.busy_use and self.pause=='none' and 1<=probe['door_phase']<max(2,probe['door_extent']-2):
            self.pause='press';self.pause_since=frame
            return held|BUTTONS['Start'],(64,80)
        if frame-self.since<8:return 0,None
        if stage in (1,2,3,8) and key not in self.used:
            self.used.add(key)
            buttons=held if stage==2 else BUTTONS['B'] if case==2 else BUTTONS['X']
            self.start_pulse(frame,buttons,'use-release',probe)
            return buttons,None
        if probe.get('advance_ready') and frame-self.since>=20 and key not in self.used:
            self.used.add(key);self.start_pulse(frame,BUTTONS['A'],'fixture-advance',probe)
            return BUTTONS['A'],None
        assert frame-self.since<2400,('N11 fixture stage stalled',key,probe)
        return 0,None


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,required=True)
    parser.add_argument('--rom',type=Path,default=OUT/'dok_nds_interactions.nds')
    parser.add_argument('--elf',type=Path,default=OUT/'dok_nds_interactions.elf')
    parser.add_argument('--host-gate',type=Path,default=ROOT/'research/evidence/nds/n11/host-check.json')
    parser.add_argument('--output-root',type=Path,
                        help='isolated rerun below build/nds/perf; accepted N11 evidence is preserved')
    args=parser.parse_args()
    if args.output_root:
        candidate=args.output_root.absolute()
        if '..' in candidate.parts or not candidate.is_relative_to(ROOT/'build/nds/perf'):
            raise ValueError('Isolated N11 output must stay below build/nds/perf')
        OUT=candidate
    path=OUT/'emulator';directory(path);target=safe_output(path/'result.json')
    report=dict(task='N11',passed=False,command=sys.argv,limitations=[
        'DS-mode emulator; physical DS/DS Lite, flashcart/loader and actual FPS remain N24.',
        'Keys, score, repositioning and occupancy are explicitly labelled diagnostic fixtures; pickups N14 and campaign N22.',
        'Original full-open residual texture is deliberately suppressed by NDS rendering.',
        'Bottom diagnostic console until N20/N21; no game saves or FAT writes.'])
    frontend=fixture_path=None
    try:
        assert transport.sha(args.core)==CORE_SHA,'N11 unpinned core'
        header=ROOT/'ports/nds/platform/interactions_probe/interactions_probe.h'
        pf,tf,sf=[fields(header,name) for name in ('NdsInteractionProbe','NdsInteractionTraceRecord','NdsInteractionSnapshotHeader')]
        address,size=transport.elf_symbol(args.elf,'g_nds_interaction_probe')
        assert size==len(pf)*4,'N11 ELF/header probe ABI'
        host_path=args.host_gate
        host=json.loads(host_path.read_text('utf8'))
        assert host.get('passed') and 'arm9_build' in host,'N11 current host/ARM9 gate required'
        pins=host['sources']+host['arm9_build']['artifacts']
        for row in pins:assert transport.sha(ROOT/row['path'])==row['sha256'],'N11 changed gate '+row['path']
        package=OUT/'fixture/assets.ndp';packet=OUT/'fixture/scene.dat'
        assert transport.sha(package)==PACKAGE_SHA and packet.read_bytes()==(ROOT/'build/nds/n07/scene.dat').read_bytes(),'N11 fixture pins'
        report.update(rom_sha256=transport.sha(args.rom),elf_sha256=transport.sha(args.elf),core_sha256=CORE_SHA,
                      harness_sha256=transport.sha(Path(__file__)),abi_sha256=transport.sha(header),host_gate_sha256=transport.sha(host_path))
        frontend=transport.Frontend(args.core,path,'happy')
        fixture_path=safe_output(frontend.save/'melonDS DS/dldi_sd_card.bin')
        fat.write_image(fixture_path,[('assets.ndp',b'ASSETS  NDP',package.read_bytes()),('scene.dat',b'SCENE   DAT',packet.read_bytes())])
        frontend.load(args.rom);transport.flush_native_stdio()
        report['fat_after_load_sha256']=transport.sha(fixture_path)
        rows=[];snapshots={};observations=[];inputs=[];images=[];route=Route();probe=None;completed=0
        latest_render=None;latest_render_state=None;stable_visual=None;stable_since=0
        for frame in range(18000):
            joypad,touch=route.input(frame,probe);frontend.joypad,frontend.touch=joypad,touch
            if not inputs or (inputs[-1]['joypad'],inputs[-1]['touch'])!=(joypad,touch):
                inputs.append(dict(frontend_frame=frame,joypad=joypad,touch=touch,stage=route.key))
            if frontend.frame_time and frontend.frame_time.callback:frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run();frontend.frames+=1
            if frontend.callback_error or frontend.shutdown:raise RuntimeError(frontend.callback_error or 'Core shutdown')
            probe=observe(frontend,address,pf,0x31494b44)
            if probe is None:continue
            report['last_observation']=probe
            assert probe['status']!=5 and not probe['error_bits'] and not probe['failures'],('N11 ARM9 failure',probe)
            while probe['trace_address']:
                ordinal=len(rows)+1
                row=observe(frontend,probe['trace_address']+((ordinal-1)%128)*len(tf)*4,tf,0x31544b44)
                if row is None or row['ordinal']<ordinal:break
                assert row['ordinal']==ordinal,'N11 trace overwritten'
                if row['snapshot_ordinal']:
                    assert row['snapshot_ordinal'] not in snapshots,'N11 duplicate snapshot publication'
                    snapshots[row['snapshot_ordinal']]=snapshot(frontend,probe,row,sf)
                row['observed_frontend_frame']=frame;rows.append(row)
                if row['action']==3:
                    latest_render=row
                    bound=max(snapshots)
                    row['observer_snapshot_ordinal']=bound
                    latest_render_state=decode_snapshot(snapshots[bound])
            observations.append(probe)
            assert frontend.core.retro_get_memory_size(2)==4194304 and not probe['is_dsi'],'N11 ordinary DS mode'
            case=probe['case_id'];stage=probe['stage_id']
            if latest_render and latest_render['case_id']==case and latest_render_state:
                glyph=latest_render['door_glyph'];phase=latest_render_state['phase'][glyph]
                extent=latest_render_state['extent'][glyph];render_stage=latest_render['stage_id']
                visual=(case,render_stage,phase,extent,latest_render['status'])
                if visual!=stable_visual:stable_visual=visual;stable_since=frame
                settled=frame-stable_since>=5
                conditions=[('start',render_stage in (1,3) and phase==0 and probe['frame_id']>=2),
                            ('middle',render_stage==4 and extent>3 and (extent+2)//3<=phase<=2*extent//3 and latest_render['rendered_cropped']>0),
                            ('open',render_stage in (5,6,8,9) and extent>1 and phase>=extent-1 and settled),
                            ('closed',render_stage==11 and phase==0 and settled),
                            ('paused',probe['status']==3 and latest_render['status']==3 and settled),
                            ('sensor',render_stage==13 and settled),
                            ('conveyor',render_stage==16 and probe.get('advance_ready')),
                            ('finish',render_stage==18 and probe.get('advance_ready') and settled)]
                for suffix,condition in conditions:
                    name=f'case{case}-{suffix}'
                    if condition and not any(x['name']==name for x in images):
                        images.append(dict(name=name,render_ordinal=latest_render['ordinal'],
                                           snapshot_ordinal=latest_render['observer_snapshot_ordinal'],phase=phase,extent=extent,
                                           phase_revision=latest_render['phase_revision'],files=capture(frontend,name),
                                           video=video(frontend,packet.read_bytes(),probe['level_id'])))
            if probe['status']==4:
                completed+=1
                if completed>=3:break
        assert probe and probe['status']==4 and len(rows)==probe['trace_total'],'N11 did not complete all records'
        assert probe['trace_capacity']==128 and probe['trace_record_bytes']==4*len(tf),'N11 trace ABI'
        assert probe['snapshot_capacity']==8 and probe['snapshot_record_bytes']==19520,'N11 snapshot ring ABI'
        assert len(snapshots)==probe['snapshot_total'],'N11 snapshots incomplete'
        assert probe['replay_checks']==host['shared_suites']['router']['checks'],'N11 ARM9 router unit count'
        assert probe['interaction_checks']==host['shared_suites']['interactions']['checks'],'N11 ARM9 interaction unit count'
        assert probe['door_render_checks']==host['shared_suites']['door_render']['checks'],'N11 ARM9 door render unit count'
        assert not probe['render_overflows'] and not probe['matrix_errors'],'N11 3D errors'
        report['common_trace_checks']=verify_common_trace(rows,snapshots)
        from verify_interactions_trace import verify_trace
        report['trace_checks']=verify_trace(rows,snapshots)
        assert route.pause=='done','N11 pause route missing'
        names={row['name'] for row in images}
        for case in range(1,5):
            for suffix in ('start','middle','open','closed'):
                assert f'case{case}-{suffix}' in names,('N11 missing door visual',case,suffix)
        report['memory']=verify_memory(probe,args.elf)
        assert set(report['common_trace_checks']['cases'])==set(range(1,7)),'N11 six source fixtures missing'
        for row in pins:assert transport.sha(ROOT/row['path'])==row['sha256'],'N11 gate changed during emulator'
        payload=b''.join(snapshots[k] for k in sorted(snapshots));payload_path=safe_output(path/'snapshot-worlds.bin');payload_path.write_bytes(payload)
        report.update(traces=rows,observations=observations,inputs=inputs,route_events=route.events,images=images,
                      snapshot_file=dict(path=payload_path.relative_to(ROOT).as_posix(),bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()),
                      core_name=frontend.info.library_name.decode(),core_version=frontend.info.library_version.decode())
        transport.flush_native_stdio()
        assert transport.sha(fixture_path)==report['fat_after_load_sha256'],'N11 runtime wrote FAT'
        report['passed']=True
    except Exception:
        report['error']=traceback.format_exc()
        if 'rows' in locals():report['traces']=rows
        if 'inputs' in locals():report['inputs']=inputs
    finally:
        if frontend:
            report['frontend_frames']=frontend.frames
            report['core_options']={k:v.decode() for k,v in frontend.variables.items()}
            if not report['passed'] and frontend.last_video:report['failure_images']=capture(frontend,'failure')
            try:
                frontend.close();transport.flush_native_stdio()
                if fixture_path and 'fat_after_load_sha256' in report:
                    report['fat_after_unload_sha256']=transport.sha(fixture_path)
                    assert report['fat_after_unload_sha256']==report['fat_after_load_sha256'],'N11 unload wrote FAT'
            except Exception:
                report['cleanup_error']=traceback.format_exc();report['passed']=False
        # Preserve raw observations before/after validation failures so the
        # same actual run can be reviewed offline without rerunning the guest.
        if 'rows' in locals():report['traces']=rows
        if 'observations' in locals():report['observations']=observations
        if 'inputs' in locals():report['inputs']=inputs
        if 'images' in locals():report['images']=images
        if 'route' in locals():report['route_events']=route.events
        if 'snapshots' in locals():
            payload=b''.join(snapshots[k] for k in sorted(snapshots))
            payload_path=safe_output(path/'snapshot-worlds.bin');payload_path.write_bytes(payload)
            report['snapshot_file']=dict(path=payload_path.relative_to(ROOT).as_posix(),bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest())
        target.write_bytes((json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
    print(f"N11 emulator: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report['passed'] else 1


if __name__=='__main__':raise SystemExit(main())

