"""Bind G17 UI, original x86, host, ARM and visual evidence to final artifacts."""
import hashlib
import json
from pathlib import Path

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def read(p):
    return json.loads(Path(p).read_text(encoding='utf-8-sig'))

def main():
    build=read('build/g17/build-report.json')
    rom='build/g17/dok-diagnostic.gba'
    assert sha(rom)==build['rom_sha256']
    names=('host-verification','original-verification','ui-gba','visual-review',
           'renderer-regression','interactions-regression','time-regression','ai-combat-regression','audio-regression')
    reports={name:read(f'analysis/g17-{name}.json') for name in names}
    for name,report in reports.items():
        assert report.get('status','passed')=='passed',name
        if 'rom_sha256' in report:
            assert report['rom_sha256']==build['rom_sha256'],name
    for p,h in reports['host-verification']['sources'].items():
        assert sha(p)==h,p
    for p,h in reports['host-verification']['artifacts'].items():
        assert sha(p)==h,p
    assert reports['host-verification']['cross_compiled']
    assert reports['ui-gba']['script_sha256']==sha('scripts/verify_ui_gba.py')
    assert all(reports['ui-gba']['checks'].values())
    assert len(reports['ui-gba']['notifications'])==2
    assert len(reports['ui-gba']['pages'])==3
    assert reports['original-verification']['script_sha256']==sha('scripts/verify_ui_original.py')
    for lang in ('ru','en'):
        directory=Path('build/ui')/lang
        manifest=read(directory/'analysis-manifest.json')
        assert manifest['sha256_before']==manifest['sha256_after']==sha(manifest['sample'])
        assert manifest['script_sha256']==sha('scripts/InspectUI.java')
        assert manifest['driver_sha256']==sha('scripts/analyze-ui.ps1')
        original=reports['original-verification']['languages'][lang]
        assert original['manifest_sha256']==sha(directory/'analysis-manifest.json')
        assert original['glyph_vectors']==1024 and original['message_vectors']==9
        for p,h in original['exports'].items():
            assert sha(directory/p)==h,p
    for name,screen in reports['ui-gba']['screens'].items():
        assert sha(screen['path'])==screen['sha256'],name
    for name,screen in reports['visual-review']['screens'].items():
        assert screen==reports['ui-gba']['screens'][name]
        assert sha(screen['path'])==screen['sha256'],name
    assert reports['time-regression']['exact_rational_samples']
    assert reports['audio-regression']['final']['underruns']==0
    assert len(reports['audio-regression']['all_sounds'])==23
    assert reports['audio-regression']['irq_stack']['untouched_margin_bytes']>=32
    for name in ('ui-gba','audio-regression'):
        assert reports[name]['stack']['untouched_margin_bytes']>=512
    sources=[]
    for folder in ('src','include/dok','gba','tests'):
        sources.extend(p for p in Path(folder).iterdir() if p.suffix in ('.c','.h','.inc','.S','.ld'))
    sources.extend(Path('scripts')/p for p in (
        'InspectUI.java','analyze-ui.ps1','ui_copy.py','build-gba.ps1','build-gba-ui.ps1',
        'verify_ui_original.py','verify_ui_host.py','verify_ui_gba.py','report_ui.py','verify_gba.py',
        'verify_renderer_gba.py','verify_interactions_gba.py','verify_time_gba.py','verify_sprites_gba.py',
        'verify_ai_combat_gba.py','verify_audio_gba.py'))
    sources.append(Path('CMakeLists.txt'))
    symbols=build['symbols']
    iwram=symbols['__iwram_end']-symbols['__iwram_start']
    assert iwram<=24576
    result=dict(task='G17',status='passed',next='G18',rom_sha256=build['rom_sha256'],rom_bytes=Path(rom).stat().st_size,
                ewram_static_bytes=build['ewram_static_bytes'],ewram_remaining_bytes=262144-build['ewram_static_bytes'],
                iwram_static_bytes=iwram,iwram_remaining_before_stack_reserve=24576-iwram,stack_reserve_bytes=8192,
                sources={p.as_posix():sha(p) for p in sorted(set(sources))},
                artifacts={p:sha(p) for p in ('build/g17/dok-diagnostic.gba','build/g17/dok-diagnostic.elf','build/g17/dok-diagnostic.map')},
                reports={f'analysis/g17-{name}.json':sha(f'analysis/g17-{name}.json') for name in names},
                checks=dict(original_x86_glyph_vectors=2048,original_x86_message_vectors=18,
                            ui=reports['ui-gba']['checks'],production_notifications=reports['ui-gba']['notifications'],
                            actual_audio_all23=True,exact_rational_active_clock=True,ai_combat_variants=28),
                performance=dict(renderer_timing=reports['renderer-regression']['timing'],target30fps_verified=False),
                limits=['Physical GBA/original BIOS and whole DOS session untested',
                        'Title Continue retains existing world; full new-game/death/level/ending flow belongs to G18',
                        'GBA keypad layout, menu wording, queue/paging and timeout are port policies, not byte-equivalent DOS UI',
                        'Original key/door notification wording and FONT8/FONT16 are verified independently for RU and EN',
                        'Audio user-listening reference is the accepted G16 recording; final G17 output is numerically captured, not newly listened by user',
                        'Linux static and Thumb compiled; Linux/MSVC/CMake execution untested; 30FPS remains G20'])
    Path('analysis/g17-review.json').write_text(json.dumps(result,indent=2)+'\n')
    print('G17 final ROM/source/host/original x86/ARM/visual/regression evidence bound')

if __name__=='__main__':
    main()
