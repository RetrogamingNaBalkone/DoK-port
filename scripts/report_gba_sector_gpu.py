"""Collect the qualified diagonal sector-fed GPU checkpoint; not a family rejection."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    read=lambda p:json.loads((ROOT/p).read_text(encoding='utf-8-sig'))
    sha=lambda p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()
    folder=Path('research/experiments/g20a-sector-gpu-v8');repeat_folder=Path('research/experiments/g20a-sector-gpu-repeat')
    manifest=read(folder/'prototype-build.json');repeat=read(repeat_folder/'prototype-build.json')
    control='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    assert manifest['variants']['hw_v128_entity_rect']['rom_sha256']==control
    for name,build in manifest['variants'].items():
        assert build['rom_sha256']==repeat['variants'][name]['rom_sha256']
        for directory in (folder,repeat_folder):assert sha(directory/name/'dok-diagnostic.gba')==build['rom_sha256']
    assert all(sha(Path(p))==v for p,v in manifest['sources'].items()),'Current native source snapshot'
    qa=read(folder/'visibility.json');profile=read(folder/'profile.json')
    assert qa['script_sha256']==sha(Path('scripts/verify_gba_sector_gpu.py'))
    assert profile['script_sha256']==sha(Path('scripts/profile_gba_sector_gpu.py'))
    assert len(qa['after']['rows'])==len(qa['cpu']['rows'])==58
    assert sum(r['mode4_cycle'] for r in qa['after']['rows'])==6
    assert all(r['gpu_pieces']>0 for r in qa['after']['rows'] if r['mode4_cycle'])
    assert sum(bool(r['gpu_pieces']) for r in qa['after']['rows'])==46
    assert sum(bool(r['weapon_fixture']) for r in qa['after']['rows'])==18
    base=read(folder/'control-target.json');key=lambda r:(r['level'],r['cell'],r['heading'])
    baselines={key(r):r for r in base['rows']};results={}
    paths=[folder/n for n in ('prototype-build.json','visibility.json','profile.json','control-target.json')]
    paths.append(repeat_folder/'prototype-build.json')
    for name,prefix in (('hw_v128_sector_gpu_thumb',''),('hw_v128_sector_gpu_off','off-')):
        target=read(folder/(prefix+'target.json'));live=read(folder/(name+'-live24.json'));build=manifest['variants'][name]
        assert target['rom_sha256']==live['rom_sha256']==build['rom_sha256']
        for r in target['rows']+live['rows']:assert r['audio']['underruns']==0 and r['sound_active_frames']>0
        assert all(r['time_conservation_error_ticks']==0 for r in live['rows'])
        results[name]=dict(rom_sha256=build['rom_sha256'],rom_bytes=build['rom_bytes'],
            heavy=[dict(level=r['level'],cell=r['cell'],heading=r['heading'],cpu_ratio=r['median']/baselines[key(r)]['median'],
                fps=r['presented_fps'],baseline_fps=baselines[key(r)]['presented_fps']) for r in target['rows']],
            live24=[dict(level=r['level'],language=r['language'],fps=r['presented_fps'],max_motion_debt=r['max_motion_debt']) for r in live['rows']],
            memory=dict(ewram_bytes=build['ewram_static_bytes'],ewram_free=262144-build['ewram_static_bytes'],
                iwram_bytes=build['symbols']['__iwram_end']-build['symbols']['__iwram_start'],
                iwram_free_before_stack=24576-(build['symbols']['__iwram_end']-build['symbols']['__iwram_start']),
                main_stack=live['stack'],irq_stack=live['irq_stack']))
        paths += [folder/(prefix+'target.json'),folder/(name+'-live24.json')]
    assert profile['rom_sha256']==qa['after']['rom_sha256']==results['hw_v128_sector_gpu_thumb']['rom_sha256']
    assert qa['cpu']['rom_sha256']==results['hw_v128_sector_gpu_off']['rom_sha256']
    assert all(r['state'][1]==1 for r in profile['rows']),'All five heavy views require whole-frame CPU fallback'
    live_control=read(folder/'hw_v128_entity_rect-live24.json');assert live_control['rom_sha256']==control
    paths.append(folder/'hw_v128_entity_rect-live24.json')
    rows=qa['after']['rows'];count=lambda k:sum(r[k] for r in rows)
    preserved={}
    for name,expected in (
        ('DoK-port_hardware_cached_prototype.gba',control),
        ('DoK-port_hardware_rows96_prototype.gba','97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
        ('DoK-port_hardware_rows72_prototype.gba','17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')):
        assert sha(Path('artifacts/gba')/name)==expected;preserved[name]=expected
    result=dict(task='G20A',status='qualified-sector-gpu-diagonal-checkpoint-not-selected',goal_complete=False,
        performance_build_in_current_goal=False,control_rom_sha256=control,variants=results,profile=profile,
        verification=dict(poses=58,mode4_cycles=6,weapon_poses=18,active_gpu_poses=46,
            source_texels=count('source_texels'),gpu_candidate_clip_pixels=count('gpu_clip_pixels'),
            full_ppu_pixels=count('ppu_pixels'),original_base_pixels=count('base_pixels'),overlay_changes=0,
            mask_missing=count('missing_physical_mask_pixels'),mask_extra=count('extra_physical_mask_pixels'),
            mask_missing_max=max(r['missing_physical_mask_pixels'] for r in rows),mask_extra_max=max(r['extra_physical_mask_pixels'] for r in rows),
            missing_interior_radius2=count('missing_interior_radius2_pixels'),extra_beyond_radius2=count('extra_beyond_radius2_pixels'),
            missing_interior_radius2_max=max(r['missing_interior_radius2_pixels'] for r in rows),
            extra_beyond_radius2_max=max(r['extra_beyond_radius2_pixels'] for r in rows),scope=qa['scope']),
        byte_exact_repeat=True,core_sha256=qa['core_sha256'],gpu_asset_tables=manifest['sector_gpu_tables'],
        control_live24=[dict(level=r['level'],language=r['language'],fps=r['presented_fps']) for r in live_control['rows']],
        limitations=[
            'CPU sector BSP/static walls/rims/low ceilings, BG3 base planes, BG2 CPU foreground and clipped source-mip OBJ dynamics. Not a full GPU static-wall renderer.',
            'Diagonal matrices with whole flat billboards and narrow perspective strips. Wider bounded-error affine/sheared pieces remain a significant untested representation; this checkpoint does not reject that family.',
            'All five heavy views exceed the scanline budget or16KiB dynamic atlas and use complete CPU dynamic fallback after attempted GPU preparation. These FPS are not successful pure-GPU heavy scenes.',
            'Actual nonzero source mip/LIGHT texels, full PPU composition and the sector static clip contract are checked; no independent completeness proof of dynamic visibility. Alpha loss is measured, not accepted as visually minor.',
            'Current source uses unshaded nearest-sampled mips and the original16-row LIGHT during clipped source copy. A prelit-all-images prototype bank was too large and is superseded.',
            'Early V1/V2 physical/logical X mapping was wrong; earlier scanline/atlas/admission drafts superseded. Only V8 qualifies as this checkpoint.',
            '58 poses include repeated cameras with18 weapon fixtures. No map-edit/full440/full gameplay/physical GBA acceptance of this losing checkpoint.',
            'No Performance ROM, AI radius or darkness/draw-distance change implemented.'],
        next_step='Bounded test of wider perspective fragments with affine shear/error bounds; only after its disposition choose the main architecture and finish remaining-cost handoff. Do not locally polish this diagonal-strip version.',
        preserved_roms=preserved,evidence=[dict(path=p.as_posix(),sha256=sha(p)) for p in paths])
    (ROOT/'analysis/g20a-sector-gpu-checkpoint.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(result['status'],result['verification'])


if __name__=='__main__':main()
