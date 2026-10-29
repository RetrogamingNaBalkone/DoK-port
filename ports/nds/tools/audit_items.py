"""Execute original RU/EN ADD16/CMP/OR and pickup persistence through RETF.
HUD/text/sound far calls are intercepted. This is not a whole DOS session.
"""
import hashlib,json,random,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
class Original:
    def __init__(self,lang):
        self.ru=lang=='ru';self.segment=0x1068 if self.ru else 0x1020
        self.path=ROOT/'research/evidence/nds/n14/original'/lang
        self.c=Uc(UC_ARCH_X86,UC_MODE_16);self.c.mem_map(0,0x100000)
        self.c.mem_write(self.segment*16,(self.path/'actor.bin').read_bytes())
        self.c.hook_add(UC_HOOK_CODE,self.hook);self.calls=[]
    def hook(self,c,address,size,context):
        cs=c.reg_read(R.UC_X86_REG_CS)
        if cs in ((0x10c0,0x1108) if self.ru else (0x1050,0x1060)):
            self.calls.append((cs,c.reg_read(R.UC_X86_REG_IP)))
            sp=c.reg_read(R.UC_X86_REG_SP);ip,seg=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
            c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,seg);c.reg_write(R.UC_X86_REG_IP,ip)
    def run(self,entry,args):
        c=self.c;self.calls=[]
        for reg,value in ((R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.segment),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):
            c.reg_write(reg,value)
        c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args)
        c.emu_start(self.segment*16+entry,0x6fff0,count=3000)
        assert c.reg_read(R.UC_X86_REG_SP)==0xf004
        return c.reg_read(R.UC_X86_REG_AX)
    def stat(self,v):
        category,value,h,e,hm,em,keys,score=v;c=self.c
        c.mem_write(0x40000,bytes(0xe0));c.mem_write(0x4005e,struct.pack('<4h',h,e,hm,em))
        c.mem_write(0x40072,struct.pack('<H',keys));c.mem_write(0x400d8,struct.pack('<h',score))
        c.mem_write(0x30000+(0x266a if self.ru else 0x271a),struct.pack('<HH',0,0x7000));c.mem_write(0x7000c,b'\0\0')
        a=self.run(0x1cc2 if self.ru else 0x2276,struct.pack('<HHhH',0,0x4000,category,value&65535))
        return (a,*struct.unpack('<4h',c.mem_read(0x4005e,8)),struct.unpack('<H',c.mem_read(0x40072,2))[0],struct.unpack('<h',c.mem_read(0x400d8,2))[0],struct.unpack('<H',c.mem_read(0x7000c,2))[0])
def main():
    rng=random.Random(140029);vectors=[]
    for cat in (0,1,2,3,10,99,200):
        for value in (-32768,-101,-50,-15,-10,-1,0,1,10,15,50,100,32767):
            for current,maximum in ((0,100),(50,100),(100,100),(101,100),(32760,32767),(-1,100),(100,-1)):
                vectors.append((cat,value,current,current,maximum,maximum,5,32760))
    for i in range(4000):vectors.append((rng.choice((0,1,2,3,10,99,200)),rng.randint(-32768,32767),*[rng.randint(-32768,32767) for _ in range(4)],rng.randrange(65536),rng.randint(-32768,32767)))
    result=subprocess.run([ROOT/'build/nds/n14/items_dump.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),text=True,capture_output=True,check=True)
    lines=result.stdout.splitlines();assert len(lines)==len(vectors)
    report=dict(passed=False,languages={},whole_DOS_session=False,intercepted='HUD/text/sound far calls only')
    for language in ('ru','en'):
        n=Original(language);manifest=json.loads((n.path/'analysis-manifest.json').read_text('utf-8-sig'))
        assert manifest['sha256_before']==manifest['sha256_after']==hashlib.sha256((ROOT/manifest['sample']).read_bytes()).hexdigest()
        for v,line in zip(vectors,lines):assert n.stat(v)==tuple(map(int,line.split())),(language,v,n.stat(v),line)
        for cat in (0,1,2,3,10,99,200,1000):
            n.c.mem_write(0x40054,struct.pack('<h',cat));a=n.run(0x2f8a if n.ru else 0x362d,struct.pack('<HH',0,0x4000))
            assert a==(cat!=1000) and len(n.calls)==3
        report['languages'][language]=dict(vectors=len(vectors),callbacks=8,sample=manifest['sample'],sha256=manifest['sha256_after'],actor_sha256=hashlib.sha256((n.path/'actor.bin').read_bytes()).hexdigest())
    report['passed']=True
    (ROOT/'research/evidence/nds/n14/original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))
if __name__=='__main__':main()
