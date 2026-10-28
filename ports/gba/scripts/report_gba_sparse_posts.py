"""Validate linked G20C data and collect the bounded negative native trial."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import struct

from benchmark_gba_sparse_posts import CONTROL
from gba_rom import elf_info
from verify_gba import sha


def read(path):return json.loads(path.read_text(encoding='utf-8'))


def linked_posts(folder,build):
    _,symbols,_=elf_info(build/'dok-diagnostic.elf')
    rom=(build/'dok-diagnostic.gba').read_bytes()
    for symbol,filename in (('dok_sparse_image_offsets','sparse-offsets.bin'),
                            ('dok_sparse_posts','sparse-posts.bin'),('dok_prelit_bank','prelit.bin')):
        blob=(folder/filename).read_bytes();off=symbols[symbol]-0x08000000
        assert rom[off:off+len(blob)]==blob,('linked payload',symbol)
    source=(folder/'prelit.bin').read_bytes();posts=(folder/'sparse-posts.bin').read_bytes()
    offsets=(folder/'exact-offsets.bin').read_bytes();tables=(folder/'sparse-offsets.bin').read_bytes()
    package=Path('build/gba/assets/assets.bin').read_bytes()
    count=struct.unpack_from('<I',package,20)[0];directory=struct.unpack_from('<I',package,24)[0]
    checked=set();columns=texels=0;records=set()
    for index in range(len(offsets)//4):
        base=struct.unpack_from('<I',offsets,index*4)[0];table=struct.unpack_from('<I',tables,index*4)[0]
        if not base:assert table==0;continue
        view=index//((count+1)*16);rid=index//16%(count+1)
        start=struct.unpack_from('<I',package,directory+(rid-1)*32+16+(view%2)*8)[0]
        width,height=struct.unpack_from('<HH',package,start)
        if (base,width,height) in checked:continue
        checked.add((base,width,height));assert table and table%4==0
        for x in range(width):
            record=struct.unpack_from('<I',posts,table+x*4)[0];records.add(record)
            n=struct.unpack_from('<H',posts,record)[0];assert record%2==0
            restored=bytearray(height);previous=0
            for k in range(n):
                lo,hi=struct.unpack_from('<HH',posts,record+2+k*4)
                assert previous<=lo<hi<=height
                if k:assert previous<lo
                pixels=source[base+x*height+lo:base+x*height+hi];assert all(pixels)
                restored[lo:hi]=pixels;previous=hi
            assert restored==source[base+x*height:base+(x+1)*height]
            columns+=1;texels+=height
    return dict(status='passed',images=len(checked),columns=columns,texels=texels,
                unique_records=len(records),auxiliary_bytes=len(posts)+len(tables),
                linked_payloads_exact=True,all_16_light_rows=True)


def paired(before,after):
    assert len(before)==len(after)
    keys=('completed_ppu_sha256','coverage_sha256','foreground_sha256','completed_renderer_stats')
    for b,a in zip(before,after):
        assert tuple(b[k] for k in ('level','language','cell','heading'))==tuple(a[k] for k in ('level','language','cell','heading'))
        for key in keys:assert b[key]==a[key],(key,a['level'],a['cell'],a['heading'])
        assert a['audio']['underruns']==b['audio']['underruns']==0
    return dict(pairs=len(before),ppu_rgb_exact=True,coverage_exact=True,
                foreground_indices_exact=True,public_renderer_stats_exact=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=Path('research/experiments/g20c-sparse-posts'))
    p.add_argument('--repeat',type=Path,default=Path('research/experiments/g20c-sparse-repeat'))
    p.add_argument('--report',type=Path,default=Path('analysis/gba/g20c-sparse-posts.json'))
    p.add_argument('--copy-rom',action='store_true')
    a=p.parse_args();out=a.out
    variants=('hw_v128_entity_rect','hw_v128_sparse_control','hw_v128_sparse_posts')
    builds={v:read(out/v/'build-report.json') for v in variants}
    trial=builds[variants[-1]]['rom_sha256']
    assert builds[variants[0]]['rom_sha256']==CONTROL
    for v,b in builds.items():
        assert sha(out/v/'dok-diagnostic.gba')==b['rom_sha256']
        assert sha(a.repeat/v/'dok-diagnostic.gba')==b['rom_sha256']
        assert read(a.repeat/v/'build-report.json')['sections']==b['sections']
    checks={n:read(out/(n+'.json')) for n in ('arm','sprites','interactions','ai-combat','ui','mutations','transitions')}
    for n,c in checks.items():assert c['status']=='passed' and c['rom_sha256']==trial,n
    details=read(out/'ppu-details.json')
    assert details['before']['rom_sha256']==CONTROL and details['after']['rom_sha256']==trial
    assert len(details['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 and r['coverage_equal'] for r in details['after']['rows'])
    heavy=[];spatial=[];live={};inputs=[]
    for language in (0,1):
        reports={}
        for kind in ('heavy','spatial'):
            reports[kind]={}
            for variant in variants if kind=='heavy' else (variants[0],variants[-1]):
                path=out/f'{kind}-{variant}-{language}.json';inputs.append(path)
                result=read(path);assert result['rom_sha256']==builds[variant]['rom_sha256']
                assert result['script_sha256']==sha('ports/gba/scripts/benchmark_gba_spatial_sweep.py')
                assert result['capture_hold_during_samples'] is False
                reports[kind][variant]=result['rows']
            before=reports[kind][variants[0]];after=reports[kind][variants[-1]]
            exact=paired(before,after)
            if kind=='heavy':
                matched=reports[kind][variants[1]];paired(before,matched)
                for b,c,t in zip(before,matched,after):
                    heavy.append(dict(level=b['level'],language=language,cell=b['cell'],heading=b['heading'],
                        control_cpu_cycles=b['median'],placement_cpu_cycles=c['median'],trial_cpu_cycles=t['median'],
                        cpu_ratio=t['median']/b['median'],placement_cpu_ratio=c['median']/b['median'],
                        control_fps=b['presented_fps'],trial_fps=t['presented_fps'],exact_output=True))
            else:
                assert exact['pairs']==440
                for level in (1,2,3):
                    pairs=[(b,t) for b,t in zip(before,after) if b['level']==level]
                    spatial.append(dict(level=level,language=language,pairs=len(pairs),
                        cpu_ratio_median=statistics.median(t['median']/b['median'] for b,t in pairs),
                        cpu_ratio_min=min(t['median']/b['median'] for b,t in pairs),
                        cpu_ratio_max=max(t['median']/b['median'] for b,t in pairs),
                        faster=sum(t['median']<b['median'] for b,t in pairs),
                        slower=sum(t['median']>b['median'] for b,t in pairs),
                        equal=sum(t['median']==b['median'] for b,t in pairs),
                        control_minimum_fps=min(b['presented_fps'] for b,t in pairs),
                        trial_minimum_fps=min(t['presented_fps'] for b,t in pairs),**{k:v for k,v in exact.items() if k!='pairs'}))
    stacks=[];irq=[]
    for route in ('turn120','walking120'):
        values={}
        for v in (variants[0],variants[-1]):
            path=out/f'{route}-{v}.json';inputs.append(path);r=read(path)
            assert r['rom_sha256']==builds[v]['rom_sha256']
            for row in r['rows']:
                assert len(row['samples'])==120
                assert row['audio']['underruns']==row['time_conservation_error_ticks']==row['hardware_bg_new_overflows']==0
                assert all(s['flow_state']==2 and s['ui_state']==0 for s in row['samples'])
            values[v]=r
        live[route]=[]
        for b,t in zip(values[variants[0]]['rows'],values[variants[-1]]['rows']):
            assert (b['level'],b['language'])==(t['level'],t['language'])
            live[route].append(dict(level=b['level'],language=b['language'],
                control_fps=b['presented_fps'],trial_fps=t['presented_fps'],
                control_cpu_median=b['median'],trial_cpu_median=t['median'],
                control_sound_active_frames=b['sound_active_frames'],
                trial_sound_active_frames=t['sound_active_frames'],
                underruns=0,time_conservation_error_ticks=0))
        stacks.append(values[variants[-1]]['stack']);irq.append(values[variants[-1]]['irq_stack'])
    stacks += [c['stack'] for c in checks.values() if 'stack' in c]
    memory={}
    for v,b in builds.items():
        s=b['symbols'];iwram=s['__iwram_end']-s['__iwram_start']
        memory[v]=dict(rom_bytes=b['rom_bytes'],rom_sha256=b['rom_sha256'],
            ewram=b['ewram_static_bytes'],ewram_free=262144-b['ewram_static_bytes'],
            iwram=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192)
    sparse=linked_posts(out,out/variants[-1])
    # Confirm the installed playable/quality controls have not been replaced.
    artifacts={f.as_posix():sha(f) for f in Path('artifacts/gba').glob('*.gba')}
    assert artifacts['artifacts/gba/DoK-port_hardware_cached_prototype.gba']==CONTROL
    artifact=Path('artifacts/gba/DoK-port_sparse_posts_trial.gba')
    if a.copy_rom:shutil.copyfile(out/variants[-1]/'dok-diagnostic.gba',artifact)
    if artifact.exists():assert sha(artifact)==trial
    inputs += [out/(n+'.json') for n in checks]+[out/'ppu-details.json',out/'prototype-build.json',a.repeat/'prototype-build.json']
    sources=['ports/gba/scripts/report_gba_sparse_posts.py','ports/gba/scripts/benchmark_gba_sparse_posts.py',
             'ports/gba/scripts/benchmark_gba_spatial_sweep.py','ports/gba/scripts/gba_sparse_posts.py',
             'ports/gba/scripts/prototype_gba_bsp.py','ports/gba/scripts/verify_performance_arm.py',
             'ports/gba/src/renderer.c','ports/gba/src/renderer_rect.inc','ports/gba/src/renderer_sparse_posts.inc']
    result=dict(task='G20C',date='2026-10-27',status='complete-negative-native-trial',
        decision='Reject this source-run implementation; retain2775; next G20B UI.',
        control_rom_sha256=CONTROL,trial_rom_sha256=trial,
        core_sha256=read(out/'heavy-hw_v128_entity_rect-0.json')['core_sha256'],
        native_cpu_hz=16777216,waitcnt=4317,forced_pcm_hz=22050,
        audio_scope='Heavy/spatial: continuous forcedPCM. Live: initial22050Hz loop and normal gameplay audio/preemption; active presentation counts recorded.',
        package_sha256=sha('build/gba/assets/assets.bin'),
        quality=dict(logical_columns=120,cpu_rows=128,light_rows=16,geometry_rules_ai_distance_unchanged=True),
        sparse_format=sparse,heavy=heavy,spatial=spatial,live=live,memory=memory,
        observed_main_stack=max(s['high_water_bytes'] for s in stacks),
        observed_main_margin=min(s['untouched_margin_bytes'] for s in stacks),irq_stacks=irq,
        validation=dict(arm_sparse_vectors=checks['arm']['sparse_post_vectors'],
            arm_rectangle_vectors=checks['arm']['rectangle_raster_vectors'],
            spatial_ppu_pairs=880,heavy_ppu_pairs=10,detail_ppu_pairs=396,
            dynamic_mutation_pairs=checks['mutations']['cases'],
            actual_transition_cases=len(checks['transitions']['rows']),
            actual_transition_video_frames=sum(r['video_frames'] for r in checks['transitions']['rows']),
            checks_passed=list(checks),clean_repeat_roms_exact=True,
            main_ram_guards=True,callee_saved_registers_and_stack=True),
        g20_budget_complete=False,performance_build_created=False,
        next_task='G20B',artifact=artifact.as_posix(),artifact_copied=a.copy_rom,
        preserved_artifacts=artifacts,
        input_reports={f.as_posix():sha(f) for f in inputs},sources={f:sha(f) for f in sources},
        limitations=[
            'Native mGBA replacement BIOS; physical GBA/original BIOS not checked.',
            'Negative result applies to this bounded source-run implementation, not every possible sparse kernel.',
            'IWRAM requires moving edge_start to ROM; matched placement control costs measured separately.',
            'Spatial covers440 fixed poses per language, not every possible world state.',
            'Live timing advances simulation by elapsed emulated time; paired live dynamic states are not a pixel oracle.',
            'Dynamic exact image checks cover48 prescribed actor/map/door mutations, not full-game equivalence.',
            '282 transitions exclude known pre-existing Pause/Resume intermediate-frame issue;396 held details include six Mode4 cycles.',
            'Original EXEs not analyzed or changed; existing G03 package/RES evidence used.'])
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Heavy CPU ratios',[(r['level'],r['cell'],round(r['cpu_ratio'],4)) for r in heavy if r['language']==0])
    print('Spatial',[(r['language'],r['level'],round(r['cpu_ratio_median'],4),r['faster'],r['slower']) for r in spatial])
    print('Memory',memory[variants[-1]],'main stack',result['observed_main_stack'],result['observed_main_margin'])
    print('Sparse',sparse)
    print('Report',a.report)


if __name__=='__main__':main()
