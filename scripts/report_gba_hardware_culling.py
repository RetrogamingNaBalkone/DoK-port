"""Collect the measured G20A culling/presentation prototype and its actual QA."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
BASE='7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8'
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--copy-rom',action='store_true');args=parser.parse_args()
    final='research/experiments/g20a-hw-early-qa';repeat='research/experiments/g20a-hw-early-qa-repeat';variant='hw_early_present'
    build=read(final+'/'+variant+'/build-report.json');rom=ROOT/final/variant/'dok-diagnostic.gba';digest=sha(rom)
    assert digest==build['rom_sha256']==read(repeat+'/'+variant+'/build-report.json')['rom_sha256']
    for folder in (final,repeat):assert read(folder+'/source_control/build-report.json')['rom_sha256']==BASE
    ppu=read(final+'/ppu.json');assert len(ppu['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 for r in ppu['after']['rows'])
    transitions=read(final+'/transitions.json');assert transitions['status']=='passed' and len(transitions['rows'])==282
    assert all(not r['mixed_frames'] and not r['overflows'] for r in transitions['rows'])
    arm=read(final+'/arm.json');ai=read(final+'/ai.json');combat=read(final+'/ai-combat.json');interactions=read(final+'/interactions.json');clock=read(final+'/time.json')
    for check in (arm,ai,combat,interactions):assert check['status']=='passed' and check['rom_sha256']==digest
    assert len(combat['groups'])==28
    assert clock['rom_sha256']==digest and clock['edge_vectors_on_arm']=='passed' and clock['exact_rational_samples']
    live=read(final+'/live.json');summaries={}
    for name,result in live.items():
        assert result['rom_sha256']==digest and len(result['rows'])==6
        rows=[]
        for row in result['rows']:
            samples=row['samples'];assert len(samples)==(24 if name=='follow' else 120)
            assert row['time_conservation_error_ticks']==row['hardware_bg_new_overflows']==row['hardware_bg_new_cold_loads']==row['audio']['underruns']==0
            assert all(s['flow_state']==2 and s['ui_state']==0 and s['vblanks']>=2 for s in samples)
            margin=min(s['hardware_present'][0]-s['hardware_obj'][2] for s in samples);assert margin>0
            rows.append({**{k:v for k,v in row.items() if k!='samples'},'minimum_upload_budget_margin_cycles':margin,
                'upload_started_in_hidden_scene_rows':sum(144<=s['hardware_present'][1]<160 for s in samples)})
        summaries[name]={**result,'rows':rows}
    bface=read('research/experiments/g20a-hw-backface/ppu.json');side=read('research/experiments/g20a-hw-side-setup/ppu.json');masked=read('research/experiments/g20a-hw-masked/ppu.json')
    assert all(r['different_rgb_pixels']==0 for result in (side,masked) for r in result['after']['rows'])
    originals=[]
    for entry in read('analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if entry['path'].startswith('originals/'):
            assert sha(ROOT/entry['path'])==entry['sha256'];originals.append(entry)
    assert len(originals)==22
    old=ROOT/'artifacts/gba/DoK-port_hardware_planes_prototype.gba'
    assert sha(old)=='4dfbbe0e8a14352fcef5c182baef3ffdf0483b81345c8837b1530ee09f7f581d'
    assert sha(ROOT/'artifacts/gba/DoK-port_pre_alpha.gba')==BASE
    artifact=ROOT/'artifacts/gba/DoK-port_hardware_culling_prototype.gba'
    if args.copy_rom:shutil.copyfile(rom,artifact)
    paths=[final+'/'+n+'.json' for n in ('ppu','transitions','arm','ai','ai-combat','interactions','time','live')]
    paths += ['research/experiments/g20a-hw-backface/ppu.json','research/experiments/g20a-hw-side-setup/ppu.json','research/experiments/g20a-hw-masked/ppu.json','research/experiments/g20a-hw-masked/arm.json','research/experiments/g20a-hw-column-profile/attribution.json']
    s=build['symbols'];ewram=s['__bss_end']-s['__data_start'];iwram=s['__iwram_end']-s['__iwram_start']
    result=dict(task='G20A',date='2026-10-27',status='measured-culling-presentation-prototype-partial-acceptance',targets_met=False,
        artifact=artifact.relative_to(ROOT).as_posix(),artifact_copied=args.copy_rom,rom_sha256=digest,rom_bytes=rom.stat().st_size,
        build_dir=final+'/'+variant,ram=dict(ewram=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
        live=summaries,validation=dict(arm=arm,ai=ai,ai_combat=combat,interactions=interactions,clock=clock,
            rgb_exact_frames=396,transition_cases=282,actual_transition_video_frames=sum(r['video_frames'] for r in transitions['rows']),mixed_transition_frames=0,clean_repeat_byte_exact=True),
        quality=dict(backface_selected_rows=bface['after']['rows'],max_changed_pixels_in18_backface_views=max(r['different_rgb_pixels'] for r in bface['after']['rows']),
            side_setup_exact_frames=18,masked_sampler_exact_frames=18,other_plane_quality='Same source-derived32x32 floor/ceiling and three LIGHT rows as previous hardware prototype.'),
        preserved_hardware_rom_sha256=sha(old),originals_unchanged=originals,input_reports={p:sha(ROOT/p) for p in paths},
        build_manifest=read(final+'/prototype-build.json'),
        limitations=['15+ is observed only in the120-presentation walking scenarios, not all ordinary scenes; follow LEVEL1/2 still below15.',
            'No whole-level minimum FPS, full water/sprite/UI/flow/stress acceptance, physical GBA or original BIOS validation.',
            'Interaction logic passed; original affine visual oracle intentionally omitted for this quality prototype.',
            'Synthetic large camera seeks use an explicitly blanked transition instead of an impossible upload deadline.',
            'Presentation can begin at scanline144 because only the opaque bottom HUD remains; bottom HUD data is deferred until VBlank.',
            '282 held transitions test every PPU callback; this does not prove every continuous-gameplay video frame is tear-free.',
            'Procedural textures, previous BSP, C/ARM opaque kernels and gaps variant remain separate, unselected trials.'])
    (ROOT/'analysis/g20a-hardware-culling.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result[k] for k in ('status','targets_met','rom_sha256','rom_bytes','ram')}))

if __name__=='__main__':main()
