"""Collect the verified native hybrid world-OBJ trial (not selected)."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    def read(p):return json.loads((ROOT/p).read_text(encoding='utf-8-sig'))
    def sha(p):return hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    folder=Path('build/g20a-world-obj-native-final')
    paths=[folder/n for n in ('prototype-build.json','target.json','profile.json','ppu18-live.json','qa/verification.json')]
    paths += [Path('build/g20a-world-obj-native-repeat/prototype-build.json'),
              Path('build/g20a-hw-entity-rect-v2/spatial.json'),Path('build/g20a-columns-v6/control-live24.json'),
              Path('analysis/g20a-world-obj-bringup.json')]
    manifest,target,profile,ppu,qa,repeat,base,live,bringup=map(read,paths)
    variants=manifest['variants'];trial=variants['hw_v128_world_obj']['rom_sha256']
    control='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    assert trial=='807f3e295143d5f01d97b5e262960543f160996800ddb719bce811f78663916c'
    for name in ('hw_v128_world_obj','hw_v128_entity_rect','hw_v128_world_obj_probe'):
        expected=variants[name]['rom_sha256']
        assert expected==repeat['variants'][name]['rom_sha256']
        for directory in (folder,Path('build/g20a-world-obj-native-repeat')):
            assert sha(directory/name/'dok-diagnostic.gba')==expected
    assert variants['hw_v128_entity_rect']['rom_sha256']==control
    assert target['rom_sha256']==profile['rom_sha256']==ppu['after']['rom_sha256']==qa['after']['rom_sha256']==trial
    assert base['rom_sha256']==live['rom_sha256']==ppu['before']['rom_sha256']==qa['before']['rom_sha256']==control
    assert ppu['live']['rom_sha256']==trial and qa['status']=='passed'
    assert qa['script_sha256']==sha(Path('scripts/verify_gba_world_obj_native.py'))
    assert profile['script_sha256']==sha(Path('scripts/profile_gba_world_obj.py'))
    assert not profile['forced_pcm'] and profile['cold_reset_after_freeze']
    assert len(qa['after']['rows'])==40 and sum(r['mode4_cycle'] for r in qa['after']['rows'])==6
    assert all(r['max_scanline_bound']<=954 for r in qa['after']['rows'])
    assert bringup['ppu_exact_cases']==54 and len(bringup['ppu_pressure_cases'])==2
    assert bringup['trial_rom_sha256']==variants['hw_v128_world_obj_probe']['rom_sha256']
    key=lambda r:(r['level'],r['cell'],r['heading'])
    ref={key(r):r for r in base['rows']}
    heavy=[dict(level=r['level'],cell=r['cell'],heading=r['heading'],cpu_ratio=r['median']/ref[key(r)]['median'],
        baseline_fps=ref[key(r)]['presented_fps'],fps=r['presented_fps'],work=r['world_obj_work']) for r in target['rows']]
    assert len(heavy)==5 and all(r['cpu_ratio']>1 for r in heavy)
    assert all(r['work'][9]==0 for r in heavy),'Warm texture cache must not be confused with texture upload cost'
    assert len(ppu['after']['rows'])==18 and ppu['frozen_initial_world_reset']
    for r in target['rows']+ppu['live']['rows']+live['rows']:
        assert r['audio']['underruns']==0 and r['sound_active_frames']>0
    for r in ppu['live']['rows']+live['rows']:assert r['time_conservation_error_ticks']==0
    preserved={}
    for name,expected in (
        ('DoK-port_hardware_cached_prototype.gba',control),
        ('DoK-port_hardware_rows96_prototype.gba','97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
        ('DoK-port_hardware_rows72_prototype.gba','17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')):
        assert sha(Path('artifacts/gba')/name)==expected;preserved[name]=expected
    short=lambda rows:[dict(level=r['level'],language=r['language'],fps=r['presented_fps'],
        max_motion_debt=r['max_motion_debt']) for r in rows]
    result=dict(task='G20A',status='native-hybrid-world-obj-not-selected',goal_complete=False,
        performance_build_in_current_goal=False,trial_rom_sha256=trial,control_rom_sha256=control,
        byte_exact_repeat=True,core_sha256=target['core_sha256'],heavy_cases=heavy,
        live24=dict(before=short(live['rows']),after=short(ppu['live']['rows'])),profile=profile,
        ppu=dict(cases=18,max_changed_rgb_pixels=max(r['different_rgb_pixels'] for r in ppu['after']['rows']),
            max_overlay_changes=max(r['different_overlay_rgb_pixels'] for r in ppu['after']['rows'])),
        verification=dict(poses=40,mode4_cycles=6,scope=qa['scope'],
            revealed_direct_obj_pixels=sum(r['revealed_direct_obj_pixels'] for r in qa['after']['rows']),
            foreground_ppu_pixels_checked=sum(r['foreground_ppu_pixels_checked'] for r in qa['after']['rows']),
            max_scanline_bound=max(r['max_scanline_bound'] for r in qa['after']['rows'])),
        memory=dict(ewram_bytes=261424,ewram_free_bytes=720,iwram_bytes=24072,
            iwram_free_before_stack_reserve_bytes=504,stack_reserve_bytes=8192,
            main_stack=ppu['live']['stack'],irq_stack=ppu['live']['irq_stack']),
        world_obj_tables=manifest['world_obj_tables'],preserved_roms=preserved,
        limitations=[
            'V1-V8 superseded: Mode4 could corrupt persistent OBJ row maps in shared interval scratch; final invalidates all OBJ state across Mode4, with six verified pause/resume cycles.',
            'Early asset records used image flags alone; final includes definition sprite_flags and falls back for ambiguous resource opacity.',
            'Original source dimensions and 16 LIGHT rows retained; perspective approximated by 16x32 diagonal affine strips, with CPU fallback for crop, magnification, residency and scanline capacity.',
            'Specific hybrid GPU/source-texture plus CPU coverage path. Does not disprove a pure hardware renderer or GPU output fed by a sector clip representation.',
            'Five heavy PCM views, six live24 runs, 18 paired PPU views and 40 native composition/geometry/lease poses; no full440 sweep or full gameplay/physical GBA acceptance.',
            'Profile instrumented without forced PCM; use unprofiled target report for speed comparison. Live motion debt retained; not a simulation budget pass.'],
        decision='Keep control2775 and quality96/72 artifacts; stop local tuning of this losing hybrid. Test genuine sector clip/span architecture next.',
        evidence=[dict(path=p.as_posix(),sha256=sha(p)) for p in paths])
    (ROOT/'analysis/g20a-world-obj-native.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],heavy_cases=heavy,ppu=result['ppu'],verification=result['verification']),indent=2))


if __name__=='__main__':main()
