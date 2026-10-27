"""Bind the tiled-output bring-up to actual PPU/ARM evidence; no promotion."""
import json
from pathlib import Path
import shutil

from report_gba_second import ROOT, read, checked
from verify_gba import sha


def main():
    root=ROOT/'build/g20a-hw-pack-ready';build=root/'hw_obj_arm'
    comparison=read(root/'ppu-comparison.json');meta=read(build/'build-report.json');digest=sha(build/'dok-diagnostic.gba')
    assert digest==meta['rom_sha256']==comparison['after']['rom_sha256']==comparison['live']['rom_sha256']
    assert comparison['before']['rom_sha256']==sha(ROOT/'build/g20a-sides/sides_exact/dok-diagnostic.gba')
    assert len(comparison['after']['rows'])==18 and comparison['languages']==[0,1]
    assert all(r['different_rgb_pixels']==0 and r['max_channel_difference']==0 and r['dispcnt']&7==1 for r in comparison['after']['rows'])
    arm=checked(root/'hw-arm.json',digest);assert read(root/'hw-arm.json')['hardware_pack_vectors']==768
    baseline=sha(ROOT/'build/select-menu-fix/dok-diagnostic.gba')
    assert sha(root/'baseline/dok-diagnostic.gba')==sha(root/'main_control/dok-diagnostic.gba')==baseline
    assert all(r['sound_active_frames']>0 for r in comparison['live']['rows'])
    copies={}
    for folder,name in (('g20a-hw-pack-ready','g20a-hardware-ppu.json'),('g20a-lookup','g20a-lookup-trial.json'),
                        ('g20a-vertex-fixed','g20a-vertex-trial.json'),('g20a-ray-sides','g20a-ray-trial.json')):
        source=ROOT/'build'/folder/('ppu-comparison.json' if folder=='g20a-hw-pack-ready' else 'comparison.json')
        dest=ROOT/'analysis'/name;shutil.copy2(source,dest)
        copies[folder]=dict(path=dest.relative_to(ROOT).as_posix(),sha256=sha(dest))
    for r in read(ROOT/'build/g20a-lookup/comparison.json')['frozen']['lookup_world']['rows']:
        name=r['name']+'.idx'
        assert (ROOT/'build/g20a-lookup/lookup_world'/name).read_bytes()==(ROOT/'build/g20a-sides/sides_world'/name).read_bytes()
    originals=[r for r in read(ROOT/'analysis/artifact-cleanup-20261026.json')['preserved_sha256'] if r['path'].startswith('originals/')]
    for record in originals:assert sha(ROOT/record['path'])==record['sha256']
    result=dict(task='G20A',status='hardware-composition-bringup-not-optimized',targets_met=False,
        rom_sha256=digest,rom_path='build/g20a-hw-pack-ready/hw_obj_arm/dok-diagnostic.gba',rom_bytes=meta['rom_bytes'],
        arm=arm,actual_ppu_rgb_exact_frames=18,software_floor_still_enabled=True,
        original_files_sha_matched=len(originals),normal_main_renderer_rom_byte_identical=True,
        ewram_static_bytes=meta['ewram_static_bytes'],
        iwram_static_bytes=meta['symbols']['__iwram_end']-meta['symbols']['__iwram_start'],
        measurements=copies,source_manifest=dict(path='build/g20a-hw-pack-ready/prototype-build.json',sha256=sha(root/'prototype-build.json')),
        unchanged_public_artifacts={name:sha(ROOT/'artifacts/gba'/name) for name in
            ('DoK-port_pre_alpha.gba','DoK-port_optimized_prototype.gba','DoK-port_floor_4x2_prototype.gba')},
        script_sha256=sha(__file__),next_step='Direct tiled foreground output to remove packing, then affine BG floor sampling.',
        limitations=['Not a faster candidate; no artifact promotion.',
            'Only18 frozen PPU scenes and six live PCM fixtures; no full functional/UI/flow/game stress suite.',
            'Weapon overlay with distinct odd/even pixels, all water phases and physical GBA not verified.',
            'The current prototype still samples both horizontal planes on CPU.',
            'Future affine BG/HBlank floor, material streaming and row lighting are not implemented.'])
    (ROOT/'analysis/g20a-hardware-bringup.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Hardware composition18 PPU frames exact, ARM pack768 vectors passed; normal ROM unchanged;15+ still open.')


if __name__=='__main__':main()
