"""N20 acceptance summary: source, host, real pointer and actual LCD gates."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3];E=ROOT/'research/evidence/nds/n20'
def read(p):return json.loads(p.read_text('utf8'))
def artifact(p):
    b=p.read_bytes();return dict(path=p.relative_to(ROOT).as_posix(),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
    paths={
        'source':E/'source-discovery.json','raster':E/'raster-check.json',
        'postvisited':E/'postvisited-check.json','controller':E/'host-control.json',
        'frozen_sources':E/'frozen-source-check.json','host_test_extension':E/'host-test-extension.json',
        'combat_touch':ROOT/'build/nds/n20/combat-touch-ru/map/combat-result.json',
        'title_stream':ROOT/'build/nds/n20/title-smoke-ru/audio/title-gate.json',
        'visual':E/'visual-review.json'}
    gates={k:read(p) for k,p in paths.items()}
    assert all(r.get('passed') is True for r in gates.values()),'N20 independent gate open'
    assert gates['controller']['checks']==174391
    assert gates['raster']['checks']==954 and gates['postvisited']['checks']==12
    title=gates['title_stream']['waveform']
    assert title['complete_source_covered'] and title['source_samples']==474361 and title['whole_coefficient']>=.95
    assert gates['combat_touch']['combat_proven'] and gates['combat_touch']['observed_stages']==94
    supplemental=ROOT/'build/nds/n20/combat-touch-ru'
    supplemental_input=read(supplemental/'map/result.json')
    supplemental_audio=read(supplemental/'audio/result.json')
    assert supplemental_input['input_checks_passed'] and supplemental_input['script_completed']
    assert supplemental_audio['passed'] and supplemental_audio['fat_unchanged_after_unload']
    assert supplemental_audio['catalog_completed'] and len(supplemental_audio['catalog'])==24
    runtime=[]
    for lang in ('ru','en'):
        target=ROOT/f'build/nds/n20/final-release-{lang}'
        reports={k:read(target/k/'result.json') for k in ('map','hud','audio','emulator')}
        assert all(r['passed'] is True for r in reports.values())
        r=reports['emulator'];m=reports['map'];h=reports['hud'];a=reports['audio']
        assert len(r['cases'])==63 and r['last_observation']['total_frames']==12880
        assert m['script_completed'] and m['input_checks_passed'] and len(m['stages'])==94
        assert m['index8_checks']>0 and m['rgb_checks']>0 and m['last_probe'][31]==0
        assert m['last_probe'][46]>=1 and m['last_probe'][47]>=1
        assert not any(h['missing_stages'].values())
        assert h['pixel_checks']>0 and h['hardware_pixel_checks']>0
        assert a['catalog_completed'] and len(a['catalog'])==24 and a['fat_unchanged_after_unload']
        title_path=target/'audio/title-gate.json';title_gate=read(title_path)
        wave=title_gate['waveform']
        assert title_gate['passed'] and wave['complete_source_covered'] and wave['source_samples']==474361
        assert wave['whole_coefficient']>=.95
        assert r['fat_unchanged'] and r['memory']['reserve_bytes']==655360
        assert r['memory']['sdk_heap_available']>=196608
        rom=artifact(target/'dok_nds_gameplay.nds');elf=artifact(target/'dok_nds_gameplay.elf')
        assert all(x['rom_sha256']==rom['sha256'] for x in reports.values())
        assert title_gate['rom_sha256']==rom['sha256']
        assert m['elf_sha256']==elf['sha256']==a['elf_sha256']==r['elf_sha256']
        runtime.append(dict(language=lang,rom=rom,elf=elf,
            reports={k:artifact(target/k/'result.json') for k in reports},
            cases=63,presentations=12880,input_stages=len(m['stages']),map_states=len(m['rows']),
            map_index8_checks=m['index8_checks'],map_rgb_checks=m['rgb_checks'],
            hud_index8_checks=h['pixel_checks'],hud_rgb_checks=h['hardware_pixel_checks'],audio_captures=len(a['catalog']),
            live_title_gate=artifact(title_path),live_title_coefficient=wave['whole_coefficient'],
            memory=r['memory'],
            history={k:v for k,v in r['history'].items() if k not in ('cases','cold_loading')},
            live_routes={k:v for k,v in r['live_routes'].items() if k not in ('cases','cold_loading')}))
    assert gates['combat_touch']['rom_sha256']==runtime[0]['rom']['sha256']
    assert gates['title_stream']['rom_sha256']==runtime[0]['rom']['sha256']
    archive=read(E/'final-archive.json');assert archive['passed']
    for pair in archive['reports']+archive['generated']:
        assert artifact(ROOT/pair['source']['path'])==pair['source']
        assert artifact(ROOT/pair['archive']['path'])==pair['archive']
        assert pair['source']['sha256']==pair['archive']['sha256']
    report=dict(task='N20',status='ready-emulator',passed=True,software_checks_passed=True,
        gates={k:artifact(p) for k,p in paths.items()},runtime=runtime,
        source_summary=artifact(ROOT/'analysis/nds-n20-discovery-source.json'),
        fixture=artifact(ROOT/'build/nds/n20/scene.dat'),
        archive=artifact(E/'final-archive.json'),supersession=artifact(E/'supersession.json'),
        audio_service_fix=artifact(E/'audio-service-supersession.json'),
        concurrent_combat=dict(report=artifact(paths['combat_touch']),input_report=artifact(supplemental/'map/result.json'),
            audio_report=artifact(supplemental/'audio/result.json'),fixture=artifact(ROOT/'build/nds/n20/scene-combat-touch.dat'),
            scope='Separate source_kind24 case from accepted combat49: real pickups, live firing during touch/drag and after resume; full map pixels remain main RU/EN gates'),
        joint_performance_budget_passed=all(r['history']['budget_passed'] for r in runtime),
        adaptations=['Permanent bottom256x160 map plus32px buttons; five scales and independent touch gestures',
            'Center anchor survives zoom with follow offset; Center explicitly resets this offset',
            'Original map paints hero once after traversal, excluding undefined stale-stack arrow residue',
            'Explicit bugfix: global projectile low-WORD world-coordinate clamp cannot reveal cell4095 without world-cell provenance'],
        physical_hardware=False,next_task='N21',limits=[
            'Original constructor/record/column and raster isolated execution is verified; a complete live DOS session is not claimed',
            'Physical DS/DSLite, actual LCD readability/FPS/DMA/touch/IRQ/stacks remain N24',
            'Joint full renderer/audio/HUD/map/menu performance budget remains N21P',
            'No gameplay saves; runtime FAT unchanged, settings-only writes belong to N21'])
    (ROOT/'analysis/nds-n20-map.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,runtime=[{k:v for k,v in r.items() if k in ('language','map_states','input_stages','map_index8_checks','map_rgb_checks','memory')} for r in runtime])))
if __name__=='__main__':main()
