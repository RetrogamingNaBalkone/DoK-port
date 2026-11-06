"""Compare frozen RGBA captures from the original seven-mode SP-FILTERS bench."""
import argparse
import hashlib
import json
from pathlib import Path

def difference(a,b):
    assert len(a)==len(b) and len(a)%4==0
    histogram=[0]*256;total=0
    for i in range(0,len(a),4):
        ds=[abs(a[i+c]-b[i+c]) for c in range(3)]
        histogram[max(ds)]+=1;total+=sum(ds)
    pixels=len(a)//4
    return dict(mean_channel_delta=total/(pixels*3),pixels_delta_gt8_pct=100*sum(histogram[9:])/pixels,
                pixels_delta_gt24_pct=100*sum(histogram[25:])/pixels)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('captures',type=Path)
    parser.add_argument('out',type=Path)
    args=parser.parse_args();rows=[]
    pairs=[('nearest','linear'),('linear','trilinear'),('linear','aniso8'),('mipmaps','trilinear'),('trilinear','aniso8'),('aniso2','aniso4'),('aniso4','aniso8')]
    for name in sorted((args.captures/'nearest').glob('*.rgba')):
        data={key:(args.captures/key/name.name).read_bytes() for key in {s for pair in pairs for s in pair}}
        rows.append(dict(scene=name.stem,sha256={key:hashlib.sha256(value).hexdigest() for key,value in data.items()},
                         pairs={a+'/'+b:difference(data[a],data[b]) for a,b in pairs}))
    report=dict(captures=str(args.captures),note='Pixel deltas are measurements, not perceptual quality scores.',checks=rows)
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    for a,b in pairs:
        values=[r['pairs'][a+'/'+b] for r in rows]
        print(a+'/'+b,'mean delta %.3f; >24 pixel range %.3f..%.3f%%'%(sum(v['mean_channel_delta'] for v in values)/len(values),min(v['pixels_delta_gt24_pct'] for v in values),max(v['pixels_delta_gt24_pct'] for v in values)))

if __name__=='__main__':main()
