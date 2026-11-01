"""Native PPU short frozen walks/turns; equal poses, no claim of live-state equality."""
import json
import os
import re
from pathlib import Path
from verify_gba import png
from benchmark_gba_hardware import capture
from check_g25 import BUILDS, OUT
from check_g22 import ROOT
from verify_midrange_bats_gba import scenes

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    # Heavy ceilings, grilles, original key rooms/witches, and original start.
    poses=[(1,520,225),(1,3640,225),(2,1592,135),(2,2871,225),
           (2,3039,90),(3,1623,0),(3,1940,270),(3,693,0)]
    cameras=[]
    for lang in (0,1):
        for level,cell,heading in poses:
            for step in range(9):
                cameras.append(dict(name=f'L{level}-c{cell}-lang{lang}-{step:02}',
                    level=level-1,language=lang,
                    camera=[cell%64*480+240+step*8,-300,cell//64*480+240, (heading+step*2-8)%360],
                    fixture=dict(weapon=1,frame=0,shift=0)))
    trig=list(map(int,re.findall(r'-?\d+',(ROOT/'ports/gba/src/renderer_trig.inc').read_text().split('{',1)[1])))
    for lang in (0,1):
        for level in (1,3):
            model=json.loads((ROOT/f'research/evidence/runtime/ru/LEVEL{level}-runtime.json').read_text())
            for n,scene in enumerate(scenes(model,trig)):
                cameras.append(dict(name=f'bat-L{level}-lang{lang}-{n}',level=level-1,language=lang,
                    camera=[scene['hero'][0],-300,scene['hero'][1],scene['heading']],
                    fixture=dict(weapon=1,frame=0,shift=0)))
    results={};images={}
    for name,build in BUILDS.items():
        results[name],images[name]=capture(build,core,cameras,save_images=False)
    comparisons={}
    for name in ('rows96','lod'):
        rows=[]
        for b,c in zip(results['control']['rows'],results[name]['rows']):
            assert b['name']==c['name']
            key=b['name'];bi=images['control'][key];ci=images[name][key]
            rows.append(dict(name=key,coverage_equal=b['coverage_sha256']==c['coverage_sha256'],
                leases_equal=b['lease_sha256']==c['lease_sha256'],
                different_rgb_pixels=sum(bi[i:i+3]!=ci[i:i+3] for i in range(0,len(bi),3))))
            if name=='lod':
                assert rows[-1]['coverage_equal'] and rows[-1]['leases_equal']
        comparisons[name]=rows
    for level,cell,heading in poses:
        for step in range(9):
            key=f'L{level}-c{cell}-lang0-{step:02}'
            sheet=b''.join(images[name][key][y*720:(y+1)*720]
                for y in range(160) for name in BUILDS)
            png(OUT/f'quality-L{level}-c{cell}-{step:02}.png',720,160,sheet)
    for case in cameras:
        key=case['name']
        if not key.startswith('bat-'):continue
        sheet=b''.join(images[name][key][y*720:(y+1)*720] for y in range(160) for name in BUILDS)
        png(OUT/(key+'.png'),720,160,sheet)
    (OUT/'quality.json').write_text(json.dumps(dict(status='passed',captures=results,
        comparisons=comparisons,scope='160 poses per build, native240x160 PPU. Frozen equal-pose short translations/turns plus16 original bat poses at960/1440; synthetic camera path, not an interactive playtest.'),indent=2)+'\n')
    print('480 native PPU captures, LOD coverage/leases preserved',flush=True)

if __name__=='__main__':main()
