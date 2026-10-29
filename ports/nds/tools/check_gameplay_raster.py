"""Pixel-exact paired GPU water raster, all18 phases in two source views."""
import hashlib,json,struct,subprocess,sys,zlib
from pathlib import Path
from check_gameplay_performance import ROOT,OUT,EVIDENCE,artifact
def png_scene(path):
    b=Path(path).read_bytes();assert b[:8]==b'\x89PNG\r\n\x1a\n';i=8;data=b''
    while i<len(b):
        n=struct.unpack_from('>I',b,i)[0];tag=b[i+4:i+8];payload=b[i+8:i+8+n]
        assert zlib.crc32(tag+payload)&0xffffffff==struct.unpack_from('>I',b,i+8+n)[0]
        if tag==b'IHDR':assert struct.unpack('>IIBBBBB',payload)==(256,192,8,2,0,0,0)
        if tag==b'IDAT':data+=payload
        i+=n+12
    raw=zlib.decompress(data);assert len(raw)==192*769 and all(raw[y*769]==0 for y in range(192))
    return b''.join(raw[y*769+1:(y+1)*769] for y in range(154))
def main():
    old=(OUT/'scene.dat').read_bytes();n=struct.unpack_from('<I',old,12)[0];records=[]
    for heading in (0,90):
        for phase in range(18):
            first=not records
            records.append(struct.pack('<16I',len(records)+1,2,2160,(-300)&0xffffffff,2160,heading,0,0,0,2 if first else 1,0,28,17 if first else phase,4,0,0))
    body=b''.join(records)+old[64+n*64:];packet=b'N07SCN1\0'+struct.pack('<6I',1,len(records),64,10240,64+len(body),0)+hashlib.sha256(body).digest()+body
    (OUT/'water-raster.dat').write_bytes(packet)
    labels=('before-release-ru','final-release-ru')
    for label in labels:
        target=OUT/label
        with (target/'water-raster.log').open('w',encoding='utf8') as log:
            subprocess.run([sys.executable,'-B','-X','utf8',str(ROOT/'ports/nds/tools/run_gameplay_emulator.py'),'--target',str(target),'--packet',str(OUT/'water-raster.dat'),'--output-subdir','water-raster'],stdout=log,stderr=subprocess.STDOUT,check=True)
        assert json.loads((target/'water-raster/result.json').read_text())['passed']
    rows=[]
    for i in range(1,37):
        a=OUT/labels[0]/f'water-raster/case-{i:02}-top.png';b=OUT/labels[1]/f'water-raster/case-{i:02}-top.png'
        x,y=png_scene(a),png_scene(b);assert x==y,(i,sum(u!=v for u,v in zip(x,y)))
        rows.append(dict(heading=0 if i<=18 else 90,phase=(i-1)%18,scene_sha256=hashlib.sha256(x).hexdigest(),before=artifact(a),after=artifact(b)))
    report=dict(task='N17P',passed=True,packet=artifact(OUT/'water-raster.dat'),gpu_pairs=36,phase_count=18,rows=rows,scope='All18 source phases, two GPU views, exact256x154 RGB. CPU tests cover allfour cyclic directions864 actual texture pairs. No resampling or hardware128 wrap.')
    (EVIDENCE/'water-raster.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print('36 GPU raster pairs exact')
if __name__=='__main__':main()
