"""Close N13P with pinned exact reference and explicit artistic GPU adaptation."""
from pathlib import Path
import json,shutil
from check_effects import ROOT,artifact,digest
from check_effects_performance import prepare,OUT,EVIDENCE,load
from run_renderer_emulator import read_fixture
def main():
    prepare();variants={};artifacts=[]
    _,cases,_=read_fixture(OUT/'scene.dat')
    normal_ids={c['case_id'] for c in cases if c['source_kind'] not in (12,13,14,17,18)}
    for label in ('baseline-ru','baseline-en','optimized-ru','optimized-en','optimized-audit','fog-ru','fog-en','fog-audit'):
        target=OUT/label;report=target/'emulator/result.json';j=load(report)
        assert j['passed'] and len(j['cases'])==41 and len(j['frame_texture_traces'])==462 and j['fat_unchanged']
        assert j['rom_sha256']==digest(target/'dok_nds_effects.nds') and j['elf_sha256']==digest(target/'dok_nds_effects.elf')
        assert len([r for r in j['history_records'] if r['flags']&8])==2740
        assert sum(c.get('whole_top_fade_black',False) for c in j['cases'])==3
        if label.startswith(('optimized','fog')) and not label.endswith('audit'):
            base=load(OUT/('baseline-'+label.rsplit('-',1)[1])/'emulator/result.json')
            assert j['effect_traces']==base['effect_traces'] and j['frame_texture_traces']==base['frame_texture_traces']
            assert [c['sprites'] for c in j['cases']]==[c['sprites'] for c in base['cases']]
            if label.startswith('optimized'):
                assert all(a['images'][1]['sha256']==b['images'][1]['sha256'] for a,b in zip(j['cases'],base['cases']))
        if label.endswith('audit'):
            normal=load(OUT/(label.removesuffix('audit')+'ru')/'emulator/result.json')
            assert j['effect_traces']==normal['effect_traces'] and j['frame_texture_traces']==normal['frame_texture_traces']
            assert all(a['images'][1]['sha256']==b['images'][1]['sha256'] for a,b in zip(j['cases'],normal['cases']))
            assert load(target/'build-manifest.json')['audit']
        rows=[r for r in j['history_records'] if r['flags']&8]
        active=[r for r in rows if r['case_id'] in normal_ids and not r['flags']&1]
        max_work=max(sum(r[k] for k in ('prepare_ticks','submit_ticks','io_upload_ticks')) for r in active)*1000/j['last_observation']['timer_hz']
        m={k:v for k,v in j['history'].items() if k not in ('cases','cold_loading')}
        variants[label]=dict(metrics=m,cases=41,presentations=2740,source_frame_hashes=462,source_callbacks=len(j['effect_traces']),
            memory=j['memory'],max_quads=max(r['input_quads'] for r in rows),
            max_polygons=max(r['hardware_polygons'] for r in rows),max_vertices=max(r['hardware_vertices'] for r in rows),
            max_texture_bytes=max(r['texture_bytes'] for r in rows),max_palette_bytes=max(r['palette_bytes'] for r in rows),
            max_variant_bytes=max(c['effects'][13] for c in j['cases']),
            max_cpu_source_bytes=max(c['sprite']['cache_bytes'] for c in j['cases']),
            cold_loads=len(j['history']['cold_loading']),report=artifact(report),rom_sha256=j['rom_sha256'],
            max_measured_work_ms=max_work,nominal_two_vblank_component_margin_ms=2000/j['history']['refresh_hz']-max_work,
            work_margin_scope='Sum of prepare/submit/upload; excludes probe/history publishing and does not guarantee future combat budget',
            effect_metadata_samples=len(j['effect_frames']))
        dest=EVIDENCE/'final'/label;dest.mkdir(parents=True,exist_ok=True)
        for p in (report,target/'build-manifest.json',target/'dok_nds_effects.nds',target/'dok_nds_effects.elf'):
            q=dest/p.name;shutil.copyfile(p,q);artifacts.append(artifact(q))
        if label=='fog-ru':
            for p in sorted((target/'emulator').glob('*-top.png')):
                q=dest/p.name;shutil.copyfile(p,q);artifacts.append(artifact(q))
    quality=load(OUT/'quality/emulator/result.json');assert quality['passed']
    for language in ('ru','en'):assert variants['fog-'+language]['metrics']['budget_passed']
    host=load(EVIDENCE/'host-check.json');assert host['passed']
    result=dict(schema_version=1,task='N13P',date='2026-10-29',status='ready-emulator',passed=True,next_task='N14',
        decision='Keep hardware3D scene/GE/depth architecture; replace production LIGHT tessellation with black per-pixel GPU fog',
        user_authorization='Approximate lighting explicitly authorized; artistic aim: distant objects vanish into frightening darkness',
        fidelity_changes=['Smooth black depth fog instead of16 source color-index remaps',
            'Source index0 silhouettes retained; extra nonzero-to0 LIGHT holes are replaced by RGB darkening',
            'No geometry splits/welds or prepared-light geometry cache on the GPU path',
            'Exact N13 LIGHT remains a reference and controlled mask/depth fixtures remain exact'],
        variants=variants,depth_quality=quality['samples'],quality=artifact(OUT/'quality/emulator/result.json'),host=artifact(EVIDENCE/'host-check.json'),
        input_proof=artifact(EVIDENCE/'baseline-inputs.json'),additional_source_archives=artifact(EVIDENCE/'source-supersession-extra.json'),
        fixture=load(EVIDENCE/'fixture.json'),artifacts=artifacts,
        limitations=['DS-mode emulator only; physical DS/DS Lite remain N24',
            'Deterministic166tick source fixture per sweep; no claim of full gameplay or real hardware FPS',
            'Audit hashing overhead reported separately; no audio, live combat or full second-screen map yet',
            '30 cadence accepted for normal accumulated fixtures;60 not accepted',
            'Fast frames can outrun the single effect-metadata probe; presentation history, callback trace, case packets and462 source-frame hashes are complete',
            'N14 is not started automatically; N17P/N21P retain architectural decision points'])
    (ROOT/'analysis/nds-n13p-performance.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print('N13P ready-emulator; GPU black fog accepted, exact LIGHT geometry retired from production path')
if __name__=='__main__':main()
