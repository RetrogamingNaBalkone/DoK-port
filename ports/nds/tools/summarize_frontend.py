"""Fail-closed N21 compact acceptance from independent source/runtime gates."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/nds/n21'

def read(path):return json.loads(path.read_text('utf8'))
def artifact(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def main():
    acceptance_path=E/'root-acceptance.json'
    acceptance=read(acceptance_path);assert acceptance['passed'] is True
    gate_names=('assets.json','original-selector.json','visual-source.json',
                'host-state-settings.json','host-visual.json','visual-review.json')
    gates={name:read(E/name) for name in gate_names}
    assert all(r['passed'] is True for r in gates.values())
    assert gates['host-state-settings.json']['checks']==142931
    assert gates['host-visual.json']['checks']==1049583
    assert gates['host-visual.json']['wrong_language_mutations_rejected']==2
    assert gates['visual-source.json']['game_art_pairs']==111
    assert gates['visual-source.json']['identical_game_art_pairs']==110
    native=gates['original-selector.json']
    assert native['count']==14 and len(native['titles'])==2 and len(native['readmes'])==2
    assert native['executed_source_pins_unchanged'] and len(native['rejected_source_mutations'])==6
    builds={}
    for relative in acceptance['targets']:
        path=ROOT/relative;manifest=read(path/'build-manifest.json')
        for pin in manifest['sources']+manifest['build_drivers']+manifest['generated_inputs']:
            assert artifact(ROOT/pin['path'])==pin
        assert artifact(ROOT/manifest['arm7_dependency']['path'])==manifest['arm7_dependency']
        builds[path.name]=dict(path=relative,rom=artifact(path/'dok_nds_gameplay.nds'),
            elf=artifact(path/'dok_nds_gameplay.elf'),manifest=artifact(path/'build-manifest.json'),
            fixtures=manifest['frontend_fixtures'])
    assert len(builds)==4 and sum(b['fixtures'] for b in builds.values())==2
    all_runtime=[];full=[];early=[];settings=[];production=[];restores=[]
    observer=artifact(ROOT/'ports/nds/tools/run_frontend_emulator.py')['sha256']
    pixel=artifact(ROOT/'ports/nds/tools/frontend_pixel_oracle.py')['sha256']
    fat=artifact(ROOT/'ports/nds/tools/frontend_fat_oracle.py')['sha256']
    for relative in acceptance['reports']:
        path=ROOT/relative;r=read(path);assert r['passed'] is True,relative
        # Supplemental independent gates can have their own schemas; the root
        # manifest binds them explicitly without weakening frontend checks.
        if 'rom_sha256' not in r:
            all_runtime.append(dict(report=artifact(path),supplemental=True));continue
        build=builds[path.parent.parent.name]
        assert r['rom_sha256']==build['rom']['sha256'] and r['elf_sha256']==build['elf']['sha256']
        assert r['observer_sha256']==observer and r['pixel_oracle_sha256']==pixel and r['fat_oracle_sha256']==fat
        assert r['disk']['passed'] and set(r['disk']['changed_files'])<={'/DOK/SETTINGS.DAT'}
        assert r['index8_checks']>0 and r['rgb_checks']==r['index8_checks']
        item=dict(report=artifact(path),target=build['path'],mode=path.parent.name,
            index8_checks=r['index8_checks'],rgb_checks=r['rgb_checks'],states=len(r['rows']))
        all_runtime.append(item)
        if path.parent.name=='frontend-full':
            assert build['fixtures'] and r['all_original_pages_covered']
            assert len(r['language_preservation'])==6 and len(r['visual_alias'])==6
            assert {(x['language'],x['level']) for x in r['visual_alias']}=={(a,b) for a in (1,2) for b in (1,2,3)}
            assert all(x['quad_references']>0 and x['bytes']==32768 for x in r['visual_alias'])
            assert len(r['audio'])==2 and all(x['complete'] for x in r['audio'])
            for a in r['audio']:
                w=a['waveform'];assert w['complete_source_covered'] and w['source_samples']==474361 and w['whole_coefficient']>=.95
            assert len(r['level_memory'])==3
            assert all(m['sdk_heap_available']>=196608 and m['reserve_bytes']==655360 and m['closure_bytes']==3670016 for m in r['level_memory'])
            assert not r['last_audio_probe'][8] and not r['last_audio_probe'][9] and not r['last_audio_probe'][22]
            pages={(x['state'],x['level'] if x['state'] in (7,10) else 0,x['page']) for x in r['rows'] if x['state'] in (6,7,10,11)}
            item.update(language=r['initial_language'],original_pages=len(pages),
                visual_rows=6,visual_decoded_bytes=6*32768,language_switches=6,
                pcm_coefficients=[a['waveform']['whole_coefficient'] for a in r['audio']],
                memory=r['level_memory'])
            full.append(item)
        elif path.parent.name.startswith('frontend-early-'):
            e=r['early_input'];assert e['video_frames']==1
            assert not r['disk']['changed_files'];early.append((build['path'],e['button'],e['phase']))
        elif path.parent.name in ('frontend-settings','frontend-restart','frontend-corrupt','frontend-readonly'):
            assert not build['fixtures'];settings.append(path.parent.name)
        elif path.parent.name=='frontend-world-smoke':
            assert not build['fixtures'] and r['last_probe'][31]==0
            assert r['memory']['sdk_heap_available']>=196608 and r['memory']['reserve_bytes']==655360
            assert not r['disk']['changed_files'];production.append(item)
        elif path.parent.name=='frontend-restore':
            assert not build['fixtures']
            assert r['restore_observer_sha256']==artifact(ROOT/'ports/nds/tools/run_frontend_restore_emulator.py')['sha256']
            assert {(x['level'],x['language']) for x in r['restore_points']}=={(a,b) for a in (1,2,3) for b in (1,2)}
            h,m=r['hud_restore'],r['map_restore']
            assert h['pixel_checks']==h['hardware_pixel_checks']==116736
            assert m['index8_checks']==m['rgb_checks']==589824
            item.update(restore_points=6,hud_index8_checks=h['pixel_checks'],hud_rgb_checks=h['hardware_pixel_checks'],
                map_index8_checks=m['index8_checks'],map_rgb_checks=m['rgb_checks'])
            restores.append(item)
    assert len(full)==2 and {f['language'] for f in full}=={1,2}
    assert sum(f['original_pages'] for f in full)==34
    assert len(early)==len(set(early))==32
    assert len(settings)==4 and len(set(settings))==4 and len(production)==2 and len(restores)==2
    archive=read(E/'final-archive.json');assert archive['passed']
    for group in ('sources','builds','generated','reports'):
        for pair in archive[group]:
            assert artifact(ROOT/pair['source']['path'])==pair['source']
            assert artifact(ROOT/pair['archive']['path'])==pair['archive']
            assert pair['source']['sha256']==pair['archive']['sha256']
    result=dict(task='N21',status='ready-emulator',passed=True,software_checks_passed=True,
        gates={name:artifact(E/name) for name in gate_names},root_acceptance=artifact(acceptance_path),
        builds=list(builds.values()),runtime=all_runtime,full=full,
        totals=dict(original_pages=34,early_short_press_cases=32,production_world_smokes=2,
            settings_cases=4,controller_host_checks=142931,visual_host_checks=1049583,
            full_index8_checks=sum(r['index8_checks'] for r in full),
            full_rgb_checks=sum(r['rgb_checks'] for r in full),visual_cache_bytes=12*32768,
            restored_hud_index8_checks=sum(r['hud_index8_checks'] for r in restores),
            restored_hud_rgb_checks=sum(r['hud_rgb_checks'] for r in restores),
            restored_map_index8_checks=sum(r['map_index8_checks'] for r in restores),
            restored_map_rgb_checks=sum(r['map_rgb_checks'] for r in restores)),
        archive=artifact(E/'final-archive.json'),supersession=artifact(E/'supersession.json'),
        joint_performance_budget_passed=False,physical_hardware=False,
        listening_deferred_by_user=True,next_task='N21P',limits=[
            'DS/DSLite, physical LCD/touch/IRQ/stack and subjective listening remain N24',
            'N22 consumes real FINISH/death; EINTRO and POPAL display poses here are explicitly compiled diagnostic fixtures',
            'No game saves; only versioned language settings are written to microSD',
            'N20/N21 joint performance remains open for N21P; idle/dirty map and immediate gestures requested by user',
            'SDK heap is exactly196608bytes at the final fixture, so further code/state must reclaim budget rather than spending reserve'])
    (ROOT/'analysis/nds-n21-frontend.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,totals=result['totals'])))

if __name__=='__main__':main()
