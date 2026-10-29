"""N12 isolated original PKR/PKE billboard/animation instructions and RES oracle.

No GBA engine or test module is imported. Original far calls are relocated to
read-only Ghidra segment exports. Only array construction/scheduler services
are controlled; original arithmetic, frame iteration and signed intervals run.
"""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/nds/n12'
SAMPLES={'ru':('originals/pkr/RUS/PKR.EXE','12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'),
         'en':('originals/pkr/ENG/PKE.EXE','cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b')}
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def pin(path):return dict(path=Path(path).relative_to(ROOT).as_posix(),sha256=sha(path))
def originals():
    result={k:sha(ROOT/v[0]) for k,v in SAMPLES.items()}
    assert result=={k:v[1] for k,v in SAMPLES.items()}
    return result
def resources(language):
    path=ROOT/('originals/pkr/RUS/PKR.RES' if language=='ru' else 'originals/pkr/ENG/PKE.RES')
    data=path.read_bytes()
    assert sha(path)==('af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816' if language=='ru' else '865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458')
    return [data[o:o+n] for n,o in (struct.unpack_from('<II',data,18+i*30) for i in range(146))]

class Original:
    def __init__(self,language):
        ru=language=='ru';self.language=language
        self.delta=0 if ru else 0xb0
        self.geometry=0xccb if ru else 0xce3
        self.animation=0x1050 if ru else 0x1018
        self.next=0x54 if ru else 0xf62
        self.clamped=0xdf if ru else 0xfed
        self.base=0x2f5 if ru else 0x7cf
        self.monster=0x36b1 if ru else 0x3e14
        self.parent=0x9c5 if ru else 0xec9
        self.clock_init=(0x1130*16+0x1f5) if ru else (0x1078*16+0x188b)
        self.step=0x421 if ru else 0x901
        self.scheduler=0x1038 if ru else 0x1010
        self.owner_init=self.scheduler*16+(0 if ru else 0x13aa)
        self.interval=0x265 if ru else 0x161b
        # PKE cancel-current is1010:15CA, distinct from clear15EA.
        self.cancel=0x214 if ru else 0x15ca
        self.folder=OUT/'original'/language
        manifest=json.loads((self.folder/'analysis-manifest.json').read_text('utf-8-sig'))
        assert manifest['read_only'] and manifest['sha256_before']==manifest['sha256_after']==SAMPLES[language][1]
        assert manifest['script_sha256']==sha(ROOT/'tools/ghidra/ExportNdsSprites.java')
        self.cpu=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
        for role,seg,address in [('geometry','10b0' if ru else '1040',0x50000),
                                 ('animation','1050' if ru else '1018',0x60000),
                                 ('actor','1068' if ru else '1020',0x70000)]:
            p=self.folder/(seg+'.bin');assert sha(p)==manifest['binary_sha256'][seg]
            c.mem_write(address,p.read_bytes())
        c.hook_add(UC_HOOK_CODE,self.hook)
        self.stopped=False;self.scheduled=[]
    def ret(self):
        c=self.cpu;sp=c.reg_read(R.UC_X86_REG_SP)
        ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        for r,v in [('SP',sp+4),('CS',cs),('IP',ip)]:c.reg_write(getattr(R,'UC_X86_REG_'+r),v)
    def hook(self,c,address,size,unused):
        if address in (0x5fff0,0x6fff0,0x7fff0):self.stopped=True;c.emu_stop()
        elif address==0x15718:self.ret() # compiler array constructor only
        elif address==self.animation*16+self.next:
            c.reg_write(R.UC_X86_REG_CS,0x6000);c.reg_write(R.UC_X86_REG_IP,self.next)
        elif address==self.animation*16+self.clamped:
            c.reg_write(R.UC_X86_REG_CS,0x6000);c.reg_write(R.UC_X86_REG_IP,self.clamped)
        elif address in (self.owner_init,self.clock_init):self.ret()
        elif getattr(self,'constructing_monster',False) and address==0x70000+self.parent:self.ret()
        elif address==self.scheduler*16+self.interval:
            sp=c.reg_read(R.UC_X86_REG_SP)
            self.scheduled.append(struct.unpack('<i',c.mem_read(0x30000+sp+8,4))[0]);self.ret()
        elif address==self.scheduler*16+self.cancel:self.scheduled.append(None);self.ret()
    def setup(self,segment,args):
        c=self.cpu;self.stopped=False;self.scheduled=[]
        for r,v in [('SS',0x3000),('DS',0x3000),('CS',segment),('SP',0xf000),('BP',0xf800)]:
            c.reg_write(getattr(R,'UC_X86_REG_'+r),v)
        c.mem_write(0x3f000,struct.pack('<HH',0xfff0,segment)+args)
    def project(self,row):
        x,y,z,sine,cosine,px,py,pz,width,height=row;c=self.cpu
        self.setup(0x5000,struct.pack('<4H2h4H',0x2000,0x4000,0x1000,0x4000,height,width,8,0x100,0x4000,0))
        for offset,fmt,value in [(0xaa16,'i',200),(0xaa1e,'h',159),(0xaa20,'h',79),(0xaa32,'h',8),
            (0xaa3e,'i',sine),(0xaa42,'i',cosine),(0xaa46,'i',x),(0xaa4a,'i',y),(0xaa4e,'i',z),
            (0xaa22,'i',0),(0xaa26,'i',0),(0xaa2a,'i',319),(0xaa2e,'i',159)]:
            c.mem_write(0x30000+offset+self.delta,struct.pack('<'+fmt,value))
        c.mem_write(0x30000+(0x934 if self.language=='ru' else 0xa44),struct.pack('<h',4096))
        c.mem_write(0x41000,struct.pack('<iii',px,py,pz));c.mem_write(0x42000,bytes(128))
        c.emu_start(0x50000+self.geometry,0x5fff1,count=10000)
        assert self.stopped and c.reg_read(R.UC_X86_REG_SP)==0xf004
        b=bytes(c.mem_read(0x42000,128));visible=bool(b[6]&1)
        vertices=[list(struct.unpack_from('<iii',b,0xf+12*k)) for k in range(4)]
        return visible,struct.unpack_from('<i',b,7)[0],vertices
    def animation_callbacks(self,payload):
        count=struct.unpack_from('<h',payload)[0];c=self.cpu
        # Runtime loader's pointer fixups, without using serialized addresses.
        c.mem_write(0x41000,struct.pack('<hhHH',count,0,0x1100,0x4000))
        for i in range(count):
            c.mem_write(0x41100+10*i,payload[8+10*i:14+10*i]+struct.pack('<HH',0x2000+i*32,0x4000))
        c.mem_write(0x44000,bytes(128))
        c.mem_write(0x44024,struct.pack('<h',count+2))
        c.mem_write(0x44048,struct.pack('<HH',0x1000,0x4000))
        c.mem_write(0x44050,struct.pack('<h',-1))
        rows=[]
        for _ in range(count+3):
            self.setup(0x7000,struct.pack('<HH',0x4000,0x4000))
            c.emu_start(0x70000+self.step,0x7fff1,count=10000)
            assert self.stopped and len(self.scheduled)==1
            rows.append(dict(cursor=struct.unpack('<h',c.mem_read(0x44050,2))[0],interval=self.scheduled[0],
                parameters=list(struct.unpack('<2h',c.mem_read(0x44020,4)))))
        return rows
    def constructors(self):
        c=self.cpu
        for flags in (0,8,0x800,0x808,0x818):
            definition=bytearray(32)
            struct.pack_into('<H',definition,0,3)
            struct.pack_into('<HH',definition,7,0x7000,0x4000)
            struct.pack_into('<HH',definition,11,0x1000,0x4000)
            struct.pack_into('<H',definition,19,flags)
            c.mem_write(0x45000,bytes(definition));c.mem_write(0x44000,bytes(256))
            self.setup(0x7000,struct.pack('<4H',0x4000,0x4000,0x5000,0x4000))
            c.emu_start(0x70000+self.base,0x7fff1,count=10000)
            assert self.stopped
            pointer=struct.unpack('<HH',c.mem_read(0x44044,4))
            assert pointer==((0x2000,0x4000) if flags&0x800 else (0x7000,0x4000))
        c.mem_write(0x44048,struct.pack('<HH',0x1000,0x4000))
        self.setup(0x7000,struct.pack('<5H',0x4000,0x4000,0x5000,0x4000,0))
        self.constructing_monster=True
        c.emu_start(0x70000+self.monster,0x7fff1,count=10000)
        self.constructing_monster=False
        assert self.stopped and struct.unpack('<HH',c.mem_read(0x44044,4))==(0x2000,0x4000)
        return 6

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--dump',type=Path,required=True);args=parser.parse_args()
    before=originals();OUT.mkdir(parents=True,exist_ok=True)
    vectors=[]
    for depth,lateral,width,height in itertools.product((0,1,120,300,600,1200,2400,6000),(-200,-40,0,40,200),(-3,25,84,480,700),(60,480,700)):
        vectors.append([15120,-300,22800,32768,0,15120-depth,-250,22800+lateral,width,height])
    for sine,cosine in ((0,32768),(23170,23170),(-23170,23170),(-32768,0),(23170,-23170)):
        for x,z in ((0,2000),(1000,2500),(-1000,600),(32700,32700)):
            vectors.append([0,-300,0,sine,cosine,x,-70,z,700,480])
    output=subprocess.check_output([args.dump,'--project'],input=''.join(' '.join(map(str,r))+'\n' for r in vectors),text=True)
    actual=[list(map(int,line.split())) for line in output.splitlines()];assert len(actual)==len(vectors)
    report=dict(task='N12',passed=False,originals_before=before,languages={},whole_DOS_session=False)
    for language in ('ru','en'):
        native=Original(language);count=0
        for row,r in zip(vectors,actual):
            visible,key,vertices=native.project(row)
            assert bool(r[7])==visible and r[6]==key,(language,row,r,visible,key)
            if visible:
                assert vertices==[[r[2],r[4],r[1]],[r[3],r[4],r[1]],[r[3],r[5],r[1]],[r[2],r[5],r[1]]],(language,row,r,vertices)
            count+=1
        samples=resources(language);sequences=[]
        catalog=json.loads((ROOT/f'analysis/originals/animations-{language}.json').read_text('utf8'))
        rows=catalog['entries']
        assert isinstance(rows,list)
        for item in rows:
            index=item.get('index',item.get('resource_index'));payload=samples[index]
            n=struct.unpack_from('<h',payload)[0]
            callbacks=native.animation_callbacks(payload)
            for k,value in enumerate(callbacks):
                if k==n+2:assert value['interval'] is None;continue
                f=k%n;interval,a,b=struct.unpack_from('<3h',payload,8+f*10)
                assert value==dict(cursor=f,interval=interval,parameters=[a,b]),(language,index,k,value)
            sequences.append(dict(index=index,callbacks=len(callbacks),frames=n))
        report['languages'][language]=dict(billboard_vectors=count,animation_sequences=sequences,
            initial_appearance_constructor_cases=native.constructors(),
            evidence=pin(native.folder/'analysis-manifest.json'))
    report['originals_after']=originals();assert report['originals_after']==before;report['passed']=True
    (OUT/'original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,billboard_vectors=2*len(vectors),languages=2)))
if __name__=='__main__':main()
