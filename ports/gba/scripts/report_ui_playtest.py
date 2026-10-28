"""Bind UI01 second playtest evidence and publish the verified repeat ROM."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

from research_paths import resolve_research_path
import hashlib,json,shutil
from pathlib import Path
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
def main():
    root=Path('build/gba/g20b-playtest2');build=root/'hardware/hw_v128_entity_rect'
    manifest=read(root/'frontend-build.json');metadata=read(build/'build-report.json');digest=sha(build/'dok-diagnostic.gba')
    assert manifest['verified'] and digest==manifest['rom_sha256']==metadata['rom_sha256']
    assert digest==sha('build/gba/g20b-playtest2-repeat/hardware/hw_v128_entity_rect/dok-diagnostic.gba')
    for source,value in manifest['sources'].items():
        if source.startswith(('src/','gba/','include/','ports/gba/src/','ports/gba/platform/','ports/gba/include/')):assert sha(resolve_research_path(source))==value,source
    checks={};reports={}
    scripts=dict(frontend='verify_frontend_gba.py',ui='verify_ui_gba.py',flow='verify_flow_gba.py',audio='verify_audio_gba.py',
        time='verify_time_gba.py',arm='verify_performance_arm.py',water='verify_gba_hardware_water.py',sprites='verify_sprites_gba.py',
        transitions='verify_gba_hardware_transitions.py',latency='verify_frontend_latency.py',playtest='verify_ui_playtest.py')
    for name,script in scripts.items():
        path=root/(name+'.json');d=read(path);assert d['rom_sha256']==digest,name
        if name=='time':
            assert d['clock_runtime_passed'] and read(root/'time-selftest.json')['edge_vectors_on_arm']=='passed'
        else:assert d['status']=='passed',name
        if 'script_sha256' in d:assert d['script_sha256']==sha('ports/gba/scripts/'+script),name
        checks[path.as_posix()]=sha(path);reports[name]=d
    native=read(root/'door-original.json');assert native['status']=='passed' and len(native['original_constructor_cases'])==16
    assert native['script_sha256']==sha('ports/gba/scripts/verify_door_mirror_original.py')
    for m in native['evidence']:
        assert sha(m['sample'])==m['sha256_before']==m['sha256_after']
    checks[(root/'door-original.json').as_posix()]=sha(root/'door-original.json')
    control=read(root/'control396.json');assert len(control['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 for r in control['after']['rows'])
    control_manifest=read(root/'unreflected-control/prototype-build.json')
    assert control_manifest['variants']['hw_v128_entity_rect']['unreflected_control']
    comparison=read(root/'render396.json');assert comparison['after']['rom_sha256']==digest and len(comparison['after']['rows'])==396
    assert comparison['before']['rom_sha256']==control['before']['rom_sha256']
    checks[(root/'control396.json').as_posix()]=sha(root/'control396.json')
    checks[(root/'render396.json').as_posix()]=sha(root/'render396.json')
    transitions=reports['transitions']['rows'];assert len(transitions)==294 and all(not r['mixed_frames'] and not r['overflows'] for r in transitions)
    screens={**reports['frontend']['screens'],**reports['playtest']['screens']}
    for screen in screens.values():assert sha(screen['path'])==screen['sha256']
    music=reports['frontend']['music'];assert sha(music['wav'])==music['sha256']
    artifact=Path('artifacts/gba/DoK-port_UI01.gba');shutil.copyfile(build/'dok-diagnostic.gba',artifact)
    symbols=metadata['symbols'];ewram=metadata['ewram_static_bytes'];iwram=symbols['__iwram_end']-symbols['__iwram_start']
    assert ewram<262144 and iwram<24576
    result=dict(task='G20B/UI01/playtest2',date='2026-10-27',status='passed',artifact=artifact.as_posix(),rom_sha256=digest,
        rom_bytes=artifact.stat().st_size,repeat_byte_exact=True,build_dir=build.as_posix(),ram=dict(ewram=ewram,ewram_free=262144-ewram,
        iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram),stack=reports['audio']['stack'],irq_stack=reports['audio']['irq_stack'],
        native_reflection_cases=16,map_world_language_cases=len(reports['playtest']['map_cases']),
        renderer_unaffected_control_ppu_exact=396,renderer_reflection_changes=sum(bool(r['different_rgb_pixels']) for r in comparison['after']['rows']),
        hardware_transitions=294,video_callbacks=sum(r['video_frames'] for r in transitions),mixed_frames=0,
        intentional_black_frames=sum(len(r['black_transition_frames']) for r in transitions),music=music,screens=screens,input_reports=checks,
        sources={p.as_posix():sha(p) for folder in ('ports/gba/src','ports/gba/platform','ports/gba/include/dok') for p in Path(folder).rglob('*') if p.is_file()},
        limitations=['No physical GBA/original BIOS run','No complete DOS playthrough',
            'GBA map sight fan and full-screen layout are adaptations, not a pixel-exact native automap reconstruction',
            'Frame budget remains open'])
    Path('analysis/gba/g20b-ui01-playtest2.json').write_text(json.dumps(result,indent=2)+'\n')
    print(digest,artifact.stat().st_size,'bytes; EWRAM',ewram,'IWRAM',iwram)
if __name__=='__main__':main()
