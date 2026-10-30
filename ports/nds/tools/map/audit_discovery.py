"""N20 original x86 constructor, gap-sort and column-disclosure oracle.

Only compiler array-initialization and bounds virtual dispatch for controlled
synthetic records are replaced. Real geometry vectors execute the exported
PKR/PKE constructor instructions, including low32 products and near clipping.
No GBA code is imported. This is isolated source execution, not a DOS session.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from audit_sprites import originals,SAMPLES
from audit_geometry import NativeCellOracle,expected_quads
OUT=ROOT/'research/evidence/nds/n20'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def pin(path):return dict(path=Path(path).relative_to(ROOT).as_posix(),sha256=sha(path))

class OriginalDiscovery:
    def __init__(self,language):
        self.language=language;ru=language=='ru';self.delta=0 if ru else 0xb0
        folder=OUT/'original'/language
        manifest=json.loads((folder/'analysis-manifest.json').read_text('utf-8-sig'))
        assert manifest['read_only'] and manifest['sha256_before']==manifest['sha256_after']==SAMPLES[language][1]
        self.c=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
        for seg,address in [(('10b0' if ru else '1040'),0x50000),(('1100' if ru else '1060'),0x60000),
                            (('1190' if ru else '10d0'),0x30000)]:
            p=folder/(seg+'.bin');assert sha(p)==manifest['binary_sha256'][seg];c.mem_write(address,p.read_bytes())
        c.hook_add(UC_HOOK_CODE,self.hook);self.stopped=False
        self.geometry_segment=0x10b0 if ru else 0x1040
        self.constructor={'center':0,'edge':0x6aa if ru else 0x6b6,
            'horizontal2':0x1007 if ru else 0x102b,'horizontal1':0x15f4 if ru else 0x1624,
            'billboard':0xccb if ru else 0xce3}
        self.column=0x301 if ru else 0x128f
        self.global_tag_value=None
        if ru:
            p=folder/'1098.bin';assert sha(p)==manifest['binary_sha256']['1098'];c.mem_write(0xa0000,p.read_bytes())
    def ret(self):
        c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        for name,value in [('SP',sp+4),('CS',cs),('IP',ip)]:c.reg_write(getattr(R,'UC_X86_REG_'+name),value)
    def hook(self,c,address,size,unused):
        if address in (0x5fff0,0x6fff0):self.stopped=True;c.emu_stop()
        elif address==0x15718:self.ret()
        elif address==0xa5335:
            self.global_tag_value=struct.unpack('<H',c.mem_read(0x3f800-0x82,2))[0]
            self.stopped=True;c.emu_stop()
        elif c.reg_read(R.UC_X86_REG_CS)==self.geometry_segment:
            ip=c.reg_read(R.UC_X86_REG_IP);c.reg_write(R.UC_X86_REG_CS,0x5000);c.reg_write(R.UC_X86_REG_IP,ip)
        elif address==0x90000:
            sp=c.reg_read(R.UC_X86_REG_SP)
            ro,rs,a,aseg,b,bseg=struct.unpack('<6H',c.mem_read(0x30000+sp+4,12))
            left,right=struct.unpack('<hh',c.mem_read(rs*16+ro+12,4))
            c.mem_write(aseg*16+a,struct.pack('<h',right));c.mem_write(bseg*16+b,struct.pack('<h',left));self.ret()
    def setup(self,segment,args):
        c=self.c;self.stopped=False
        for name,value in [('SS',0x3000),('DS',0x3000),('CS',segment),('SP',0xf000),('BP',0xf800)]:
            c.reg_write(getattr(R,'UC_X86_REG_'+name),value)
        c.mem_write(0x3f000,struct.pack('<HH',0xfff0,segment)+args)
    def project(self,rec,camera,tag=0):
        c=self.c;x,y,z,sine,cosine=camera
        for off,fmt,value in [(0xaa16,'i',200),(0xaa1e,'h',159),(0xaa20,'h',79),(0xaa32,'h',8),
            (0xaa3e,'i',sine),(0xaa42,'i',cosine),(0xaa46,'i',x),(0xaa4a,'i',y),(0xaa4e,'i',z),
            (0xaa22,'i',0),(0xaa26,'i',0),(0xaa2a,'i',319),(0xaa2e,'i',159)]:
            c.mem_write(0x30000+off+self.delta,struct.pack('<'+fmt,value))
        c.mem_write(0x30000+(0x934 if self.language=='ru' else 0xa44),struct.pack('<h',4096))
        c.mem_write(0x41000,struct.pack('<3i',*rec['xyz']));c.mem_write(0x42000,bytes(128))
        c.mem_write(0x43000,struct.pack('<HH',96,192))
        args=struct.pack('<4H',0x2000,0x4000,0x1000,0x4000)
        kind=rec['kind'];flags=rec['flags']
        if kind in ('center','edge'):
            args+=struct.pack('<4h3H',rec['height'],rec['width'],rec['orientation'],flags,0x3000,0x4000,tag)
            args+=struct.pack('<h',rec.get('mirror',0)) if kind=='center' else struct.pack('<2h',rec.get('v0',0),rec.get('v1',0))
        elif kind=='horizontal2':
            args+=struct.pack('<3h5H',rec['height'],rec['width'],flags,0x3000,0x4000,0x3000,0x4000,tag)
        elif kind=='billboard':args+=struct.pack('<3h3H',rec['height'],rec['width'],flags,0x3000,0x4000,tag)
        else:args+=struct.pack('<2h3H',rec['width'],flags,0x3000,0x4000,tag)
        self.setup(0x5000,args);c.emu_start(0x50000+self.constructor[kind],0x5fff1,count=20000)
        assert self.stopped and c.reg_read(R.UC_X86_REG_SP)==0xf004
        data=bytes(c.mem_read(0x42000,128));flags=data[6];key=struct.unpack_from('<i',data,7)[0]
        if not flags&1:return [0,0,0,0,tag]
        if kind.startswith('horizontal'):
            boundoffset=0x77 if kind=='horizontal2' else 0x43
            left,right=struct.unpack_from('<ii',data,boundoffset)
        else:left,right=sorted([struct.unpack_from('<i',data,k)[0] for k in ((0xf,0x1b) if kind=='billboard' else (0x13,0x1f))])
        return [flags,key,(left+32768)%65536-32768,(right+32768)%65536-32768,tag]
    def global_tag(self,x,z):
        assert self.language=='ru';c=self.c
        c.mem_write(0x41000,struct.pack('<3H',64,1,64));c.mem_write(0x41742,struct.pack('<I',64))
        c.mem_write(0x44026,struct.pack('<iii',x,-300,z));c.mem_write(0x3f800+6,struct.pack('<HH',0x1000,0x4000))
        c.mem_write(0x3f800-0x6a,struct.pack('<HH',0x4000,0x4000))
        self.setup(0xa000,b'');c.reg_write(R.UC_X86_REG_BP,0xf800)
        c.emu_start(0xa52b7,0xafff0,count=2000)
        assert self.stopped and self.global_tag_value is not None
        return self.global_tag_value
    def records(self,records):
        c=self.c;count=len(records);assert count<=2496
        c.mem_write(0x30000+0xaa38+self.delta,struct.pack('<H',count))
        c.mem_write(0x30000+0xaa3a+self.delta,struct.pack('<HH',0,0x4000))
        for off,value in [(0xaa22,0),(0xaa2a,319)]:c.mem_write(0x30000+off+self.delta,struct.pack('<i',value))
        c.mem_write(0x32004,struct.pack('<HH',0,0x9000))
        for i,(key,left,right,cell,flags) in enumerate(records):
            address=0x70000+i*32;offset=address%16;segment=address//16
            c.mem_write(0x40000+i*4,struct.pack('<HH',offset,segment))
            data=bytearray(32);struct.pack_into('<H',data,0,0x2000)
            struct.pack_into('<HBi',data,4,cell,flags,key);struct.pack_into('<hh',data,12,left,right);c.mem_write(address,bytes(data))
        self.setup(0x6000,struct.pack('<HH',0,0x4000))
        c.emu_start(0x60000+self.column,0x6fff1,count=10000000)
        assert self.stopped and c.reg_read(R.UC_X86_REG_SP)==0xf004
        rows=[];order=[];visited=bytearray(4096)
        for i in range(count):
            offset,segment=struct.unpack('<HH',c.mem_read(0x40000+i*4,4));order.append((segment*16+offset-0x70000)//32)
            data=bytes(c.mem_read(0x70000+i*32,32));flags=data[6];key=struct.unpack_from('<i',data,7)[0]
            rows.append([flags,key])
            if flags&1:visited[records[i][3]]=1
        return order,rows,visited

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--dump',type=Path,required=True)
    parser.add_argument('--combined-only',action='store_true',help='focused producer recheck, separate report; not the complete source gate')
    parser.add_argument('--report',type=Path,help='write a new checkpoint report; keep accepted N20 evidence unchanged')
    args=parser.parse_args()
    before=originals();OUT.mkdir(parents=True,exist_ok=True)
    source_files=['ports/nds/ui/map_discovery.c','ports/nds/include/dok/map_discovery.h',
        'ports/nds/tests/map_discovery_dump.c','ports/nds/tools/map/audit_discovery.py',
        'ports/nds/render/geometry.c','ports/nds/render/scene.c','ports/nds/render/sprites.c',
        'ports/nds/core/world.c','ports/nds/core/sha256.c','ports/nds/tools/audit_geometry.py',
        'ports/nds/tools/audit_sprites.py','tools/ghidra/ExportNdsMap.java',
        'ports/nds/tools/export_map_evidence.ps1']
    source_before=[pin(ROOT/p) for p in source_files]
    executable_before=pin(args.dump.resolve())
    # Accepted N07 originals contain exact original per-cell constructor args.
    source=json.loads((ROOT/'research/evidence/nds/n07/geometry-native-report.json').read_text())
    vectors=[];native_inputs=[];pins=[]
    for level in (1,2,3):
        original=NativeCellOracle(level);path=ROOT/f'research/evidence/nds/n07/native-geometry-level{level}.json'
        assert sha(path)==source['levels'][level-1]['sha256'];pins.append(pin(path))
        for cell in json.loads(path.read_text()):
            quads=expected_quads(original,cell);qi=0
            for rec in cell['records']:
                quad=quads[qi];qi+=2 if rec['kind']=='horizontal2' else 1
                x,z=cell['x'],cell['z']
                # Nearest/behind, oblique, far and screen-edge vectors.
                for dx,dz,sine,cosine in [(0,-720,0,32768),(0,240,0,32768),(-720,-720,23170,23170),(2400,-360,32768,0)]:
                    camera=[x*480+240+dx,-300,z*480+240+dz,sine,cosine]
                    row=[quad[3],x,z,rec['flags']]+[quad[9+5*k+j] for k in range(4) for j in range(3)]+camera
                    vectors.append(row);native_inputs.append((rec,camera,z*64+x))
    actual=subprocess.check_output([args.dump],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),text=True)
    actual=[list(map(int,row.split())) for row in actual.splitlines()];assert len(actual)==len(vectors)
    report=dict(task='N20',passed=False,method=__doc__,originals_before=before,geometry_inputs=pins,languages={})
    aabb_path=ROOT/'research/evidence/nds/n07/native-geometry-aabb.json'
    assert sha(aabb_path)==source['aabb']['sha256']
    aabbs=json.loads(aabb_path.read_text())
    text=''.join(f"{r['camera'][0]} {r['camera'][2]} {r['camera'][3]}\n" for r in aabbs)
    observed=subprocess.check_output([args.dump,'--bounds'],input=text,text=True).splitlines()
    assert [list(map(int,r.split())) for r in observed]==[r['bounds'] for r in aabbs]
    report['native_bounds']=dict(cases=len(aabbs),evidence=pin(aabb_path),uses_original_overwritten_slot=True)
    rng=random.Random(20020);cases=[]
    for count in (0,1,2,3,16,31,64,128,512,2496):
        for _ in range(4):
            cases.append([[rng.choice([0,1,2,500,1000]),rng.randint(-100,350),rng.randint(-20,450),rng.randrange(4096),rng.choice([1,3,9,11,129])]
                for i in range(count)])
    cases.extend([[[2,0,319,0,3],[1,0,319,1,3]],[[1,0,319,0,3],[2,0,319,1,1]],
        [[1,0,159,0,3],[1,160,319,1,3],[2,0,319,2,1]],[[1,0,319,0,1],[2,0,319,1,1]]])
    stdin=''.join(str(len(case))+'\n'+''.join(' '.join(map(str,r))+'\n' for r in case) for case in cases)
    columns=subprocess.check_output([args.dump,'--records'],input=stdin,text=True).splitlines();assert len(columns)==len(cases)
    sprite_vectors=[]
    for depth in (0,1,120,300,600,1200,2400):
        for lateral in (-1500,-200,0,200,1500):
            for width,height in ((96,192),(480,960),(25,60),(700,480)):
                sprite_vectors.append([15120,-300,22800,0,32768,15120+lateral,-200,22800+depth,width,height,0x81000053,8])
    sprite_output=subprocess.check_output([args.dump,'--sprite'],
        input=''.join(' '.join(map(str,r))+'\n' for r in sprite_vectors),text=True).splitlines()
    sprite_actual=[list(map(int,r.split())) for r in sprite_output];assert len(sprite_actual)==len(sprite_vectors)
    bad_ids=[0,0x90000011,0x80001000,0x82000053,0xFFFFFFFF]
    bads=[sprite_vectors[-1][:10]+[n,8] for n in bad_ids]
    negative=subprocess.check_output([args.dump,'--sprite'],input=''.join(' '.join(map(str,r))+'\n' for r in bads),text=True).splitlines()
    assert all(len(r.split())==1 and int(r)!=0 for r in negative)
    report['sprite_provenance_negative_cases']=len(bads)
    for language in (() if args.combined_only else ('ru','en')):
        native=OriginalDiscovery(language)
        for i,((rec,camera,tag),actualrow) in enumerate(zip(native_inputs,actual)):
            expected=native.project(rec,camera,tag)
            if not actualrow[0]&1:actualrow=[0,0,0,0,actualrow[4]]
            assert actualrow==expected,(language,i,rec,camera,actualrow,expected)
        for case,row in zip(cases,columns):
            order,records,visited=native.records(case);values=row.split();observed=[int(n) for n in values[:-1]]
            assert observed==[v for i,(flags,key) in enumerate(records) for v in (order[i],flags,key)],(language,case,observed,order,records)
            assert values[-1]==''.join(str(n) for n in visited)
        for vector,row in zip(sprite_vectors,sprite_actual):
            x,y,z,sine,cosine,px,py,pz,width,height,source_id,flags=vector
            rec=dict(kind='billboard',xyz=[px,py,pz],width=width,height=height,flags=flags)
            expected=native.project(rec,[x,y,z,sine,cosine],source_id&4095)
            assert row[0]==0 and row[1:]==expected,(language,vector,row,expected)
        report['languages'][language]=dict(constructor_vectors=len(vectors),column_cases=len(cases),column_records=sum(map(len,cases)),
            sprite_constructor_vectors=len(sprite_vectors),
            native_manifest=pin(OUT/'original'/language/'analysis-manifest.json'))
        print(language,'constructors and columns passed',flush=True)
    # Combined producer proof: actual C update versus fresh original per-cell
    # builder branches -> original constructors -> original Shell+column pass.
    # Static source records are intentionally isolated from sprite callbacks;
    # live sprites are separately covered by accepted N12 and constructor tests.
    combined=[];native=OriginalDiscovery('ru')
    for level in (1,2,3):
        original=NativeCellOracle(level)
        hero=next(o for o in original.world['objects'] if o['class_id']==1)
        poses=[[hero['x'],hero['y'],hero['z'],a] for a in (0,45,90,112,135,180,225,270,315,360)]
        poses.extend([[23760,-300,6480,a] for a in (0,45,90,135,180,225,270,315)])
        levelpath=ROOT/f'build/nds/n07/geometry-level{level}.cp866'
        bindingpath=ROOT/f'build/nds/n07/geometry-bindings{level}.bin'
        assert levelpath.read_bytes()==original.source,'Host LEVEL fixture differs from original resource'
        output=subprocess.check_output([args.dump,'--world',levelpath,bindingpath],
            input=''.join(' '.join(map(str,p))+'\n' for p in poses),text=True).splitlines()
        extra=subprocess.check_output([args.dump,'--world-extra',levelpath,bindingpath],
            input=''.join(' '.join(map(str,p))+'\n' for p in poses),text=True).splitlines()
        assert output==extra,('Projectile/synthetic ID modified disclosure',level)
        assert len(output)==len(poses)
        for pose,observed in zip(poses,output):
            x,y,z,angle=pose;bounds=original.aabb(x,z,angle)['bounds'];records=[]
            camera=[x,y,z,int(math.sin(angle*3.14159265359/180.0)*32768),int(math.cos(angle*3.14159265359/180.0)*32768)]
            for cz in range(bounds[1],bounds[3]+1):
                for cx in range(bounds[0],bounds[2]+1):
                    for rec in original.cell(cx,cz):
                        flags,key,left,right,tag=native.project(rec,camera,cz*64+cx)
                        if flags&1:records.append([key,left,right,tag,flags])
            order,post,visited=native.records(records)
            values=observed.split();count=int(values[0]);assert count==len(records),(level,pose,count,len(records))
            rows=[list(map(int,values[1+6*i:7+6*i])) for i in range(count)]
            expected=[[post[i][0],post[i][1],r[1],r[2],r[3],order[i]] for i,r in enumerate(records)]
            assert rows==expected,(level,pose,next((i,a,b) for i,(a,b) in enumerate(zip(rows,expected)) if a!=b))
            assert values[-1]==''.join(str(n) for n in visited),(level,pose,'visited mismatch')
            combined.append(dict(level=level,camera=pose,bounds=bounds,records=count,
                visited_cells=sum(visited),visited_sha256=hashlib.sha256(visited).hexdigest()))
        print('combined level',level,'passed',flush=True)
    report['combined_producer']=dict(cases=len(combined),rows=combined,
        scope='fresh original per-cell static builder + native constructor + native sort/column versus actual C update; no live DOS session')
    native=OriginalDiscovery('ru');tag=native.global_tag(15120,23280);assert tag==4095
    projectile=dict(kind='billboard',xyz=[15120,-300,23280],width=96,height=96,flags=0x1008)
    flags,key,left,right,tag=native.project(projectile,[15120,-300,22800,0,32768],tag)
    assert flags&1
    _,_,visited=native.records([[key,left,right,tag,flags]])
    assert sum(visited)==1 and visited[4095]==1
    report['global_projectile_corner_fix']=dict(native_entry='PKR1098:52B7..5335',
        native_spawn_sets_flag='PKR1068:2681',actor_pose=[15120,-300,23280],
        native_tag=tag,native_record_visited_sha256=hashlib.sha256(visited).hexdigest(),
        native_visited_indices=[4095],nds_negative_update_cases=len(combined),
        nds_policy='Ignore sprite IDs without N12 world-cell provenance; source projectile/synthetic pool IDs do not disclose cells',
        adaptation=True)
    report['originals_after']=originals();assert report['originals_after']==before
    report['source_pins']=[pin(ROOT/p) for p in source_files]
    assert report['source_pins']==source_before,'Source changed during native/C gate'
    assert pin(args.dump.resolve())==executable_before,'Host executable changed during gate'
    report['executable']=executable_before
    report['source_pins_before_after_equal']=True
    report['complete_source_gate']=not args.combined_only
    report['passed']=True
    report_path=args.report or OUT/('source-discovery-combined.json' if args.combined_only else 'source-discovery.json')
    report_path.parent.mkdir(parents=True,exist_ok=True)
    report_path.write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,constructor_vectors=2*len(vectors),column_cases=2*len(cases))))
if __name__=='__main__':main()
