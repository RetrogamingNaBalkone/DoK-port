"""Check LIGHT band interiors against original screen-axis depth arithmetic.

The raw source-cell geometry is the independently native-checked N07 model.
This checks subdivision/welding, not a second implementation of its clipping.
Scalar LIGHT is executed in the PKR instructions for each admitted sample.
"""
from __future__ import annotations
import argparse,json,math,re,struct,subprocess
from fractions import Fraction as F
from pathlib import Path
from audit_effects import Original,ROOT,OUT
def wrap(v):return (v+2**31)%2**32-2**31
def trunc(a,b):return (abs(a)//abs(b))*(-1 if (a<0)!=(b<0) else 1)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dump',type=Path,required=True);args=p.parse_args()
    trig=(ROOT/'build/nds/n07/generated/render_trig.inc').read_text()
    tables=[list(map(int,re.findall(r'-?\d+',b))) for b in re.findall(r'\{(.*?)\}',trig,re.S)]
    sine,cosine=tables;assert len(sine)==len(cosine)==361
    native=Original('ru');checks=guarded=behind=unmatched=outer=0;rows=[0]*16;maximum_merges=0
    heavy=json.loads((OUT/'host-check.json').read_text('utf8'))['levels'];details=[]
    for level in (1,2,3):
        x,z,_=heavy[level-1]['heavy'];poses=[(x,z,h) for h in range(0,360,45)]
        starts=[(15120,22800,90),(20880,3120,90),(17520,28560,90)];poses.append(starts[level-1])
        data=subprocess.check_output([args.dump,ROOT/'build/nds/assets/dok-assets.ndspak',str(level),'--poses'],input=''.join(f'{x} {z} {h}\n' for x,z,h in poses),text=True)
        for pose,after in zip(poses,map(json.loads,data.splitlines())):
            cx,cz,h=pose;s,c=sine[h],cosine[h]
            raw=json.loads(subprocess.check_output([ROOT/'build/nds/n13/render_dump.exe',ROOT/'build/nds/assets/dok-assets.ndspak',str(level),'--scene',str(cx),'-300',str(cz),str(h)]))['scene']
            maximum_merges=max(maximum_merges,raw['counts']['merged_quads']);assert not raw['counts']['merged_quads']
            sources={}
            for cell in raw['cells']:
                for q in cell['quads']:
                    vs=[[a+b for a,b in zip(cell['origin'],v[:3])] for v in q['vertices']]
                    horizontal=len({v[1] for v in vs})==1;points=[];invalid=False
                    for vx,vy,vz in vs:
                        dx,dz=vx-cx,vz-cz;depth=wrap(dz*c-dx*s)//32768
                        if horizontal and depth<0:depth//=32
                        if depth<=-200:invalid=True;break
                        lateral=vy+300 if horizontal else wrap(dx*c+dz*s)//32768
                        axis=(wrap(lateral*trunc(6553600,depth+200))//32768)+(79 if horizontal else 159)
                        points.append((axis,abs(depth)))
                    if invalid:sources[q['source_surface_id']]=None;continue
                    left=min(points,key=lambda v:v[0]);right=max(points,key=lambda v:v[0]);sources[q['source_surface_id']]=(horizontal,left,right)
            for cell in after['cells']:
                for q in cell['quads']:
                    if q['id']&0x80000000:continue # N12 billboard projection has its own native oracle.
                    if q['id'] not in sources:unmatched+=1;continue
                    source=sources[q['id']]
                    if source is None:behind+=1;continue
                    horizontal,(a0,d0),(a1,d1)=source
                    xyz=[F(sum(v[k]+cell['origin'][k] for v in q['vertices']),4) for k in range(3)]
                    dx,dz=xyz[0]-cx,xyz[2]-cz;eye=dz*c-dx*s+200*32768
                    if eye<=0:behind+=1;continue
                    lateral=(xyz[1]+300)*32768 if horizontal else dx*c+dz*s
                    axis=F(79 if horizontal else 159)+200*lateral/eye
                    if a0==a1 or axis<=a0 or axis>=a1:outer+=1;continue
                    if a1!=a0 and d1!=d0:
                        cuts=[F(a0)+F((1920+120*k-d0)*(a1-a0),d1-d0) for k in range(1,16)]
                        if any(abs(axis-cut)<F(1,4) for cut in cuts):guarded+=1;continue
                    depth=F(d0)+(axis-a0)*F(d1-d0,a1-a0) if a1!=a0 else F(d0)
                    expected=min(native.light(wrap(int(depth*65536)),1920),15)
                    assert q['row']==expected,(level,pose,q['id'],q['row'],expected,float(axis))
                    checks+=1;rows[expected]+=1
            details.append(dict(level=level,pose=pose,quads=after['quads']))
    assert checks>1000 and not unmatched and maximum_merges==0
    report=dict(passed=True,checked_band_interiors=checks,rows=rows,guard_native_pixels=.25,
        guarded_band_boundaries=guarded,source_outer_quantization=outer,native_invalid_denominator_samples=behind,unmatched_source_ids=unmatched,
        merged_original_cells=maximum_merges,poses=details,
        scope='Native PKR scalar LIGHT on unmerged N07 native-checked source cells; source projector arithmetic reviewed against constructor ASM. This is not a full DOS raster equality claim.')
    (OUT/'light-geometry-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(f'N13 LIGHT partition: {checks} native band interiors in {len(details)} poses PASS')
if __name__=='__main__':main()
