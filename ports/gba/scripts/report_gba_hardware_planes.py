"""Collect executed G20A hardware-plane experiments; never claim full acceptance."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[3]
BASE='7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy-rom',action='store_true');args=parser.parse_args()
    final='research/experiments/g20a-hw-final';ready='research/experiments/g20a-hw-ready'
    control=read(final+'/source_control/build-report.json')
    assert control['rom_sha256']==BASE
    arm=read(final+'/arm.json');assert arm['status']=='passed'
    exact=read(ready+'/direct-native-ppu.json');cache=read(final+'/cache-ppu.json');relink=read(final+'/relink-ppu.json')
    for result,count in ((exact,378),(cache,396),(relink,18)):
        assert len(result['after']['rows'])==count
        assert all(r['different_rgb_pixels']==0 for r in result['after']['rows'])
    build=read(final+'/hw_hud_cache/build-report.json');rom=ROOT/final/'hw_hud_cache/dok-diagnostic.gba'
    assert sha(rom)==build['rom_sha256']==arm['rom_sha256']==cache['after']['rom_sha256']==relink['after']['rom_sha256']
    repeat='research/experiments/g20a-hw-repeat'
    assert read(repeat+'/hw_hud_cache/build-report.json')['rom_sha256']==sha(rom)
    assert read(repeat+'/source_control/build-report.json')['rom_sha256']==BASE
    live={}
    for name in ('follow','walking'):
        result=read(final+'/'+name+'.json');assert result['rom_sha256']==sha(rom)
        for row in result['rows']:
            assert row['hardware_bg_new_overflows']==row['hardware_bg_new_cold_loads']==row['audio']['underruns']==row['time_conservation_error_ticks']==0
            assert all(s.get('flow_state')==2 and s.get('ui_state')==0 for s in row['samples'])
        live[name]={**result,'rows':[{k:v for k,v in r.items() if k!='samples'} for r in result['rows']]}
    assert live['walking']['irq_stack']['untouched_margin_bytes']>0
    originals=[]
    for entry in read('analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if entry['path'].startswith('originals/'):
            assert sha(ROOT/entry['path'])==entry['sha256'];originals.append(entry)
    assert len(originals)==22
    preserved={}
    for name,expected in [('DoK-port_pre_alpha.gba',BASE),
        ('DoK-port_optimized_prototype.gba','b36472a0701f19a41fc009e003f0f78a4493921e1cf19f2b51c266cdfeb42501'),
        ('DoK-port_floor_4x2_prototype.gba','04dfe358654cc865a22ec9e674217eb4cde7583bde4f61d57d361fa1e61b5d11')]:
        assert sha(ROOT/'artifacts/gba'/name)==expected;preserved[name]=expected
    artifact=ROOT/'artifacts/gba/DoK-port_hardware_planes_prototype.gba'
    if args.copy_rom:shutil.copyfile(rom,artifact)
    native=read(ready+'/bg-native-ppu.json')
    comparison={name:{kind:[{k:v for k,v in r.items() if k!='samples'} for r in result['rows']]
                      for kind,result in read(ready+'/'+name+'-routes.json').items()} for name in ('exact','coarse')}
    paths=[final+'/'+name+'.json' for name in ('arm','cache-ppu','relink-ppu','follow','walking')]
    paths += [ready+'/'+name+'.json' for name in ('direct-native-ppu','bg-native-ppu','exact-routes','coarse-routes')]
    paths += [repeat+'/'+name+'/build-report.json' for name in ('hw_hud_cache','source_control')]
    symbols=build['symbols'];ewram=symbols['__bss_end']-symbols['__data_start'];iwram=symbols['__iwram_end']-symbols['__iwram_start']
    result=dict(task='G20A',status='measured-hardware-plane-prototype-not-full-acceptance',targets_met=False,
        date='2026-10-27',build_dir=final+'/hw_hud_cache',rom_sha256=sha(rom),rom_bytes=rom.stat().st_size,
        artifact=str(artifact.relative_to(ROOT)).replace('\\','/'),artifact_copied=args.copy_rom,
        ram=dict(ewram_bytes=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
        validation=dict(direct_native_rgb_exact_frames=378,hud_cache_rgb_exact_frames=396,final_relink_rgb_exact_frames=18,clean_repeat_rom_byte_exact=True,arm=arm),
        native_rgb_comparison=dict(scope='GPU plane sampling changes; foreground-mask differences include native plane occlusion boundaries. Not pixel-exact.',
            frames=len(native['after']['rows']),max_foreground_boundary_differences=max(r['different_foreground_rgb_pixels'] for r in native['after']['rows']),
            selected_rows=[r for r in native['after']['rows'] if 'detail' not in r['name']]),
        live=live,paired_routes=comparison,originals_unchanged=originals,preserved_artifacts=preserved,
        input_reports={p:sha(ROOT/p) for p in paths},build_manifest=read(final+'/prototype-build.json'),
        limitations=['Replacement BIOS mGBA 16.78 MHz, not physical GBA or whole-level minimum FPS.',
            'Earlier live fixtures pinned a diagnostic camera. Follow releases it; walking also presses Up.',
            'Follow uses active 22050Hz PCM on every sample. Walking includes natural completion/replacement of audio.',
            'No complete logic/door/grate/stress/flow acceptance for this hardware candidate yet.',
            '32x32 source-derived tiles, three LIGHT rows for GPU planes, merged inset0/10 base ceiling, depth cap3600.',
            'Walls/actors/weapon/HUD retain source detail; occlusion at plane boundaries is not pixel-identical.',
            'BSP and procedural trials remain separate; procedural textures are deferred.'])
    (ROOT/'analysis/gba/g20a-hardware-planes.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:result[k] for k in ('status','targets_met','rom_sha256','rom_bytes','ram')}))

if __name__=='__main__':main()
