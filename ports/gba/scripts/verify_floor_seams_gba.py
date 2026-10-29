"""Verify that floor underfill only repairs black background at wall edges."""

import argparse
import json
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT/'ports/gba/scripts'))
from benchmark_gba_hardware import capture
from benchmark_gba_spatial_sweep import positions
from verify_sprites_gba import load_json
from verify_gba import sha


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--reference-build',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    cameras=[]
    for language in (0,1):
        for level in range(3):
            model=load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json')
            for cell,_ in positions(model):
                for heading in range(0,360,45):
                    cameras.append(dict(name=f'L{level+1}-{language}-{cell}-{heading}',level=level,language=language,
                                        camera=[cell%64*480+240,-300,cell//64*480+240,heading]))
    before,old=capture(args.reference_build,core,cameras,save_images=False)
    after,new=capture(args.build_dir,core,cameras,save_images=False)
    repairs=[]
    for a,b in zip(before['rows'],after['rows']):
        name=a['name'];assert name==b['name']
        assert a['coverage_sha256']==b['coverage_sha256'],name
        for suffix in ('#foreground','#overlay'):assert old[name+suffix]==new[name+suffix],name
        changes=[]
        for p in range(38400):
            x,y=p%240,p//240
            if old[name][p*3:p*3+3]==new[name][p*3:p*3+3]:continue
            assert 72<=y<144 and not new[name+'#foreground'][p],(name,x,y,'foreground')
            assert old[name][p*3:p*3+3]==b'\0\0\0',(name,x,y,'nonblack')
            changes.append((x,y))
        repairs.append(dict(name=name,pixels=len(changes),examples=changes[:4]))
    result=dict(status='passed',rom_sha256=after['rom_sha256'],reference_rom_sha256=before['rom_sha256'],
                script_sha256=sha(__file__),core_sha256=sha(core),cases=len(repairs),
                changed_views=sum(bool(r['pixels']) for r in repairs),repaired_pixels=sum(r['pixels'] for r in repairs),
                foreground_and_coverage_exact=True,only_black_floor_background_changed=True,rows=repairs,
                scope='Original maps, 3 levels / RU+EN, 880 stationary PPU poses. Not physical GBA or a full playthrough.')
    args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Floor repair:',result['cases'],'poses,',result['changed_views'],'changed,',result['repaired_pixels'],'black pixels filled')


if __name__=='__main__':main()
