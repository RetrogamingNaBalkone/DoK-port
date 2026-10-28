"""Bind G20 measurements, regressions, sources and finite GBA memory budgets.

A passed regression is never interpreted as achievement of the 30 FPS target.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from research_paths import script_path
import argparse
import json
from pathlib import Path
import statistics
from gba_rom import elf_info
from verify_gba import sha


def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=Path('build/gba/g20'))
    p.add_argument('--report',type=Path,default=Path('analysis/gba/gba-final-budget.json'))
    a=p.parse_args()
    build=a.build_dir
    metadata=read(build/'build-report.json')
    rom=sha(build/'dok-diagnostic.gba')
    assert rom==metadata['rom_sha256']
    reports={name:Path(f'analysis/gba/g20-{name}-regression.json') for name in
             ('flow','ui','renderer','sprites','interactions','time','ai-combat','audio')}
    reports.update(cameras=Path('analysis/gba/g20-cameras.json'),game=Path('analysis/gba/g20-game-profile.json'),
                   game_baseline=Path('analysis/gba/g20-game-baseline.json'),arm=Path('analysis/gba/g20-arm-vectors.json'))
    evidence={name:read(path) for name,path in reports.items()}
    for name,value in evidence.items():
        status=value.get('status',value.get('edge_vectors_on_arm'))
        assert status=='passed',(name,status)
        digest=value['after']['rom_sha256'] if name=='cameras' else value['rom_sha256']
        expected=evidence['cameras']['before']['rom_sha256'] if name=='game_baseline' else rom
        assert digest==expected,(name,'stale ROM')
    assert evidence['game']['audio_underruns']==0
    assert evidence['cameras']['after']['surface_cache_epoch_rollover_exercised']
    _,symbols,sections=elf_info(build/'dok-diagnostic.elf')
    iwram=next(s['size'] for s in sections if s['name']=='.iwram')
    ewram=metadata['ewram_static_bytes']
    reserve=symbols['__iwram_reserved_bytes']
    assert iwram<=32768-reserve and ewram<=262144 and metadata['rom_bytes']<=33554432
    before_originals=read(build/'originals-before.json')
    after_originals={p.as_posix():sha(p) for p in sorted(Path('originals').rglob('*')) if p.is_file()}
    assert before_originals==after_originals,'Originals changed during G20 validation'
    cleanup=read('analysis/artifact-cleanup-20261026.json')
    historical={r['path']:r['sha256'] for r in cleanup['preserved_sha256'] if r['path'].startswith('originals/')}
    assert historical==after_originals,'Originals differ from the preserved pre-G20 inventory'
    camera_rows=evidence['cameras']['after']['rows']
    baseline=evidence['cameras']['before']['rows']
    timing_rows=[]
    for level in range(3):
        for pose in ('best','median','worst'):
            rows=sorted((r for r in camera_rows if r['level']==level and r['language']==0 and '-start-' in r['name']),key=lambda r:r['median'])
            row=rows[0 if pose=='best' else len(rows)//2 if pose=='median' else -1]
            old=next(r for r in baseline if r['name']==row['name'] and r['language']==row['language'])
            timing_rows.append(dict(level=level+1,category=pose,scene=row['name'],camera=row['camera'],
                                    minimum=row['minimum'],median=row['median'],maximum=row['maximum'],
                                    before_median=old['median'],speedup=old['median']/row['median'],
                                    budget_multiple=row['median']/561792,
                                    stages=row['stage_medians'],synthetic_boundary=row['name'].endswith(('-low','-high'))))
    game=[]
    for row in evidence['game']['rows']:
        if row['language']:continue
        game.append({k:row[k] for k in ('level','scene','minimum','median','maximum','presented_fps',
                                       'max_projectiles','max_ai_owners','motion_steps','game_ticks','sound_active_frames')})
        old=next(r for r in evidence['game_baseline']['rows'] if r['level']==row['level'] and r['scene']==row['scene'] and r['language']==0)
        game[-1].update(before_median=old['median'],before_presented_fps=old['presented_fps'],
                        observed_median_speedup=old['median']/row['median'],
                        identical_dynamic_state_comparison=row['simulation_frozen'])
        for key in ('keeps_up_with_real_time','final_motion_debt','max_motion_debt','time_conservation_error_ticks',
                    'artificial_stress','artificial_all_enemies_awake'):
            if key in row:game[-1][key]=row[key]
        if not row['simulation_frozen']:
            assert row['keeps_up_with_real_time']==(max(s['motion_debt'] for s in row['samples'])<416)
    source_paths=sorted({p for root in ('ports/gba/src','ports/gba/include','ports/gba/platform') for p in Path(root).rglob('*')
                         if p.is_file() and p.suffix in ('.c','.h','.inc','.S','.ld')})
    source_paths+=sorted(Path('ports/gba/scripts').glob('*performance*'))
    source_paths += [Path('ports/gba/scripts/profile_game_gba.py'),Path('ports/gba/scripts/profile_renderer.py'),
                     Path('ports/gba/scripts/pack_animation_index.py'),Path('ports/gba/scripts/render_tables.py'),
                     Path('ports/gba/scripts/build-gba.ps1'),Path('ports/gba/scripts/build-native.ps1'),Path('CMakeLists.txt')]
    regression_scripts=[script_path(name) for name in ('verify_flow_gba.py','verify_ui_gba.py','verify_renderer_gba.py',
        'verify_sprites_gba.py','verify_interactions_gba.py','verify_time_gba.py','verify_ai_combat_gba.py','verify_audio_gba.py')]
    source_paths+=regression_scripts
    host_logs=[build/'host-regression.log',build/'host-ui-flow.log']
    for path in host_logs:assert path.is_file() and path.stat().st_size>0,path
    result=dict(task='G20',status='performance_target_not_met',optimization_pass_validated=True,
                target_fps=30,target_cycles=561792,target_met=False,next='G20A',
                graphics_quality_reduced=False,scene_columns=120,scene_display=[240,144],ui_display=[240,160],
                rom_sha256=rom,rom_bytes=metadata['rom_bytes'],
                memory=dict(ewram_static_bytes=ewram,ewram_remaining_bytes=262144-ewram,
                            iwram_static_bytes=iwram,iwram_remaining_before_stack=32768-reserve-iwram,
                            stack_bios_reserve_bytes=reserve,rom_remaining_bytes=33554432-metadata['rom_bytes'],
                            vram_bytes=98304,vram_page_payload_bytes=76800,vram_high_water_offset=79360,
                            vram_unused_bytes=21504,bg_palette_bytes=512,object_vram_allocated=0,heap_bytes=0),
                camera_scenes=timing_rows,game_scenes=game,
                paired_camera_frames=evidence['cameras']['pixel_exact_frames'],
                paired_median_speedup=statistics.median(r['median'] for r in baseline)/statistics.median(r['median'] for r in camera_rows),
                main_stack=evidence['game']['stack'],irq_stack=evidence['game']['irq_stack'],
                audio_underruns=0,originals_sha256_before=before_originals,originals_sha256_after=after_originals,
                originals_match_pre_g20_cleanup_inventory=True,
                sources={p.as_posix():sha(p) for p in sorted(set(source_paths))},
                evidence={name:dict(path=path.as_posix(),sha256=sha(path)) for name,path in reports.items()},
                artifacts={p.as_posix():sha(p) for p in [build/'dok-diagnostic.elf',build/'dok-diagnostic.gba',
                    build/'dok-diagnostic.map',build/'animation-index.json',build/'render-columns.json',
                    build/'build-report.json',Path('build/gba/assets/assets.bin'),*host_logs]},
                limits=['30 FPS unmet; G20 remains open pending budget work/G20A.',
                        'Camera sweep freezes simulation and uses an out-of-timer capture hold; its intervals are not normal FPS.',
                        'Game profile disables capture hold and distinguishes natural, capacity and all-awake scenes.',
                        'All-awake stress accumulates simulation debt; it is retained, not dropped.',
                        'No physical GBA, original BIOS, whole DOS session or Linux/MSVC execution verified.',
                        'Original EXE analysis was not performed in G20; existing original evidence remains separate.',
                        'This pass does not prove that every conceivable renderer/engine rewrite has been exhausted.'])
    a.report.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(f'G20 regressions validated; target NOT met; ROM {rom}; next G20A')


if __name__=='__main__':main()
