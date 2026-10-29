"""N16 original integer heading/getter/course instructions, independent of NDS C.
Original FPU ctor is reviewed; its1000-scale table is regenerated explicitly.
"""
import hashlib,json,random,struct,subprocess
from audit_items import Original as ItemOriginal,ROOT
from audit_sprites import originals
from generate_ai_table import table
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as R
class Original(ItemOriginal):
    def __init__(self,lang):
        super().__init__(lang);self.math=0x10c8 if self.ru else 0x1058;self.tables=0x1180 if self.ru else 0x10c0;self.wakes=0
        manifest=json.loads((self.path/'analysis-manifest.json').read_text('utf-8-sig'))
        for name,sha in manifest['binary_sha256'].items():assert hashlib.sha256((self.path/(name+'.bin')).read_bytes()).hexdigest()==sha
    def ret(self):
        c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        for reg,v in ((R.UC_X86_REG_SP,sp+4),(R.UC_X86_REG_CS,cs),(R.UC_X86_REG_IP,ip)):c.reg_write(reg,v)
    def hook(self,c,address,size,context):
        cs=c.reg_read(R.UC_X86_REG_CS);ip=c.reg_read(R.UC_X86_REG_IP)
        if cs==0x1000:
            assert ip==0x4c90,(hex(cs),hex(ip));sp=c.reg_read(R.UC_X86_REG_SP);n=abs(struct.unpack('<i',c.mem_read(0x30000+sp+4,4))[0])
            c.reg_write(R.UC_X86_REG_AX,n&65535);c.reg_write(R.UC_X86_REG_DX,n>>16);self.ret()
        elif cs==self.segment and ip==(0x3747 if self.ru else 0x3eb0):self.wakes+=1;self.ret()
    def heading(self,x,z):
        c=self.c;c.mem_write(self.math*16,(self.path/'math.bin').read_bytes());c.mem_write(self.tables*16+0xb48,struct.pack('<101h',*table()))
        saved=self.segment;self.segment=self.math
        try:angle=self.run(0x1c5,struct.pack('<ii',z,x))
        finally:self.segment=saved
        return (360-angle)%360
    def near(self,x,z,d,state):
        c=self.c;c.mem_write(self.segment*16,(self.path/'actor.bin').read_bytes())
        c.mem_write(0x3266a if self.ru else 0x3271a,struct.pack('<HH',0,0x7000));c.mem_write(0x70442,struct.pack('<HH',0,0x5000))
        c.mem_write(0x50026,struct.pack('<i',x));c.mem_write(0x5002e,struct.pack('<i',z));c.mem_write(0x40026,bytes(12));c.mem_write(0x400a2,struct.pack('<h',state));c.mem_write(0x400ba,struct.pack('<i',d));self.wakes=0
        self.run(0x3eca if self.ru else 0x4633,struct.pack('<HH',0,0x4000));return int(bool(self.wakes))
    def course(self,counter,period):
        c=self.c;c.mem_write(self.segment*16,(self.path/'actor.bin').read_bytes());c.mem_write(0x3f806,struct.pack('<HH',0,0x4000));c.mem_write(0x400ac,struct.pack('<hh',counter,period))
        for reg,v in ((R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,v)
        start,yes,no=(0x3b86,0x3b9e,0x3d39) if self.ru else (0x42ef,0x4307,0x44a2);branches=[]
        def stop(cpu,address,size,context):
            if address in (self.segment*16+yes,self.segment*16+no):branches.append(address==self.segment*16+yes);cpu.emu_stop()
        h=c.hook_add(UC_HOOK_CODE,stop)
        try:c.emu_start(self.segment*16+start,0xfffff,count=100)
        finally:c.hook_del(h)
        assert len(branches)==1;return struct.unpack('<h',c.mem_read(0x400ac,2))[0],int(branches[0])
def main():
    before=originals();rng=random.Random(161029);vectors=[]
    vectors += [(0,x,z,0,0) for x in (-30000,-480,-1,0,1,480,30000) for z in (-30000,-480,-1,0,1,480,30000)]
    vectors += [(0,rng.randint(-30000,30000),rng.randint(-30000,30000),0,0) for _ in range(2000)]
    vectors += [(1,x,z,d,state) for d in (3400,4000,9000,20000) for x in (-d,-d+1,0,d-1,d) for z in (-d,-d+1,0,d-1,d) for state in (0,1)]
    vectors += [(2,counter,period,0,0) for period in (-1,0,4,8,10,24,32767) for counter in (-32768,-1,0,1,period,32767)]
    p=subprocess.run([ROOT/'build/nds/n16/ai_dump.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),text=True,capture_output=True,check=True)
    rows=[tuple(map(int,line.split())) for line in p.stdout.splitlines()];assert len(rows)==len(vectors)
    report=dict(task='N16',passed=False,languages={},controlled=['signed32 abs helper','wake boundary'],limits=['No full active callback/FPU ctor/DOS session replay','Native course trial uses previously verified N10 resolver and N09 Q15 motion boundaries'])
    for lang in ('ru','en'):
        o=Original(lang)
        for v,row in zip(vectors,rows):
            mode,a,b,c,d=v;source=(o.heading(a,b),) if mode==0 else (o.near(a,b,c,d),) if mode==1 else o.course(a,b)
            assert source==row,(lang,v,source,row)
        report['languages'][lang]=dict(heading=2049,getters=200,course=42)
    assert before==originals();report.update(passed=True,original_sha256_before=before,original_sha256_after=originals(),table_sha256=hashlib.sha256((ROOT/'ports/nds/core/ai_atan.inc').read_bytes()).hexdigest())
    (ROOT/'research/evidence/nds/n16/original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report))
if __name__=='__main__':main()
