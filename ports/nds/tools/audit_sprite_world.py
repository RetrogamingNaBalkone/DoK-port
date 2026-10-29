"""N12 execute original RU world-builder billboard call sites for all cells.

Controlled getters return the original static Sprite. Actor X/Z are accepted
N05 seeds; no AI or DOS full-session claim. Native builder determines classes,
cell centres, floor anchoring, IMUL16 scales and constructor flags/tag.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
from audit_geometry import NativeCellOracle, source_hashes
from audit_sprites import Original, OUT, pin, resources

class World(NativeCellOracle):
    def __init__(self,level):
        super().__init__(level)
        for seed in self.world['objects']:
            glyph=self.base['objects'][seed['definition_index']]['glyph'];p=0x90000+glyph*256
            self.cpu.mem_write(p+0x26,struct.pack('<iii',seed['x'],seed['y'],seed['z']))
        for o in self.base['objects']:
            if o['class_id'] not in (3,6,9,10) or not(o['class_id']==9 or o['flags']&0x0800):continue
            begin,n=o['animation'];name=self.source[begin:begin+n]
            index,payload=self.resources[name];count=struct.unpack_from('<H',payload)[0]
            image=payload[8+count*10:];width,height=struct.unpack_from('<HH',image)
            offset=0x3000+o['glyph']*16
            self.cpu.mem_write(0xa0000+offset,struct.pack('<HH',width,height))
            self.images[name]=(offset,width,height,index)
            self.image_names[offset]=name+b'#frame0'
            self.bindings[o['glyph']][8]=offset
            self.cpu.mem_write(0x90000+o['glyph']*256+0x44,struct.pack('<HH',offset,0xa000))
    def hook(self,c,address,size,unused):
        if address==0x54f7b:return # include MMap billboard pass
        if address==0x117cb:
            from unicorn import x86_const as R
            sp=c.reg_read(R.UC_X86_REG_SP);data=bytes(c.mem_read(0x30000+sp+4,20))
            off,seg=struct.unpack_from('<HH',data,4);xyz=list(struct.unpack('<iii',c.mem_read(seg*16+off,12)))
            height,width,flags,image,segment,tag=struct.unpack_from('<2h4H',data,8)
            assert segment==0xa000
            ip=struct.unpack('<H',c.mem_read(0x30000+sp,2))[0]
            plane=1 if ip<0x4f7b else 0
            self.records.append(dict(xyz=xyz,height=height,width=width,flags=flags,image=self.image_names[image].hex(),tag=tag,plane=plane))
            self.ret(0,0xb000);return
        super().hook(c,address,size,unused)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--dump',type=Path,required=True);args=parser.parse_args()
    before=source_hashes();report=dict(task='N12',passed=False,originals_before=before,levels=[])
    native_projection=Original('ru')
    for level in (1,2,3):
        world=World(level);records=[]
        for z in range(64):
            for x in range(64):
                for r in world.cell(x,z):
                    if 'plane' in r:records.append(dict(cell=[x,z],**r))
        # Original tag uses the original stride64. Native static widths retained;
        # NDS profile's only scale change is already7 for A0 in these inputs.
        poses=[]
        for z in range(0,64,4):
            for x in range(0,64,4):
                glyph=bytes.fromhex(world.world['maps'][1])[z*64+x]
                o=next(o for o in world.base['objects'] if o['glyph']==glyph)
                if o['class_id']==2:continue
                for h in range(0,360,45):poses.append([x*480+240,z*480+240,h])
        actual=[json.loads(line) for line in subprocess.check_output([args.dump,Path('build/nds/assets/dok-assets.ndspak'),str(level),'--poses'],
            input=''.join(' '.join(map(str,p))+'\n' for p in poses),text=True).splitlines()]
        checks=0
        # Get exact N07 control points from original AABB instructions. The NDS
        # conservative bug fix retains min/max over all four, documented in N07.
        for pose,case in zip(poses,actual):
            x,z,h=pose;aabb=world.aabb(x,z,h);points=aabb['points']
            bounds=[max(0,min(p[0] for p in points)),max(0,min(p[1] for p in points)),
                    min(63,max(p[0] for p in points)),min(63,max(p[1] for p in points))]
            sine=math.trunc(math.sin(h*3.14159265359/180)*32768);cosine=math.trunc(math.cos(h*3.14159265359/180)*32768)
            expected=[]
            for r in records:
                cx,cz=r['cell']
                if not(bounds[0]<=cx<=bounds[2] and bounds[1]<=cz<=bounds[3]):continue
                px,py,pz=r['xyz']
                visible,_,_=native_projection.project([x,-300,z,sine,cosine,px,py,pz,r['width'],r['height']])
                if visible:
                    expected.append([0x80000000|(r['plane']<<24)|r['tag'],px,py,pz,r['width'],r['height']])
            got=[[p[0],*p[2:7]] for p in case['sprites']]
            assert sorted(got)==sorted(expected),(level,pose,got,expected)
            checks+=len(expected)+1
        report['levels'].append(dict(level=level,native_billboards=len(records),poses=len(poses),checks=checks))
        (OUT/f'native-billboards-level{level}.json').write_text(json.dumps(records,indent=2)+'\n','utf8')
        print(report['levels'][-1],flush=True)
    report['originals_after']=source_hashes();assert before==report['originals_after'];report['passed']=True
    (OUT/'world-original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
if __name__=='__main__':main()
