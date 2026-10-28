"""Collect actual evidence for the independent128-row G20A prototype."""

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
FOLDER='research/experiments/g20a-hw-face-box'
BASE='7babd40f913eb12f77c93c9df917cea432b0cbe3bd83e11da45ba08dec917ea8'
def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--copy-rom',action='store_true');args=parser.parse_args()
    build=read(FOLDER+'/hw_v128_box/build-report.json');digest=build['rom_sha256']
    rom=ROOT/FOLDER/'hw_v128_box/dok-diagnostic.gba';assert sha(rom)==digest
    repeat=read('research/experiments/g20a-hw-rows-final-repeat/hw_v128_box/build-report.json')
    assert repeat['rom_sha256']==digest
    assert read('research/experiments/g20a-hw-rows-final-repeat/source_control/build-report.json')['rom_sha256']==BASE
    baseline_sprites=read(FOLDER+'/source-control-sprites.json')
    assert baseline_sprites['status']=='passed' and baseline_sprites['rom_sha256']==BASE
    names=('arm','ui','flow','sprites','water','ai','ai-combat','interactions','transitions','stress')
    checks={name:read(FOLDER+'/'+name+'.json') for name in names}
    for name,check in checks.items():assert check['status']=='passed' and check['rom_sha256']==digest,name
    assert len(checks['ai-combat']['groups'])==28
    assert checks['water']['phase_cases']==144 and checks['water']['ppu_pixels']>0
    assert checks['ui']['hardware_ppu_frames']==22
    clock=read(FOLDER+'/time.json');assert clock['rom_sha256']==digest and clock['edge_vectors_on_arm']=='passed'
    exact=read(FOLDER+'/exact128.json');quality=read(FOLDER+'/quality.json')
    assert len(exact['after']['rows'])==len(quality['after']['rows'])==396
    assert all(r['different_rgb_pixels']==0 for r in exact['after']['rows'])
    assert all(r['different_overlay_rgb_pixels']==0 for r in quality['after']['rows'])
    transitions=checks['transitions']['rows'];assert len(transitions)==282
    assert all(not r['mixed_frames'] and not r['overflows'] for r in transitions)
    live={}
    for mode in ('follow','turn120','walking120'):
        result=read(FOLDER+'/hw_v128_box-'+mode+'.json');assert result['rom_sha256']==digest
        for row in result['rows']:
            assert row['time_conservation_error_ticks']==row['hardware_bg_new_overflows']==row['hardware_bg_new_cold_loads']==row['audio']['underruns']==0
            assert all(s['flow_state']==2 and s['ui_state']==0 and s['vblanks']>=2 for s in row['samples'])
            row['minimum_upload_budget_margin_cycles']=min(s['hardware_present'][0]-s['hardware_obj'][2] for s in row['samples'])
            assert row['minimum_upload_budget_margin_cycles']>0
            del row['samples']
        live[mode]=result
    originals=[]
    for entry in read('analysis/artifact-cleanup-20261026.json')['preserved_sha256']:
        if entry['path'].startswith('originals/'):
            assert sha(ROOT/entry['path'])==entry['sha256'];originals.append(entry)
    assert len(originals)==22
    artifact=ROOT/'artifacts/gba/DoK-port_hardware_128_prototype.gba'
    if args.copy_rom:shutil.copyfile(rom,artifact)
    if artifact.exists():assert sha(artifact)==digest
    symbol=build['symbols'];ewram=symbol['__bss_end']-symbol['__data_start'];iwram=symbol['__iwram_end']-symbol['__iwram_start']
    paths=[FOLDER+'/'+n+'.json' for n in (*names,'time','exact128','quality')]
    paths += [FOLDER+'/hw_v128_box-'+mode+'.json' for mode in ('follow','turn120','walking120')]
    paths += ['research/experiments/g20a-hw-row-probe/rows128.json','research/experiments/g20a-hw-row-probe/rows144.json']
    old_qa={name:read('research/experiments/g20a-hw-early-qa/'+name+'.json') for name in ('ui','flow','sprites','water')}
    for value in old_qa.values():assert value['status']=='passed' and value['rom_sha256']=='240669c13508acba18fa08a68dd0bc793c02366561ee0b617d7c1e62d976f014'
    result=dict(task='G20A',date='2026-10-27',status='measured-separate128-row-prototype',targets_met=False,
        artifact=artifact.relative_to(ROOT).as_posix(),artifact_copied=args.copy_rom,rom_sha256=digest,rom_bytes=rom.stat().st_size,
        build_dir=FOLDER+'/hw_v128_box',quality=dict(scene_rows=128,displayed_scene_rows=144,logical_columns=120,
            gpu_floor_rows=144,native_ui=True,weapon_unchanged=True,exact_optimization_comparison_frames=396,
            comparison_with144=dict(frames=396,max_changed_pixels=max(r['different_rgb_pixels'] for r in quality['after']['rows']),
                max_overlay_changed_pixels=0)),
        ram=dict(ewram=ewram,ewram_free=262144-ewram,iwram_before_stacks=iwram,iwram_free_before_stacks=24576-iwram,stack_reserve=8192),
        live=live,validation={**checks,'clock':clock,'clean_repeat_byte_exact':True,
            'actual_transition_video_frames':sum(r['video_frames'] for r in transitions)},
        earlier144_rom_additional_qa=old_qa,mode4_harness_regression=baseline_sprites,originals_unchanged=originals,
        build_manifest=read(FOLDER+'/prototype-build.json'),input_reports={path:sha(ROOT/path) for path in paths},
        limitations=['15+ not met in LEVEL2 turning:14.75FPS over120 presentations.24.1/30 not achieved.',
            '128 CPU scene rows change wall/sprite vertical sampling; floor144 rows, weapon and UI preserved. No release quality choice made.',
            'Artificial all-awake stress remains0.07..0.37FPS with growing motion debt; pool capacity11.56..18.86FPS. A status passed verifies guards/logic, not frame budget.',
            'Stress harness uses a diagnostic camera; runtime-camera target measurements are recorded separately in live.',
            'GPU water checks source-derived32x32 pixels and PPU composition, not original affine floor geometry.',
            'Full independent combat-effect pixel oracle and item presentation regression remain open for hardware.',
            'No whole-level minimum FPS, physical GBA or original BIOS verification.',
            'Procedural/BSP alternatives remain unselected; all prior ROM artifacts preserved.'])
    (ROOT/'analysis/gba/g20a-hardware-rows.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(digest,'EWRAM',ewram,'IWRAM',iwram)


if __name__=='__main__':main()
