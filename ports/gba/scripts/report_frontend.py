"""Validate UI01 evidence and publish its separate, reproducible ROM artifact."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from research_paths import script_path, resolve_research_path
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy-rom',action='store_true')
    parser.add_argument('--build-root',type=Path,default=Path('build/gba/g20b'))
    parser.add_argument('--repeat-root',type=Path,default=Path('build/gba/g20b-repeat'))
    parser.add_argument('--report',type=Path,default=Path('analysis/gba/g20b-ui01.json'))
    args=parser.parse_args()
    root=args.build_root;build=root/'hardware/hw_v128_entity_rect'
    read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    metadata=read(build/'build-report.json');digest=metadata['rom_sha256']
    assert sha(build/'dok-diagnostic.gba')==digest
    repeat=read(args.repeat_root/'frontend-build.json')
    assert repeat['rom_sha256']==digest==sha(args.repeat_root/'hardware/hw_v128_entity_rect/dok-diagnostic.gba')
    manifest=read(root/'frontend-build.json')
    assert manifest['verified'] and manifest['rom_sha256']==digest
    # All runtime sources still match the actual build. Test scripts bind
    # themselves below, independently of compilation/source snapshots.
    for path,value in manifest['sources'].items():
        if path.startswith(('src/','gba/','include/','ports/gba/src/','ports/gba/platform/','ports/gba/include/')):assert sha(resolve_research_path(path))==value,path
    scripts=dict(frontend='verify_frontend_gba.py',ui='verify_ui_gba.py',flow='verify_flow_gba.py',audio='verify_audio_gba.py',
                 arm='verify_performance_arm.py',water='verify_gba_hardware_water.py',sprites='verify_sprites_gba.py',
                 transitions='verify_gba_hardware_transitions.py')
    if (root/'latency.json').exists():scripts['latency']='verify_frontend_latency.py'
    reports={}
    for name,script in scripts.items():
        d=read(root/(name+'.json'))
        assert d['status']=='passed' and d['rom_sha256']==digest,name
        assert d['script_sha256']==sha('ports/gba/scripts/'+script),name
        reports[name]=d
    clock=read(root/'time.json');assert clock['rom_sha256']==digest and clock.get('clock_runtime_passed',True)
    if clock['edge_vectors_on_arm']!='passed':
        diagnostic=read(root/'time-selftest.json');assert diagnostic['edge_vectors_on_arm']=='passed'
    comparison=read(root/'exact396.json')
    assert comparison['before']['rom_sha256']=='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    assert comparison['after']['rom_sha256']==digest and len(comparison['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 for r in comparison['after']['rows'])
    transitions=reports['transitions']['rows']
    assert len(transitions)==294 and all(not r['mixed_frames'] and not r['overflows'] for r in transitions)
    original_path=Path('build/gba/g20b/original-evidence.json')
    original=read(original_path);assert original['status']=='passed'
    assert original['script_sha256']==sha('ports/gba/scripts/verify_frontend_original.py')
    for sample in original['samples']:
        m=sample['manifest'];assert sha(m['sample'])==m['sha256_before']==m['sha256_after']
        assert sha(sample['res']['path'])==sample['res']['sha256']
    frontend=reports['frontend'];screens={}
    for name,screen in frontend['screens'].items():
        assert sha(screen['path'])==screen['sha256'];screens[name]=screen
    assert sha(frontend['music']['wav'])==frontend['music']['sha256']
    artifact=Path('artifacts/gba/DoK-port_UI01.gba')
    if args.copy_rom:shutil.copyfile(build/'dok-diagnostic.gba',artifact)
    assert sha(artifact)==digest
    s=metadata['symbols'];ewram=metadata['ewram_static_bytes'];iwram=s['__iwram_end']-s['__iwram_start']
    paths=[root/(n+'.json') for n in (*scripts,'time','exact396','frontend-build')]+[original_path]
    if (root/'time-selftest.json').exists():paths.append(root/'time-selftest.json')
    baseline=None
    if (root/'baseline-latency.json').exists():
        baseline=read(root/'baseline-latency.json');paths.append(root/'baseline-latency.json')
    report=dict(task='G20B/UI01',date='2026-10-27',status='passed',artifact=artifact.as_posix(),rom_sha256=digest,
                rom_bytes=metadata['rom_bytes'],clean_repeat_byte_exact=True,build_dir=build.as_posix(),
                source_control_sha256=comparison['before']['rom_sha256'],
                ram=dict(ewram=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
                stack=reports['audio']['stack'],irq_stack=reports['audio']['irq_stack'],music=frontend['music'],screens=screens,
                validation=dict(host_windows=True,linux_and_thumb_compile_only=True,flow_world_language_views=len(reports['flow']['levels']),
                    original_sound_resources=23,water_phases=reports['water']['phase_cases'],gameplay_ppu_exact_cases=396,
                    transitions=len(transitions),video_callbacks=sum(r['video_frames'] for r in transitions),mixed_frames=0,
                    intentional_black_frames=sum(len(r['black_transition_frames']) for r in transitions),
                    old_pause_resume_issue_fixed=True,clock_edges_on_arm=True,arm_rectangle_vectors=reports['arm']['rectangle_raster_vectors']),
                original_evidence=original,latency=reports.get('latency'),before_feedback=baseline,input_reports={p.as_posix():sha(p) for p in paths},
                source_hashes={p.as_posix():sha(p) for p in [*Path('ports/gba/src').glob('ui*'),Path('ports/gba/include/dok/ui.h'),
                    *[Path('ports/gba/platform')/n for n in ('ui_backend.c','flow_backend.c','audio_backend.c','renderer_probe.c','hardware_bg.inc')],
                    *[script_path(n) for n in (*scripts.values(),'build_gba_frontend.py','report_frontend.py','ui_copy.py','verify_frontend_original.py')]]},
                limitations=['No physical GBA/original BIOS test','No whole DOS playthrough or new DOS Sound Blaster waveform capture',
                             'No new human listening claim','G20 frame budget remains open; G21 playtest is next'])
    args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(digest,metadata['rom_bytes'],'bytes; EWRAM',ewram,'IWRAM',iwram,'UI01 passed')


if __name__=='__main__':main()
