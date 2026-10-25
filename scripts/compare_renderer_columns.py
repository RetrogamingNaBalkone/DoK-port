"""Render target-size 240/120-column comparisons from the scalar oracle.

The 120-column ARM build is separately checked against this exact even-column
duplication by verify_renderer_early.py --half-columns. No claim of perceptual
equivalence follows from pixel counts. These images omit the caller-owned HUD.
"""
import argparse
import json
from pathlib import Path
import subprocess
from verify_gba import png

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=Path,default=Path('build/g06a-column-comparison'))
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
cases=[('L1-start',1,[15120,-300,22800,90]),
       ('L1-diagonal',1,[15120,-300,22800,45]),
       ('L1-side',1,[15120,-300,22800,0]),
       ('L1-near',1,[15120,-300,22801,90]),
       ('L3-heavy',3,[16560,-300,27600,210]),
       ('L3-diagonal',3,[16560,-300,27600,45])]
results=[];sheet=bytearray(488*len(cases)*160*3)
for number,(name,level,camera) in enumerate(cases):
    raw=a.out/(name+'.idx');pal=a.out/(name+'.pal')
    subprocess.run(['build/native/dok-render-reference.exe','build/gba/assets/assets.bin',str(level),'ru',str(raw),str(pal),*map(str,camera)],check=True,stdout=subprocess.DEVNULL)
    full=raw.read_bytes();palette=pal.read_bytes()
    colors=[bytes((palette[i*3+j]>>1)*255//31 for j in range(3)) for i in range(256)]
    half=bytes(value for value in full[::2] for _ in range(2))
    rgb=[b''.join(colors[v] for v in frame) for frame in (full,half)]
    changed=sum(colors[full[i]]!=colors[half[i]] for i in range(34560))
    delta=sum(abs(x-y) for x,y in zip(rgb[0][:34560*3],rgb[1][:34560*3]))/(34560*3)
    for variant,pixels in zip(('240','120'),rgb):
        png(a.out/f'{name}-{variant}.png',240,160,pixels)
    pair=bytearray(488*160*3)
    for y in range(160):
        for column in range(2):
            at=(y*488+column*248)*3
            pair[at:at+720]=rgb[column][y*720:(y+1)*720]
    png(a.out/f'{name}-pair.png',488,160,bytes(pair))
    sheet[number*160*488*3:(number+1)*160*488*3]=pair
    results.append(dict(name=name,level=level,camera=camera,changed_rgb_pixels=changed,
                        scene_pixels=34560,changed_percent=changed*100/34560,mean_rgb_absolute_error=delta))
png(a.out/'comparison.png',488,len(cases)*160,bytes(sheet))
(a.out/'comparison.json').write_text(json.dumps(dict(left=240,right=120,scale='1:1, target RGB555 palette',hud='omitted',cases=results),indent=2)+'\n')
print(json.dumps(results,indent=2))
