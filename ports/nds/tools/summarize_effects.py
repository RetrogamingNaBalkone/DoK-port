"""Close functional N13 with explicit full-run and final HUD-correction scopes."""
from pathlib import Path
import json,shutil
from check_effects import ROOT,OUT,EVIDENCE,artifact,digest
from render_metrics import summarize
from run_renderer_emulator import read_fixture
def load(path):return json.loads(Path(path).read_text('utf8'))
def main():
    host=load(EVIDENCE/'host-check.json');assert host['passed']
    for name in ('original-report.json','actual-assets-report.json','light-geometry-report.json','phase-pair-report.json','render-list-evidence.json'):
        assert load(EVIDENCE/name)['passed'],name
    _,cases,_=read_fixture(OUT/'scene.dat');assert len(cases)==37 and sum(c['frames'] for c in cases)==2350
    _,focused,_=read_fixture(OUT/'focused.dat');assert len(focused)==12 and sum(c['frames'] for c in focused)==592
    normal=[c for c in cases if c['source_kind'] not in (12,13,14,17,18)];ids={c['case_id'] for c in normal}
    variants={};archive=[];artifacts=[]
    for variant in ('ru','en','audit'):
        full=load(EVIDENCE/'full-before-hud'/variant/'result.json');patch=load(OUT/f'hud-{variant}/emulator/result.json')
        assert full['passed'] and len(full['cases'])==37 and len(full['frame_texture_traces'])==462
        assert len(full['effect_frames'])==2350 and full['fat_unchanged'] and patch['fat_unchanged']
        assert patch['passed'] and len(patch['cases'])==12 and len(patch['effect_frames'])==592
        assert sum(c.get('whole_top_fade_black',False) for c in patch['cases'])==3
        assert patch['rom_sha256']==digest(OUT/variant/'dok_nds_effects.nds')
        runtime_elf=EVIDENCE/variant/'runtime-symbols.elf'
        assert digest(runtime_elf)==full['elf_sha256']
        for case in cases:
            if case['source_kind']==18:
                values=[f[8] for f in full['effect_frames'] if f[27]==case['case_id']]
                assert values==list(range(0,100,3))+list(range(100,0,-3))
        metrics=summarize([r for r in full['history_records'] if r['case_id'] in ids],full['last_observation'],normal,quad_strips=True)
        active=[r for r in full['history_records'] if r['flags']&8]
        variants[variant]=dict(full_presentations=2350,all_frame_hashes=462,source_callbacks=len(full['effect_traces']),
            full_scope='All37 cases before final HUD-palette commit correction; exact runtime ELF retained',
            final_hud_scope='12 cases /592 frames on final ROM: doors, water, both masked scenes and all3 level fades; top scene and HUD black at1%',
            final_hud_presentations=592,whole_top_fade_checks=3,metrics_before_hud_correction={k:v for k,v in metrics.items() if k not in ('cases','cold_loading')},
            memory=patch['memory'],max_input_quads=max(r['input_quads'] for r in active),
            max_hardware_vertices=max(r['hardware_vertices'] for r in active),max_hardware_polygons=max(r['hardware_polygons'] for r in active),
            max_texture_bytes=max(r['texture_bytes'] for r in active),max_palette_bytes=max(r['palette_bytes'] for r in active),
            max_variant_cache=max(f[13] for f in full['effect_frames']),
            max_cpu_source_cache=max(c['sprite']['cache_bytes'] for c in full['cases']),
            rom_sha256=patch['rom_sha256'],full_report=artifact(EVIDENCE/'full-before-hud'/variant/'result.json'))
        dest=EVIDENCE/'final'/variant;dest.mkdir(parents=True,exist_ok=True)
        for source in (OUT/f'hud-{variant}/emulator/result.json',OUT/variant/'build-manifest.json',OUT/variant/'dok_nds_effects.nds',OUT/variant/'dok_nds_effects.elf'):
            target=dest/source.name;shutil.copyfile(source,target);archive.append(artifact(target))
        if variant=='ru':
            for source in sorted((OUT/f'hud-{variant}/emulator').glob('*-top.png')):
                target=dest/source.name;shutil.copyfile(source,target);archive.append(artifact(target))
        artifacts.extend(artifact(OUT/variant/f'dok_nds_effects.{ext}') for ext in ('nds','elf','map'))
    real=load(OUT/'real/emulator/result.json');assert real['passed'] and len(real['effect_frames'])==48
    assert all(f[22]==1 for f in real['effect_frames'])
    pair=load(OUT/'phase-pair/emulator/result.json');assert pair['passed']
    regression=load(OUT/'regression-n12/emulator/result.json');assert regression['passed'] and len(regression['cases'])==26
    (EVIDENCE/'final-archive.json').write_text(json.dumps(archive,indent=2)+'\n','utf8')
    result=dict(schema_version=1,task='N13',date='2026-10-29',status='ready-emulator',passed=True,next_task='N13P',
        host=artifact(EVIDENCE/'host-check.json'),native=artifact(EVIDENCE/'original-report.json'),
        actual_assets=artifact(EVIDENCE/'actual-assets-report.json'),light_geometry=artifact(EVIDENCE/'light-geometry-report.json'),
        phase_pair=artifact(EVIDENCE/'phase-pair-report.json'),registry=artifact(EVIDENCE/'render-list-evidence.json'),
        archive=artifact(EVIDENCE/'final-archive.json'),variants=variants,artifacts=artifacts,
        native_light_vectors_per_language=2865,native_crop_vectors_per_language=150,actual_resource_phase_pairs=540,
        native_band_interiors=load(EVIDENCE/'light-geometry-report.json')['checked_band_interiors'],
        full_host_scan_poses=sum(v['poses'] for v in host['levels']),host_units=host['units'],
        n12_regression=dict(cases=26,presentations=1790,passed=True),real_clock_presentations=48,paired_dos_levels=3,
        limitations=['Physical DS/DS Lite remain N24; emulator FPS is virtual',
          'Functional N13 acceptance is separate from the failed performance target; N13P must assess/optimize the combined renderer',
          'Default deterministic fixture clock166ticks/sweep; real N08 clock tested separately',
          'Actors/doors include scripted snapshots; full AI, combat, second-screen map and audio are future stages',
          'No separate reflected-world pass found in reviewed normal PLAY path; mirror UV and water are separately preserved',
          'Final whole-top HUD palette correction has a scoped592-frame regression per language/audit; prior2350-frame runs are identified separately, not relabelled as final ROM runs'])
    (ROOT/'analysis/nds-n13-effects.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print('N13 functional ready (emulator); performance decision is N13P')
if __name__=='__main__':main()
