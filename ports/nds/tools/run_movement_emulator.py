"""N09 real libretro controls + read-only ARM9 trace verification.

No RAM writes, savestates, CPU/clock injection or automatic guest test edits.
Live callback states are additionally compared with original PKR instructions.
"""
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

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/nds/n09'
CORE_SHA = 'c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef'
PACKAGE_SHA = '2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511'
BUTTONS = {name: 1 << retro for name, retro, _ in transport.BUTTONS}
SIGNED = {'x', 'y', 'z', 'heading', 'angle_parameter', 'speed', 'distance', 'turn_step',
          'linear_step', 'last_turn', 'movement_accumulator', 'turn_accumulator', 'accel_sign'}


def safe_output(path):
    path = Path(path).absolute()
    if '..' in path.parts or path == OUT or not path.is_relative_to(OUT):
        raise ValueError('N09 output must stay under build/nds/n09')
    for item in (path, *path.parents):
        if item == ROOT: break
        if item.is_symlink() or item.is_junction(): raise ValueError('N09 output alias rejected')
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError('N09 output must be an unaliased regular file')
    return path


def directory(path):
    safe_output(path / 'reserved')
    if path.exists():
        pending = [path]
        while pending:
            for item in pending.pop().iterdir():
                if item.is_symlink() or item.is_junction(): raise ValueError('N09 core directory alias')
                if item.is_dir(): pending.append(item)
                elif not item.is_file() or item.stat().st_nlink != 1: raise ValueError('N09 core file alias')
    path.mkdir(parents=True, exist_ok=True)


def fields(header, name):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', header.read_text('utf8'), flags=re.S)
    match = re.search(r'typedef\s+struct\s+' + name + r'\s*\{(.*?)\}', text, re.S)
    if not match: raise ValueError('N09 missing observation struct')
    result = []
    for declaration in match.group(1).split(';'):
        declaration = declaration.strip()
        if not declaration: continue
        if not declaration.startswith('uint32_t '): raise ValueError('N09 observation ABI requires uint32 only')
        for name in declaration[9:].split(','):
            name = name.strip()
            if not name.isidentifier() or name in result: raise ValueError('N09 invalid scalar ABI field')
            result.append(name)
    if result[:4] != ['magic', 'abi_version', 'size_bytes', 'sequence']: raise ValueError('N09 seqlock header changed')
    return result


def ram(frontend, address, count):
    pointer, size = frontend.core.retro_get_memory_data(2), frontend.core.retro_get_memory_size(2)
    offset = address - 0x02000000
    if not pointer or not 0 <= offset <= size - count: raise ValueError('N09 observation outside main RAM')
    return C.string_at(pointer + offset, count)


def observe(frontend, address, names, magic):
    first = ram(frontend, address + 12, 4)
    data = ram(frontend, address, len(names) * 4)
    last = ram(frontend, address + 12, 4)
    value = dict(zip(names, struct.unpack('<' + 'I' * len(names), data)))
    if first != last or struct.unpack('<I', first)[0] != value['sequence'] or value['sequence'] & 1: return None
    if value['magic'] != magic: return None
    if value['abi_version'] != 1 or value['size_bytes'] != len(data): raise ValueError('N09 observation ABI differs')
    return value


def u64(row, prefix):
    return row[prefix + '_low'] | (row[prefix + '_high'] << 32)


def signed(value):
    return value - 0x100000000 if value & 0x80000000 else value


def trunc(numerator, denominator):
    return numerator // denominator if numerator >= 0 else -((-numerator) // denominator)


def capture(frontend, stem):
    for suffix in ('.png', '-top.png', '-bottom.png'): safe_output(frontend.directory / (stem + suffix))
    return frontend.capture(stem)


def verify_video(frontend, packet):
    width,height,rgb=frontend.rgb()
    if (width,height)!=(256,384): raise AssertionError('N09 native dual-screen dimensions differ')
    count=struct.unpack_from('<I',packet,12)[0]
    base=64+count*64
    palette=struct.unpack_from('<256H',packet,base)
    indices=packet[base+512:base+10240]
    actual=[]
    for offset in range(154*256*3,192*256*3,3):
        r,g,b=rgb[offset:offset+3]
        actual.append((r>>3)|((g>>3)<<5)|((b>>3)<<10))
    if actual != [palette[index]&0x7fff for index in indices]: raise AssertionError('N09 HUD differs from accepted N06/N07 pixels')
    scene=bytes(rgb[:154*256*3])
    if len(set(scene))<2: raise AssertionError('N09 scene is blank')
    return dict(hud_rgb555_pixels=len(actual),scene_rgb_sha256=hashlib.sha256(scene).hexdigest())


def native(buttons):
    result = 0
    for bit, command in [(11,4),(0,8),(6,16),(7,32),(5,0x80000),(4,0x100000),(8,0x200),(9,0x2000)]:
        if buttons & (1 << bit): result |= command
    return result


class Route:
    """External input schedule, driven only by observed frame/status boundaries."""
    def __init__(self):
        self.phase, self.since, self.pauses = 'route', 0, 0
        self.events = []
        self.pulse, self.pulse_sample, self.pulse_callback = 'waiting', 0, 0
        self.latest_input, self.last_callback = None, 0
        self.latest_callback = None

    def input(self, frontend_frame, probe):
        frame = 0 if probe is None else probe['frame_id']
        status = 0 if probe is None else probe['status']
        callback = 0 if self.latest_callback is None else self.latest_callback['callback_total']
        fresh_callback = callback != self.last_callback
        self.last_callback = callback
        if self.phase == 'route' and self.pauses < 2 and frame >= (130 if not self.pauses else 250):
            self.phase, self.since = 'press-pause', frontend_frame
        if self.phase == 'press-pause' and status == 2:
            self.phase, self.since = 'paused', frontend_frame
            self.events.append(dict(kind='pause-observed', frontend_frame=frontend_frame, frame_id=frame))
        if self.phase == 'paused' and status == 1:
            if frontend_frame-self.since < 40: raise AssertionError('Held Start resumed without a fresh press')
            self.phase, self.since = 'suppressed-hold', frontend_frame
            self.pauses += 1
            self.events.append(dict(kind='resume-observed', frontend_frame=frontend_frame, frame_id=frame))
        age = frontend_frame-self.since
        held = BUTTONS['R'] | BUTTONS['X'] | BUTTONS['B']
        if self.phase == 'press-pause': return held | BUTTONS['Start'], (64, 80)
        if self.phase == 'paused':
            return held | (BUTTONS['Start'] if age < 20 or age >= 40 else 0), (64 + min(age,40),80)
        if self.phase == 'suppressed-hold':
            if age < 24: return held, (104,80)
            if age < 36: return 0, None
            if age < 48: return held, (120,100)
            self.phase, self.since = 'route', frontend_frame
        # One live R tap entirely between services. Start immediately after an
        # observed callback, hold until ARM9 captured press, then release. If
        # the sampling boundary was too late, retry after the next callback.
        if self.phase == 'route' and frame >= 76 and self.pulse != 'done':
            latest = self.latest_input
            if self.pulse == 'waiting' and fresh_callback and latest and not latest['raw_held'] & (1<<8):
                self.pulse, self.pulse_sample = 'press', latest['input_total']
            if self.pulse == 'press':
                if latest and latest['input_total'] > self.pulse_sample and latest['raw_pressed'] & (1<<8):
                    self.pulse, self.pulse_callback = 'release', latest['callback_total']
                    self.pulse_sample = latest['input_total']
                else:
                    return BUTTONS['R'], None
            if self.pulse == 'release':
                if latest and latest['input_total'] > self.pulse_sample and latest['raw_released'] & (1<<8):
                    self.pulse = 'done' if latest['callback_total'] == self.pulse_callback else 'waiting'
                    self.events.append(dict(kind='short-fire-tap',frontend_frame=frontend_frame,
                                            captured_between_services=self.pulse == 'done'))
                else:
                    return 0, None
        if frame < 16: return 0, None
        if frame < 40: return BUTTONS['Up'] | BUTTONS['L'] | BUTTONS['R'] | BUTTONS['Y'], None
        if frame < 56: return BUTTONS['Right'] | BUTTONS['A'], None
        if frame < 72: return BUTTONS['Down'], None
        if frame < 88: return BUTTONS['Left'], None
        if frame < 90: return BUTTONS['X'], None
        if frame < 98: return 0, (80,60)
        if frame < 100: return BUTTONS['B'], (120,90)
        if frame < 110: return 0, None
        if frame < 112: return BUTTONS['Select'], None
        if frame < 130: return BUTTONS['Up'], None
        # An occasional safe turn/move after pause, then stationary samples.
        if 185 <= frame < 205: return BUTTONS['Y'] | BUTTONS['Up'], None
        if 305 <= frame < 325: return BUTTONS['A'] | BUTTONS['Down'], None
        return 0, None


def verify_memory(probe, elf):
    static_end, _ = transport.elf_symbol(elf, '__end__')
    linker_end, _ = transport.elf_symbol(elf, '__eheap_end')
    if (probe['region_base'], probe['region_limit'], linker_end) != (0x02000000, 0x02380000, 0x02380000):
        raise AssertionError('N09 ARM9 region policy differs')
    if static_end != probe['static_end'] or not static_end <= probe['heap_start'] <= probe['heap_end'] <= probe['heap_limit_final']:
        raise AssertionError('N09 runtime heap/ELF ordering')
    if probe['heap_limit_policy'] != probe['region_limit'] or probe['heap_limit_before'] < probe['region_limit']:
        raise AssertionError('N09 SDK heap clamp omitted')
    if probe['reserve_bytes'] != 655360 or probe['heap_limit_final'] != probe['region_limit']-655360:
        raise AssertionError('N09 reserve differs')
    if probe['managed_bytes'] != 2293760 or probe['managed_base'] % 32 or not probe['heap_start'] <= probe['managed_base'] <= probe['heap_end']-2293760:
        raise AssertionError('N09 managed slabs differ')
    startup = probe['heap_end']-probe['region_base']-probe['managed_bytes']
    if startup != probe['static_startup_bytes'] or startup > 524288: raise AssertionError('N09 startup ceiling exceeded')
    available = probe['heap_limit_final']-probe['heap_end']
    if available != probe['sdk_heap_available'] or available < 196608 or probe['sdk_heap_min'] != 196608:
        raise AssertionError('N09 SDK growth headroom differs')
    if startup+2293760+available+655360 != 3670016: raise AssertionError('N09 RAM byte closure failed')
    for name, ceiling in [('world',393216),('scratch',131072),('scene',262144),('assets',1048576)]:
        if not 0 <= probe[name+'_used'] <= probe[name+'_high_water'] <= ceiling: raise AssertionError('N09 typed slab ceiling')
    if probe['events_bank_bytes'] != 36864: raise AssertionError('N09 event slab type differs')
    return dict(static_startup_bytes=startup, managed_bytes=2293760, sdk_heap_available=available,
                reserve_bytes=655360, closure_bytes=3670016)


def verify_trace(rows):
    if not rows: raise AssertionError('N09 trace empty')
    from audit_movement import Original, FIELDS
    original = Original('ru')
    current = {name: signed(rows[0][name]) if name in SIGNED else rows[0][name] for name, _, _ in FIELDS}
    presses, releases, held, uses, blocked, touch_blocked = [0]*12, [0]*12, 0, 0, 0, False
    callbacks, raw_samples, pauses, resumes, renders = [], [], [], [], []
    input_checks = camera_checks = clock_checks = 0
    snapshots = []
    ratio = Fraction(1193182*64,119*33513982)
    previous = None
    for row in rows:
        if previous is not None:
            delta = u64(row,'clock_last_counter')-u64(previous,'clock_last_counter')
            if delta < 0: raise AssertionError('N09 clock counter regressed')
            ticks, fraction = previous['native_now'], u64(previous,'fraction')
            if not previous['paused']:
                whole, fraction = divmod(delta*ratio.numerator+fraction,ratio.denominator)
                ticks=(ticks+whole)&0xffffffff
            if (ticks,fraction) != (row['native_now'],u64(row,'fraction')):
                raise AssertionError(('N09 exact rational clock mismatch', row['ordinal']))
            clock_checks += 1
        if (u64(row,'rate_numerator'),u64(row,'rate_denominator')) != (ratio.numerator,ratio.denominator):
            raise AssertionError('N09 clock rate differs')
        if row['action'] == 1:
            raw_samples.append(row)
            if len(raw_samples) > 1:
                old=raw_samples[-2]
                if u64(row,'raw_sequence') != u64(old,'raw_sequence')+1 or u64(row,'sample_counter') < u64(old,'sample_counter'):
                    raise AssertionError('N09 source input ordering differs')
                if row['raw_pressed'] != (row['raw_held'] & ~old['raw_held']) or row['raw_released'] != (old['raw_held'] & ~row['raw_held']):
                    raise AssertionError('N09 actual raw button fronts differ')
            expected = [row[key] & ~blocked for key in ('raw_held','raw_pressed','raw_released')]
            if expected != [row[key] for key in ('physical_held','physical_pressed','physical_released')]:
                raise AssertionError(('N09 UI suppression differs',row['ordinal'],expected))
            touch = 0 if touch_blocked else row['raw_touch']
            if touch != row['physical_touch']: raise AssertionError('N09 touch UI suppression differs')
            blocked &= row['raw_held']
            if not row['raw_touch'] & 1: touch_blocked = False
            if row['status'] == 2 or row['raw_pressed'] & 8:
                blocked |= row['raw_held']; touch_blocked |= bool(row['raw_touch'] & 1)
                presses,releases,held,uses=[0]*12,[0]*12,0,0
            else:
                held=expected[0]
                for bit in range(12):
                    presses[bit] += bool(expected[1] & (1<<bit))
                    releases[bit] += bool(expected[2] & (1<<bit))
                uses += bool(expected[2] & ((1<<10)|(1<<1)))
            input_checks += 1
        elif row['action'] == 2:
            if row['paused'] or row['native_interval'] != 416 or row['native_now'] < row['native_deadline']:
                raise AssertionError('N09 callback violated scheduler boundary')
            pressed=sum((1<<bit) for bit in range(12) if presses[bit])
            released=sum((1<<bit) for bit in range(12) if releases[bit])
            command=native(held|pressed)|(0x800 if uses else 0)
            if [held,pressed,released,command,int(bool(uses)),int(bool(pressed&4))] != [row[k] for k in
                    ('physical_held','physical_pressed','physical_released','native_commands','use_triggered','next_weapon_pressed')]:
                raise AssertionError(('N09 control latch differs',row['ordinal'],command,row['native_commands']))
            presses=[max(0,v-1) for v in presses];releases=[max(0,v-1) for v in releases];uses=max(0,uses-1)
            before_flags=(current['flags']&~0x00182a3c)|command
            if row['input_flags'] != before_flags: raise AssertionError('N09 input flags before original arithmetic differ')
            values=dict(current, flags=before_flags, elapsed=row['service_elapsed'])
            result=original.run(values)['state']
            for name,_,_ in FIELDS:
                actual=signed(row[name]) if name in SIGNED else row[name]
                if result[name] != actual: raise AssertionError(('N09 ARM9 differs from original x86',row['ordinal'],name,result[name],actual))
            if (result['moved'],result['substep_count']) != (row['moved'],row['substeps']): raise AssertionError('N09 native substep result differs')
            if bool(row['first_service']) != (not callbacks) or (not callbacks and row['service_elapsed'] != 0):
                raise AssertionError('N09 first stopwatch interval differs')
            expected_elapsed = 0 if not callbacks else (row['service_tick']-callbacks[-1]['watch_start'])&0xffffffff
            if row['service_elapsed'] != expected_elapsed or row['watch_start'] != row['native_now'] or row['watch_started'] != 1:
                raise AssertionError('N09 live stopwatch start/read boundaries differ')
            if (row['native_now']-row['service_tick'])&0xffffffff >= 0x80000000:
                raise AssertionError('N09 native service completion predates start')
            current={name:result[name] for name,_,_ in FIELDS}
            snapshots.append((row['native_now'],(current['x'],current['y'],current['z'],current['heading']%360*65536)))
            callbacks.append(row)
        elif row['action'] == 3:
            renders.append(row)
            if snapshots:
                tick,pose=snapshots[-1]; expected=list(pose)
                if len(snapshots)>1:
                    old_tick,old=snapshots[-2]
                    interval=(tick-old_tick)&0xffffffff
                    elapsed=min((row['native_now']-tick)&0xffffffff,416)
                    if interval:
                        for i in range(3): expected[i] += trunc((pose[i]-old[i])*elapsed,interval)
                        angle=pose[3]-old[3]
                        if angle>180*65536: angle-=360*65536
                        if angle< -180*65536: angle+=360*65536
                        expected[3]=(pose[3]+trunc(angle*elapsed,interval))%(360*65536)
                actual=[signed(row[k]) for k in ('camera_x','camera_y','camera_z')]+[row['camera_heading_q16']]
                if actual != expected: raise AssertionError(('N09 presentation prediction differs',row['ordinal'],actual,expected))
                camera_checks += 1
        elif row['action'] == 4: pauses.append(row)
        elif row['action'] == 5: resumes.append(row)
        # Motion pose can only change at a service, never through touch/render/UI.
        if row['action'] != 2 and any(signed(row[k]) != current[k] for k in ('x','y','z','heading')):
            raise AssertionError('N09 non-service action mutated simulation pose')
        previous=row
    if len(pauses)!=2 or len(resumes)!=2: raise AssertionError('N09 requires two pause/resume cycles')
    for pause,resume in zip(pauses,resumes):
        if (pause['native_now'],u64(pause,'fraction'),pause['callback_total']) != (resume['native_now'],u64(resume,'fraction'),resume['callback_total']):
            raise AssertionError('N09 pause advanced simulation/world time')
        if resume['vblank_count']-pause['vblank_count']<35: raise AssertionError('N09 pause observation too short')
    if len(callbacks)<100 or len(renders)<400 or input_checks<400 or camera_checks<350: raise AssertionError('N09 insufficient live coverage')
    short_taps=[]
    for before,after in zip(callbacks,callbacks[1:]):
        samples=[row for row in raw_samples if before['ordinal']<row['ordinal']<after['ordinal']]
        pressed,released=0,0
        for sample in samples:
            pressed|=sample['physical_pressed'];released|=sample['physical_released']
        if pressed&released&(1<<8):
            if not after['physical_pressed']&(1<<8) or not after['physical_released']&(1<<8) or not after['native_commands']&0x200:
                raise AssertionError('N09 short Fire press/release was lost before next service')
            source_ticks=[]
            for sample in samples:
                if (sample['physical_pressed']|sample['physical_released'])&(1<<8):
                    delta=u64(sample,'sample_counter')-u64(before,'clock_last_counter')
                    whole=(delta*ratio.numerator+u64(before,'fraction'))//ratio.denominator
                    tick=(before['native_now']+whole)&0xffffffff
                    if not before['service_tick']<tick<after['service_tick']:
                        raise AssertionError('N09 short Fire source timestamp is not strictly between service starts')
                    source_ticks.append(tick)
            short_taps.append(dict(before=before['ordinal'],after=after['ordinal'],buttons=pressed&released,
                                   before_service_tick=before['service_tick'],source_ticks=source_ticks,
                                   after_service_tick=after['service_tick']))
    if not short_taps: raise AssertionError('N09 live route did not capture a complete Fire tap between services')
    return dict(original_x86_callbacks=len(callbacks), input_samples=input_checks, presentation_predictions=camera_checks,
                rational_clock_transitions=clock_checks, pauses=2, resumes=2,
                uses=sum(r['use_triggered'] for r in callbacks),next_weapon=sum(r['next_weapon_pressed'] for r in callbacks),
                short_fire_taps=short_taps)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,required=True)
    parser.add_argument('--rom',type=Path,default=OUT/'dok_nds_movement.nds')
    parser.add_argument('--elf',type=Path,default=OUT/'dok_nds_movement.elf')
    args=parser.parse_args()
    path=OUT/'emulator';directory(path);target=safe_output(path/'result.json')
    report=dict(task='N09',passed=False,command=sys.argv,limitations=[
        'Emulator DS mode only; physical DS/DS Lite, flashcart and actual FPS remain N24.',
        'Diagnostic free flight; original collision/occupancy gameplay integration remains N10.',
        'Fire/Use/next weapon are commands; N11/N15 execute their gameplay behavior.',
        'Bottom screen is diagnostic text until N20 map; touch never rotates camera.'])
    frontend=fixture=None
    try:
        if transport.sha(args.core)!=CORE_SHA: raise ValueError('N09 requires accepted melonDS DS libretro1.4.0 core')
        header=ROOT/'ports/nds/platform/movement_probe/movement_probe.h'
        pf,tf=fields(header,'NdsMovementProbe'),fields(header,'NdsMovementTraceRecord')
        address,size=transport.elf_symbol(args.elf,'g_nds_movement_probe')
        if size!=len(pf)*4: raise ValueError('N09 header/ELF observation mismatch')
        host_path=ROOT/'research/evidence/nds/n09/host-check.json'
        host=json.loads(host_path.read_text('utf8'))
        if not host.get('passed') or 'arm9_build' not in host: raise ValueError('N09 needs passed matching host/ARM9 gate')
        pins=host['sources']+host['arm9_build']['artifacts']
        for row in pins:
            if transport.sha(ROOT/row['path'])!=row['sha256']: raise ValueError('N09 gate input/artifact changed: '+row['path'])
        package=OUT/'fixture/assets.ndp';packet=OUT/'fixture/scene.dat'
        if transport.sha(package)!=PACKAGE_SHA or packet.read_bytes()!=(ROOT/'build/nds/n07/scene.dat').read_bytes():
            raise ValueError('N09 fixture differs from accepted assets/packet')
        report.update(rom_sha256=transport.sha(args.rom),elf_sha256=transport.sha(args.elf),core_sha256=CORE_SHA,
                      harness_sha256=transport.sha(Path(__file__)),abi_sha256=transport.sha(header),
                      host_gate_sha256=transport.sha(host_path),packet_sha256=transport.sha(packet),package_sha256=PACKAGE_SHA)
        frontend=transport.Frontend(args.core,path,'happy')
        fixture=safe_output(frontend.save/'melonDS DS/dldi_sd_card.bin')
        fat.write_image(fixture,[('assets.ndp',b'ASSETS  NDP',package.read_bytes()),('scene.dat',b'SCENE   DAT',packet.read_bytes())])
        frontend.load(args.rom);transport.flush_native_stdio()
        report['fat_after_load_sha256']=transport.sha(fixture)
        rows,observations,inputs,images=[],[],[],[]
        route=Route();probe=None;completed=0
        for frame in range(2400):
            joypad,touch=route.input(frame,probe)
            frontend.joypad,frontend.touch=joypad,touch
            if not inputs or (inputs[-1]['joypad'],inputs[-1]['touch'])!=(joypad,touch):
                inputs.append(dict(frontend_frame=frame,joypad=joypad,touch=touch,phase=route.phase))
            if frontend.frame_time and frontend.frame_time.callback:
                frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run();frontend.frames+=1
            if frontend.callback_error or frontend.shutdown: raise RuntimeError(frontend.callback_error or 'Core shutdown')
            probe=observe(frontend,address,pf,0x394d4b44)
            if probe is None: continue
            report['last_observation']=probe
            if probe['status']==4 or probe['error_bits'] or probe['failures']: raise AssertionError('N09 ARM9 probe reported failure')
            # Each ring record publishes independently before the GPU work.
            # The top probe may lag until end-of-frame; do not delay input
            # feedback by waiting for its later trace_total publication.
            while True:
                ordinal=len(rows)+1
                row=observe(frontend,probe['trace_address']+((ordinal-1)%512)*len(tf)*4,tf,0x39564b44)
                if row is None: break
                if row['ordinal']<ordinal: break
                if row['ordinal']>ordinal: raise AssertionError('N09 trace ring overwritten before observation')
                row['observed_frontend_frame']=frame;rows.append(row)
                if row['action']==1: route.latest_input=row
                if row['action']==2: route.latest_callback=row
            if probe['status']:
                observations.append(probe)
                if frontend.core.retro_get_memory_size(2)!=4194304 or probe['is_dsi']: raise AssertionError('N09 did not execute ordinary DS mode')
            for name,condition in [('start',probe['status']==1 and probe['frame_id']>=3),
                                   ('moving',probe['status']==1 and probe['frame_id']>=35),
                                   ('paused',probe['status']==2),('resumed',probe['resume_count']>=1 and probe['status']==1)]:
                if condition and not any(x['name']==name for x in images):
                    images.append(dict(name=name,files=capture(frontend,name),video=verify_video(frontend,packet.read_bytes())))
            if probe['status']==3:
                completed+=1
                if completed>=3: break
        if probe is None or probe['status']!=3 or len(rows)!=probe['trace_total']: raise AssertionError('N09 did not complete with all trace records')
        if probe['trace_capacity']!=512 or probe['trace_record_bytes']!=len(tf)*4: raise AssertionError('N09 trace ABI differs')
        if probe['buttons_seen']&0xfff!=0xfff or not probe['touch_seen']&1: raise AssertionError('N09 did not observe all12buttons/touch')
        if probe['frame_id']<400 or probe['render_overflows'] or probe['matrix_errors']: raise AssertionError('N09 actual3D rendering failed')
        report['trace_checks']=verify_trace(rows)
        if len({row['video']['scene_rgb_sha256'] for row in images})<2: raise AssertionError('N09 actual camera images did not change')
        if (report['trace_checks']['uses'],report['trace_checks']['next_weapon'])!=(probe['use_total'],probe['next_weapon_total']): raise AssertionError('N09 action counters differ from traces')
        report['memory']=verify_memory(probe,args.elf)
        for field,key in [('input_checks','input'),('motion_checks','motion'),('replay_checks','replay')]:
            if probe[field]!=host['shared_suites'][key]['checks']: raise AssertionError('N09 shared host/ARM9 suite count differs')
        report.update(traces=rows,observations=observations,inputs=inputs,route_events=route.events,
                      images=images+[dict(name='complete',files=capture(frontend,'complete'),video=verify_video(frontend,packet.read_bytes()))],
                      core_name=frontend.info.library_name.decode(),core_version=frontend.info.library_version.decode())
        for row in pins:
            if transport.sha(ROOT/row['path'])!=row['sha256']: raise ValueError('N09 source/artifact changed during runtime')
        transport.flush_native_stdio()
        if transport.sha(fixture)!=report['fat_after_load_sha256']: raise AssertionError('N09 runtime modified readonly FAT')
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
                if fixture and 'fat_after_load_sha256' in report:
                    report['fat_after_unload_sha256']=transport.sha(fixture)
                    if report['fat_after_unload_sha256']!=report['fat_after_load_sha256']: raise AssertionError('N09 unload modified FAT')
            except Exception:
                report['cleanup_error']=traceback.format_exc();report['passed']=False
        target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(f"N09 emulator: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
