"""G20A hypothesis audit: isolated renderer experiments, not release defaults.

Reuses object files of a specified current build, recompiles private source
copies and links all variants in the same order. No original EXE analysis.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import struct
import subprocess
import sys

from profile_game_gba import GameProbe, collect, freeze
from verify_audio_gba import sounds, irq_stack
from verify_gba import sha, png
from verify_renderer_gba import stack_usage
from verify_sprites_gba import load_json

ROOT = Path(__file__).resolve().parents[1]


def command(args):
    subprocess.run(list(map(str, args)), cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)


def builds(base, out, zig):
    motion = ('flow flow_backend ui ui_backend audio sound audio_backend ai ai_probe '
              'combat combat_probe items movement collision interactions movement_probe '
              'movement_cases collision_cases interactions_cases').split()
    names = ['startup', 'main', 'ui-icons', 'animation-index', 'render-tables', 'divmod', 'columns']
    names += ['motion-' + n for n in motion]
    names += ['time-scheduler', 'time-clock', 'gba-clock', 'time-time_probe', 'time-time_cases']
    names += ['memory', 'runtime', 'rom_assets', 'projection', 'image', 'animation', 'visual', 'renderer', 'assets-data']
    assert all((base / (n + '.o')).is_file() for n in names)
    flags = ['cc', '-target', 'arm-freestanding-eabi', '-mcpu=arm7tdmi', '-std=c11',
             '-Wall', '-Wextra', '-Werror', '-O2', '-g', '-ffreestanding',
             '-fno-stack-protector', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
             '-fno-pic', '-I', 'include', '-DDOK_RENDERER_PROBE',
             '-DDOK_RENDER_HALF_COLUMNS', '-DDOK_RUNTIME_ROM_ONLY', '-c']
    for variant in ('baseline', 'no_scales', 'unlit', 'flat_surfaces', 'ai_half_range'):
        dest = out / variant
        private = dest / 'source'
        private.mkdir(parents=True, exist_ok=True)
        for src in (ROOT / 'src').glob('renderer*'):
            shutil.copy2(src, private / src.name)
        if variant == 'no_scales':
            path = private / 'renderer.c'
            s = path.read_text(encoding='utf-8')
            old = 'if(!crop_y && im->scales && im->scales[vstart] && bottom>top)'
            assert s.count(old) == 1
            path.write_text(s.replace(old, 'if(0 && !crop_y && im->scales && im->scales[vstart] && bottom>top)'), encoding='utf-8')
        if variant == 'unlit':
            path = private / 'renderer_surfaces.inc'
            s = path.read_text(encoding='utf-8')
            old = 'static unsigned light_index(int32_t depth) {return dok_renderer_light_row(depth,1920);}'
            assert s.count(old) == 1
            path.write_text(s.replace(old, 'static unsigned light_index(int32_t depth) {(void)depth;return 0;}'), encoding='utf-8')
            path = private / 'renderer_surface_raster.inc'
            s = path.read_text(encoding='utf-8')
            s = s.replace('lut[im->pixels[(unsigned)ty*im->width+(unsigned)tx]]',
                          'im->pixels[(unsigned)ty*im->width+(unsigned)tx]')
            path.write_text(s, encoding='utf-8')
        columns = (ROOT / 'gba/columns.S').read_text(encoding='utf-8')
        if variant == 'unlit':
            assert columns.count('    ldrb lr,[r2,lr]') == 1
            columns = columns.replace('    ldrb lr,[r2,lr]', '    @ audit: identity lighting, no LUT load')
        if variant == 'flat_surfaces':
            first = columns.index('    mov r12,r3,asr #16', columns.index('dok_gba_surface_span:'))
            last = columns.index('    ldrb lr,[r2,lr]', first)
            columns = columns[:first] + '    ldrb lr,[r1] @ audit: constant source texel\n' + columns[last:]
            path = private / 'renderer_surface_raster.inc'
            s = path.read_text(encoding='utf-8').replace(
                'lut[im->pixels[(unsigned)ty*im->width+(unsigned)tx]]', 'lut[im->pixels[0]]')
            path.write_text(s, encoding='utf-8')
        (private / 'columns.S').write_text(columns, encoding='utf-8')
        command([zig, *flags, private / 'renderer.c', '-o', dest / 'renderer.o'])
        command([zig, 'cc', '-target', 'arm-freestanding-eabi', '-mcpu=arm7tdmi',
                 '-DDOK_RENDERER_PROBE', '-DDOK_RENDER_HALF_COLUMNS', '-c',
                 private / 'columns.S', '-o', dest / 'columns.o'])
        objects = [(dest if n in ('renderer', 'columns') else base) / (n + '.o') for n in names]
        if variant == 'ai_half_range':
            shutil.copy2(ROOT / 'src/ai_atan.inc', private / 'ai_atan.inc')
            ai = (ROOT / 'src/ai.c').read_text(encoding='utf-8')
            old = 'dok_ai_near(p->x-m->x,p->z-m->z,field(a,id,0xba))'
            assert ai.count(old) == 1
            (private / 'ai.c').write_text(ai.replace(old, 'dok_ai_near(p->x-m->x,p->z-m->z,field(a,id,0xba)/2)'), encoding='utf-8')
            ai_flags = [f.replace('arm-freestanding-eabi','thumb-freestanding-eabi') for f in flags]
            command([zig,*ai_flags,private/'ai.c','-o',dest/'motion-ai.o'])
            objects[names.index('motion-ai')] = dest/'motion-ai.o'
        command([zig, 'ld.lld', '-T', 'gba/gba.ld', '--gc-sections',
                 '--defsym=__iwram_reserved_override=8192',
                 '-Map=' + str(dest / 'dok-diagnostic.map'), *objects,
                 '-o', dest / 'dok-diagnostic.elf'])
        command([zig, 'objcopy', '-O', 'binary', dest / 'dok-diagnostic.elf', dest / 'dok-diagnostic.gba'])
        command([sys.executable, 'scripts/gba_rom.py', '--elf', dest / 'dok-diagnostic.elf',
                 '--rom', dest / 'dok-diagnostic.gba', '--out', dest / 'build-report.json'])
        print('Built ' + variant, flush=True)


def measure(build, core, cameras, expected, primitive=False):
    p = GameProbe(core, build)
    rows = []
    try:
        assert 'g_hw_obj_diag' not in p.symbols,'Use benchmark_gba_hardware.py for actual tiled PPU output'
        p.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.camera(1, 15120, -300, 22800, 90)
        if primitive:
            p.write(p.symbols['g_render_work'], struct.pack('<I', 1))
        for case in cameras:
            p.language(case['language'])
            p.camera(case['level'], *case['camera'])
            p.present()
            values, stages = [], []
            for _ in range(3):
                d = p.present()
                values.append(d['last_cycles'])
                t = p.words('g_render_completed_stages', 7)
                stages.append([t[0]] + [b-a for a, b in zip(t, t[1:])])
            raw = p.frontend.read(0x06000000 + d['visible_page'] * 0xa000, 38400)
            pal = struct.unpack('<256H', p.frontend.read(0x05000000, 512))
            rgb = bytes(v for index in raw for v in tuple(((pal[index] >> shift) & 31) * 255 // 31 for shift in (0,5,10)))
            png(build / (case['name'] + '.png'), 240, 160, rgb)
            (build / (case['name'] + '.idx')).write_bytes(raw)
            reference = expected / (case['name'] + '.idx')
            prior = reference.read_bytes() if reference.is_file() else raw
            row = dict(**case, samples=values, median=statistics.median(values),
                       stages=dict(zip(('logic','resources','geometry','sort','raster','ui','copy'),
                                       map(statistics.median, zip(*stages)))),
                       changed_scene_pixels=sum(a != b for a,b in zip(raw[:34560],prior[:34560])),
                       frame_sha256=hashlib.sha256(raw).hexdigest(),
                       faces=d['face_count'], pixels=d['pixels'])
            if primitive:
                work=p.words('g_render_work',5)
                row['primitive_work']=dict(wall_cycles=work[1]*64, surface_cycles=work[2]*64,
                                           walls=work[3], surfaces=work[4])
            rows.append(row)
        return dict(rom_sha256=sha(p.rom), rows=rows, stack=stack_usage(p.frontend))
    finally:
        p.close()


def asset_audit(base):
    data = Path('build/gba/assets/assets.bin').read_bytes()
    u16 = lambda n: struct.unpack_from('<H',data,n)[0]
    u32 = lambda n: struct.unpack_from('<I',data,n)[0]
    directory, views = u32(24),u32(28)
    groups,animations = {},{}
    for view in range(6):
        rec=views+view*80
        defs,bindings,count=u32(rec+36),u32(rec+72),u16(rec+76)
        for i in range(count):
            obj,slot,rid=struct.unpack_from('<3H',data,bindings+i*8)
            if obj==65535 or slot not in (0xfff0,0xfff1,0xe8,0xec):continue
            cls=u16(defs+obj*32)
            entry=directory+(rid-1)*32+16+(view%2)*8
            at,size=u32(entry),u32(entry+4)
            payload=data[at:at+size]
            if slot!=0xfff0:
                n=struct.unpack_from('<H',payload)[0]
                offset=8+10*n
                for _ in range(n):
                    w,h=struct.unpack_from('<HH',payload,offset)
                    animations.setdefault(str(cls),Counter())[f'{w}x{h}']+=1
                    offset+=18+w*h
                assert offset==size
                continue
            w,h=struct.unpack_from('<HH',payload)
            if size==18+w*h:
                groups.setdefault(str(cls),Counter())[f'{w}x{h}']+=1
    return dict(package_sha256=hashlib.sha256(data).hexdigest(),
                static_sprite_bindings_by_class={k:dict(v) for k,v in groups.items()},
                animation_frame_bindings_by_class={k:dict(v) for k,v in animations.items()},
                render_bank=load_json(base/'render-columns.json'))


def live_measure(build,core,disable_ai=False,follow_camera=False,samples=24,buttons=(8,7)):
    p=GameProbe(core,build)
    rows=[]
    try:
        p.until(lambda d:d['draws']>=4 and ('g_hw_obj_diag' in p.symbols or d['phase']==2))
        rate=max(sounds()[0],key=lambda r:r['rate'])
        for language in (0,1):
            for level in range(3):
                freeze(p,True)
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
                p.language(language)
                hero=load_json(f'build/runtime/ru/LEVEL{level+1}-runtime.json')
                hero=hero['objects'][hero['player_index']-1]['actor']
                p.camera((level+1)%3,15120,-300,22800,90)
                p.camera(level,hero['x'],hero['y'],hero['z'],hero['heading'])
                p.command(6)
                p.command(2,rate['id'])
                freeze(p,False)
                if follow_camera:
                    assert 'test_camera' in p.symbols
                    p.write(p.symbols['test_camera'],b'\0')
                if disable_ai:p.write(p.symbols['g_ai_probe'],struct.pack('<I',1))
                # Discard transition frames; no capture hold during measurements.
                collect(p,6,buttons=buttons)
                position_before={k:p.diag()[k] for k in ('player_x','player_y','player_z')}
                bg_before=list(p.words('g_hw_bg_diag',8)) if 'g_hw_bg_diag' in p.symbols else None
                row=collect(p,samples,buttons=buttons)
                row.update(level=level+1,language=language,ai_disabled=disable_ai,camera_follows_runtime=follow_camera)
                row['buttons']=list(buttons);row['player_before']=position_before
                row['player_after']={k:p.diag()[k] for k in position_before}
                if bg_before is not None:
                    after=list(p.words('g_hw_bg_diag',8));row['hardware_bg_after']=after
                    row['hardware_bg_new_cold_loads']=after[1]-bg_before[1];row['hardware_bg_new_overflows']=after[4]-bg_before[4]
                row['stage_medians']={key:statistics.median(r['stages'][key] for r in row['samples'])
                                      for key in row['samples'][0]['stages']}
                rows.append(row)
        return dict(rom_sha256=sha(p.rom),rows=rows,stack=stack_usage(p.frontend),irq_stack=irq_stack(p))
    finally:p.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,default=Path('build/level1-fixes'))
    parser.add_argument('--out',type=Path,default=Path('build/g20a-audit'))
    parser.add_argument('--report',type=Path,default=Path('analysis/g20a-fps-audit.json'))
    parser.add_argument('--skip-build',action='store_true')
    a=parser.parse_args()
    os.chdir(ROOT)
    os.environ['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/zig-cache')
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    if not a.skip_build:builds(a.base,a.out,zig)
    previous=load_json('analysis/g20-cameras.json')['after']['rows']
    cameras=[]
    for level in range(3):
        candidates=[r for r in previous if r['language']==0 and r['level']==level and '-start-' in r['name']]
        candidates.sort(key=lambda r:r['median'])
        for kind,index in (('best',0),('median',len(candidates)//2),('worst',-1)):
            r=candidates[index]
            cameras.append(dict(name=f'L{level+1}-{kind}',level=level,language=0,camera=r['camera']))
    results={}
    for variant in ('baseline','no_scales','unlit','flat_surfaces'):
        results[variant]=measure(a.out/variant,core,cameras,a.out/'baseline')
        print(variant+': '+str([r['median'] for r in results[variant]['rows']]),flush=True)
    results['primitive_profile']=measure(a.out/'baseline',core,cameras,a.out/'baseline',True)
    assert sha(a.out/'baseline/dok-diagnostic.gba') == sha(a.base/'dok-diagnostic.gba'), 'Baseline must reproduce supplied build exactly'
    live={}
    for variant in ('baseline','no_scales','unlit','flat_surfaces','ai_half_range','no_ai'):
        live[variant]=live_measure(a.out/('baseline' if variant=='no_ai' else variant),core,variant=='no_ai')
        print('Live '+variant+': '+str([(r['level'],r['language'],round(r['presented_fps'],2),r['max_ai_owners']) for r in live[variant]['rows']]),flush=True)
    report=dict(task='G20A-hypothesis-audit',status='measured-experiments-no-release-change',
                script_sha256=sha(__file__),base_rom_sha256=sha(a.base/'dok-diagnostic.gba'),
                core_sha256=sha(core),assets=asset_audit(a.base),results=results,live=live,
                sources={str(p):sha(p) for p in [*Path('src').glob('renderer*'),Path('gba/columns.S'),Path('src/ai.c'),Path('gba/ai_probe.c'),Path('scripts/verify_sprites_gba.py'),Path('scripts/profile_game_gba.py')]},
                limitations=['Frozen RU scenes, nine G20-ranked start cameras; not a full-game FPS result.',
                             'Timer includes full frame CPU but excludes capture/presentation wait.',
                             'Primitive attribution has overhead and is a separate run.',
                             'Unlit and flat surfaces are diagnostic extremes, not accepted quality settings.',
                             'Live fixtures use original start positions, a stationary test camera, turning/fire and 22050 Hz PCM; not a whole-level playthrough.',
                             'No physical GBA test; mGBA replacement BIOS.'])
    for b,r in zip(results['baseline']['rows'],results['no_scales']['rows']):
        assert b['frame_sha256']==r['frame_sha256'],(b['name'],'no_scales pixels changed')
    a.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Report '+str(a.report),flush=True)


if __name__=='__main__':main()
