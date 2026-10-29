"""N15 original weapon callbacks/slot scan/spawn/decay with controlled services.
No GBA implementation imports. Full DOS/event closure execution is not claimed.
"""
import hashlib,json,random,struct,subprocess
from pathlib import Path
from audit_items import Original as ItemOriginal,ROOT
from audit_sprites import originals
from unicorn import x86_const as R
def word(v):return (v+32768)%65536-32768
class Original(ItemOriginal):
    def __init__(self,lang):
        super().__init__(lang);self.world=0x1098 if self.ru else 0x1030
        self.cancelled=0;self.reclaimed=[];self.flight_mode=False
    def ret(self):
        c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        for reg,v in ((R.UC_X86_REG_SP,sp+4),(R.UC_X86_REG_CS,cs),(R.UC_X86_REG_IP,ip)):c.reg_write(reg,v)
    def run(self,entry,args):
        c=self.c
        for reg,v in ((R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,v)
        c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args)
        c.emu_start(self.segment*16+entry,0x6fff0,count=30000)
        assert c.reg_read(R.UC_X86_REG_SP)==0xf004 and c.reg_read(R.UC_X86_REG_CS)==0x6000
        return c.reg_read(R.UC_X86_REG_AX)
    def hook(self,c,address,size,context):
        cs=c.reg_read(R.UC_X86_REG_CS);ip=c.reg_read(R.UC_X86_REG_IP)
        if self.flight_mode:
            sp=c.reg_read(R.UC_X86_REG_SP)
            if cs==self.world and ip==(0x309 if self.ru else 0x3ed):
                # Controlled empty lookup: only this world service is replaced.
                # The callback's own scaling/IDIV/decay/animation/clock executes.
                args=struct.unpack('<5H',c.mem_read(0x30000+sp+4,10));base=args[3]*16+args[2]
                x,z,step=struct.unpack('<i',c.mem_read(base,4))[0],struct.unpack('<i',c.mem_read(base+8,4))[0],struct.unpack('<i',c.mem_read(base+0x16,4))[0]
                c.mem_write(base+8,struct.pack('<i',z-step));c.reg_write(R.UC_X86_REG_AX,0);c.reg_write(R.UC_X86_REG_DX,0x6000);self.ret();return
            if cs==0x6000 and ip==0x100:self.ret();return # controlled base event pass-through
            if cs==(0x1130 if self.ru else 0x1078) and ip==(0x26f if self.ru else 0x1905):
                c.reg_write(R.UC_X86_REG_AX,416);c.reg_write(R.UC_X86_REG_DX,0);self.ret();return
            if cs==(0x1050 if self.ru else 0x1018) and ip==(0x54 if self.ru else 0xf62):
                args=struct.unpack('<10H',c.mem_read(0x30000+sp+4,20));counter=args[9]*16+args[8]
                cur=struct.unpack('<h',c.mem_read(counter,2))[0]+1;c.mem_write(counter,struct.pack('<h',-1 if cur>=1 else cur))
                c.mem_write(args[7]*16+args[6],struct.pack('<h',self.flight_interval))
                c.reg_write(R.UC_X86_REG_AX,0 if cur>=1 else 0x100);c.reg_write(R.UC_X86_REG_DX,0 if cur>=1 else 0x7000);self.ret();return
            if cs==(0x1038 if self.ru else 0x1010) and ip==(0x265 if self.ru else 0x161b):self.repeat_calls+=1
        if cs==self.world and ip==(0x205 if self.ru else 0x2aa):
            sp=c.reg_read(R.UC_X86_REG_SP);off,seg,slot=struct.unpack('<3H',c.mem_read(0x30000+sp+4,6));self.reclaimed.append(slot)
            c.mem_write(seg*16+off+0x30e+slot*4,bytes(4));self.ret()
        elif cs not in (self.segment,self.world,0x6000):
            scheduler=0x1038 if self.ru else 0x1010
            permitted={(scheduler,0x214 if self.ru else 0x15ca),(scheduler,0x265 if self.ru else 0x161b),
                       (0x1050 if self.ru else 0x1018,0xdf if self.ru else 0xfed)}
            if self.flight_mode:permitted|={(0x1130 if self.ru else 0x1078,v) for v in ((0x2b6,0x239) if self.ru else (0x194c,0x18cf))}
            assert (cs,ip) in permitted,('Unhandled original far service',hex(cs),hex(ip))
            if cs==scheduler and ip==(0x214 if self.ru else 0x15ca):self.cancelled+=1
            if cs==(0x1050 if self.ru else 0x1018) and ip==(0xdf if self.ru else 0xfed):
                sp=c.reg_read(R.UC_X86_REG_SP);args=struct.unpack('<5H',c.mem_read(0x30000+sp+4,10))
                c.mem_write(args[4]*16+args[3],struct.pack('<h',1100));c.reg_write(R.UC_X86_REG_AX,0x1234);c.reg_write(R.UC_X86_REG_DX,0x5000)
            self.ret()
    def step(self,state,shift,cursor,count):
        self.cancelled=0;self.c.mem_write(0x40000,bytes(0x90));self.c.mem_write(0x40048,struct.pack('<HH',0,0x5000));self.c.mem_write(0x50000,struct.pack('<H',count))
        self.c.mem_write(0x40083,struct.pack('<hh',state,shift));self.c.mem_write(0x40050,struct.pack('<h',cursor))
        self.run(0x2db9 if self.ru else 0x345c,struct.pack('<HH',0,0x4000))
        st,sh=struct.unpack('<hh',self.c.mem_read(0x40083,4));cur=struct.unpack('<h',self.c.mem_read(0x40050,2))[0]
        return st,sh,cur,self.cancelled
    def ready(self,state):
        self.c.mem_write(0x40083,struct.pack('<h',state));accepted=self.run(0x2d71 if self.ru else 0x3414,struct.pack('<HH',0,0x4000))
        return accepted,struct.unpack('<h',self.c.mem_read(0x40083,2))[0]
    def decay(self,damage,floor):
        c=self.c;start,end=(0x2c53,0x2c61) if self.ru else (0x32f6,0x3304)
        c.mem_write(0x4005c,struct.pack('<hh',damage,floor))
        for reg,v in ((R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_ES,0x4000),(R.UC_X86_REG_BX,0)):c.reg_write(reg,v)
        c.emu_start(self.segment*16+start,self.segment*16+end,count=20)
        return struct.unpack('<h',c.mem_read(0x4005c,2))[0]
    def slot(self,null,reclaim):
        c=self.c;c.mem_write(self.world*16,(self.path/'world.bin').read_bytes());self.reclaimed=[];c.mem_write(0x70000,bytes(0x800))
        for i in range(256):
            c.mem_write(0x70000+0x30e+i*4,struct.pack('<HH',i*0x20,0x5000) if i!=null else bytes(4))
            c.mem_write(0x50000+i*0x20+0x15,struct.pack('<H',0x400 if i==reclaim else 0))
        saved=self.segment;self.segment=self.world
        try:r=word(self.run(0x17d if self.ru else 0x1ea,struct.pack('<HH',0,0x7000)))
        finally:self.segment=saved;c.mem_write(saved*16,(self.path/'actor.bin').read_bytes())
        return r,self.reclaimed
    def distance(self,speed,elapsed,side):
        c=self.c;bp=0xf800
        c.mem_write(0x30000+bp+6,struct.pack('<HH',0,0x4000));c.mem_write(0x3266a if self.ru else 0x3271a,struct.pack('<HH',0,0x5000))
        c.mem_write(0x4003c,struct.pack('<i',speed));c.mem_write(0x50738,struct.pack('<h',side))
        for reg,v in ((R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,bp),(R.UC_X86_REG_EAX,elapsed)):c.reg_write(reg,v)
        delta=0 if self.ru else 0x6a3
        c.emu_start(self.segment*16+0x28cf+delta,self.segment*16+0x2938+delta,count=200)
        return struct.unpack('<i',c.mem_read(0x4003c,4))[0],struct.unpack('<h',c.mem_read(0x30000+bp-0x1c,2))[0]
    def flight_repeat(self,speed,frame_interval):
        c=self.c;self.flight_mode=True;self.flight_interval=frame_interval;self.repeat_calls=0
        c.mem_write(0x40000,bytes(0x100));c.mem_write(0x40026,struct.pack('<iiih',1200,-250,1200,0));c.mem_write(0x4003c,struct.pack('<i',speed))
        c.mem_write(0x4005c,struct.pack('<hh',14,7));c.mem_write(0x40069,struct.pack('<HH',0,0x8000))
        c.mem_write(0x40052,struct.pack('<h',frame_interval));c.mem_write(0x80012,struct.pack('<h',1))
        c.mem_write(0x50000,bytes(0x800));c.mem_write(0x50000,struct.pack('<hhh',5,1,5));c.mem_write(0x50738,struct.pack('<h',480));c.mem_write(0x50742,struct.pack('<H',5))
        c.mem_write(0x3266a if self.ru else 0x3271a,struct.pack('<HH',0,0x5000))
        c.mem_write(0x3608a if self.ru else 0x3613a,b' '*4096)
        c.mem_write(0x60000,bytes(0x100));c.mem_write(0x60010,struct.pack('<HH',0x100,2));c.mem_write(0x60015,struct.pack('<H',16))
        c.mem_write(0x30114,struct.pack('<HH',0x100,0x6000));c.mem_write(0x70100,struct.pack('<HH',10,10))
        try:self.run(0x26e8 if self.ru else 0x2d8b,struct.pack('<HH',0,0x4000))
        finally:self.flight_mode=False
        assert self.repeat_calls==0 and struct.unpack('<h',c.mem_read(0x40052,2))[0]==frame_interval
        return self.repeat_calls
def c_vectors(vectors):
    p=subprocess.run([ROOT/'build/nds/n15/combat_dump.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),capture_output=True,text=True,check=True)
    rows=[tuple(map(int,line.split())) for line in p.stdout.splitlines()];assert len(rows)==len(vectors);return rows
def main():
    before=originals();rng=random.Random(151029);vectors=[]
    for count in (1,3,9):
        for st in range(8):
            for shift in (-1,0,1,2,3):
                for cursor in (-2,0,count-1,count,count+1):vectors.append((0,st,shift,cursor,count))
    vectors += [(1,st,0,0,0) for st in (-32768,-1,0,1,2,3,4,5,6,7,8,32767)]
    vectors += [(2,rng.randint(-32768,32767),rng.randint(-32768,32767),0,0) for _ in range(2000)]
    vectors += [(3,speed,elapsed,side,0) for speed in (-32768,-3200,-3000,-55,-1,0,1,32767) for elapsed in (0,1,415,416,417,832,3328,3329,0xffffffff,0x1000000) for side in (4,479,480,481,32767)]
    rows=c_vectors(vectors);report=dict(task='N15',passed=False,whole_DOS_session=False,languages={})
    for lang in ('ru','en'):
        o=Original(lang)
        for v,row in zip(vectors,rows):
            mode,a,b,c,d=v;expected=o.step(a,b,c,d) if mode==0 else o.ready(a) if mode==1 else (o.decay(a,b),) if mode==2 else o.distance(a,b,c)
            assert row==expected,(lang,v,row,expected)
        for speed,interval in ((-3000,400),(-3200,1500),(-55,1000)):o.flight_repeat(speed,interval)
        for null in (-1,0,1,31,127,255):
            for reclaim in (-1,0,1,31,127,255):
                slot,removed=o.slot(null,reclaim);expected=min([v for v in (null,reclaim) if v>=0],default=-1)
                assert slot==expected and removed==([reclaim] if reclaim>=0 and reclaim==expected and reclaim!=null else [])
        report['languages'][lang]=dict(weapon_callbacks=600,readiness=12,decay=2000,distance=400,slots=36,normal_flight_repeat_callbacks=3,sample_sha256=json.loads((o.path/'analysis-manifest.json').read_text('utf-8-sig'))['sha256_after'])
    assert before==originals();report['original_sha256_before']=before;report['original_sha256_after']=originals()
    report['passed']=True;report['controlled_services']=['animation accessor image/interval','scheduler cancel/set_interval','slot destructor','normal-flight empty world lookup and target pass-through','normal-flight timer read416 and stop/start'];report['source_folder']='research/evidence/nds/n14/original'
    (ROOT/'research/evidence/nds/n15/original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
