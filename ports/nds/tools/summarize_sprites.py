"""Close N12 only after host/native and RU/EN/audit ARM9 reports exist."""
from __future__ import annotations
import json
from pathlib import Path
import shutil
from check_sprites import ROOT,OUT,EVIDENCE,artifact,digest
from run_renderer_emulator import read_fixture
from render_metrics import summarize
def load(path):return json.loads(Path(path).read_text('utf8'))
def main():
    host=load(EVIDENCE/'host-ru.json');native=load(EVIDENCE/'original-report.json');world=load(EVIDENCE/'world-original-report.json')
    assert host['passed'] and native['passed'] and world['passed']
    for row in host['sources']:
        if row['path'].endswith(('.c','.h')):assert digest(ROOT/row['path'])==row['sha256'],row['path']
    _,cases,_=read_fixture(OUT/'scene.dat');normal=[c for c in cases if c['source_kind'] not in (12,13)];ids={c['case_id'] for c in normal}
    variants={};archive=[];artifacts=[]
    for variant in ('ru','en','audit'):
        target=OUT/variant;r=load(target/'emulator/result.json');manifest=load(target/'build-manifest.json')
        assert r['passed'] and len(r['cases'])==26 and len(r['frame_texture_traces'])==462
        assert r['rom_sha256']==digest(target/'dok_nds_sprites.nds')
        assert r['elf_sha256']==digest(target/'dok_nds_sprites.elf')
        for row in manifest['sources']:
            if row['path'].endswith(('.c','.h')):assert digest(ROOT/row['path'])==row['sha256'],row['path']
        rows=[row for row in r['history_records'] if row['case_id'] in ids]
        metrics=summarize(rows,r['last_observation'],normal)
        if variant!='audit':assert metrics['budget_passed']
        assert r['cases'][-1]['masked_occlusion']['passed']
        variants[variant]=dict(rendered_frames=sum(c['frames'] for c in cases),frame_texture_traces=462,
            metrics={k:v for k,v in metrics.items() if k not in ('cases','cold_loading')},
            loading_boundaries=len(metrics['cold_loading']),memory=r['memory'],
            masked_occlusion=r['cases'][-1]['masked_occlusion'],
            framebuffer_coverage_cases=sum('depth_coverage' in c for c in r['cases']))
        archive_dir=EVIDENCE/variant;archive_dir.mkdir(exist_ok=True)
        inputs=[target/'build-manifest.json',target/'emulator/result.json',target/'runtime.log']
        if variant=='ru':inputs += list((target/'emulator').glob('*-top.png'))
        else:inputs += [target/'emulator/case-26-top.png']
        for source in inputs:
            dest=archive_dir/source.name;shutil.copyfile(source,dest);archive.append(artifact(dest))
        (archive_dir/'timestamp-metrics.json').write_text(json.dumps(metrics,indent=2)+'\n','utf8')
        archive.append(artifact(archive_dir/'timestamp-metrics.json'))
        artifacts += [artifact(target/f'dok_nds_sprites.{ext}') for ext in ('nds','elf','map')]
    (EVIDENCE/'archive-manifest.json').write_text(json.dumps(archive,indent=2)+'\n','utf8')
    result=dict(schema_version=1,task='N12',date='2026-10-29',status='ready-emulator',passed=True,next_task='N13',
        automatic_continuation_scope='User authorized N12 -> N13 -> N13P; stop after architecture decision',
        host=artifact(EVIDENCE/'host-ru.json'),native=artifact(EVIDENCE/'original-report.json'),
        world_native=artifact(EVIDENCE/'world-original-report.json'),archive=artifact(EVIDENCE/'archive-manifest.json'),
        native_projection_vectors=sum(r['billboard_vectors'] for r in native['languages'].values()),
        native_constructor_cases=sum(r['initial_appearance_constructor_cases'] for r in native['languages'].values()),
        native_world_poses=sum(r['poses'] for r in world['levels']),native_billboards=sum(r['native_billboards'] for r in world['levels']),
        full_host_scan_poses=sum(r['scan']['poses'] for r in host['levels']),unit_checks=host['unit'],
        maximum_sprites_by_level=[r['scan']['max_sprites'] for r in host['levels']],variants=variants,artifacts=artifacts,
        metric_script=artifact(ROOT/'ports/nds/tools/render_metrics.py'),
        limitations=['Physical DS/DS Lite and hardware FPS remain N24','Fixture object states; live AI/combat remain N16/N17',
            'Every-frame asset walk is a separate stress test, not native animation cadence',
            'LIGHT is identity until N13; combined actor/effect budget and architecture decision remain N13P'])
    (ROOT/'analysis/nds-n12-sprites.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print('N12 ready (emulator): RU, EN, audit; exact source-frame traces and masked depth checks passed')
if __name__=='__main__':main()
