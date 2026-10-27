"""G12: execute original x86 stat arithmetic and item callback, not a DOS session.
Only HUD/sound calls are intercepted; original ADD/CMP/OR and return execute.
"""
import hashlib,json,random,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def word(n):return (n+32768)%65536-32768
class Original:
 def __init__(self,language):
  self.ru=language=='ru';self.segment=0x1068 if self.ru else 0x1020
  self.c=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
  self.path=Path('research/evidence/items')/language;c.mem_write(self.segment*16,(self.path/'actor.bin').read_bytes())
  c.hook_add(UC_HOOK_CODE,self.hook);self.hud=0
 def hook(self,c,address,size,unused):
  cs=c.reg_read(R.UC_X86_REG_CS)
  if cs in ((0x10c0,0x1108) if self.ru else (0x1050,0x1060)):
   self.hud+=1;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
   c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
 def run(self,entry,args):
  c=self.c;self.hud=0
  for r,v in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.segment),
              (R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:c.reg_write(r,v)
  c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args)
  c.emu_start(self.segment*16+entry,0x6fff0,count=3000)
  assert c.reg_read(R.UC_X86_REG_SP)==0xf004
  return c.reg_read(R.UC_X86_REG_AX)
 def stat(self,vector):
  category,value,h,e,hm,em,keys,score=vector;c=self.c
  c.mem_write(0x40000,bytes(0xe0));c.mem_write(0x4005e,struct.pack('<4h',h,e,hm,em))
  c.mem_write(0x40072,struct.pack('<H',keys));c.mem_write(0x400d8,struct.pack('<h',score))
  c.mem_write(0x30000+(0x266a if self.ru else 0x271a),struct.pack('<HH',0,0x7000));c.mem_write(0x7000c,b'\0\0')
  accepted=self.run(0x1cc2 if self.ru else 0x2276,struct.pack('<HHhH',0,0x4000,category,value&65535))
  return (accepted,*struct.unpack('<4h',c.mem_read(0x4005e,8)),struct.unpack('<H',c.mem_read(0x40072,2))[0],
   struct.unpack('<h',c.mem_read(0x400d8,2))[0],struct.unpack('<H',c.mem_read(0x7000c,2))[0])
def main():
 rng=random.Random(120026);vectors=[]
 for category in (0,1,2,3,10,99,200):
  for value in (-32768,-101,-50,-15,-10,-1,0,1,10,15,50,100,32767):
   for current,maximum in ((0,100),(50,100),(100,100),(101,100),(32760,32767),(-1,100),(100,-1)):
    vectors.append((category,value,current,current,maximum,maximum,5,32760))
 for _ in range(2000):
  vectors.append((rng.choice((0,1,2,3,10,99,200)),rng.randint(-32768,32767),*[rng.randint(-32768,32767) for _ in range(4)],rng.randrange(65536),rng.randint(-32768,32767)))
 host=subprocess.check_output(['build/native/dok-items-probe.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors).encode()).decode().splitlines()
 assert len(host)==len(vectors);report=dict(task='G12',status='passed',whole_DOS_session=False,intercepted='HUD and sound far calls only',languages={})
 for language in ('ru','en'):
  native=Original(language)
  for vector,line in zip(vectors,host):
   actual=native.stat(vector);expected=tuple(map(int,line.split()));assert actual==expected,(language,vector,actual,expected)
  # Execute original item notification callback. Its persistence decision is
  # independent of flags/value; every used stat consumes one TMap cell.
  for category in (0,1,2,3,10,99,200,1000):
   native.c.mem_write(0x40054,struct.pack('<h',category))
   accepted=native.run(0x2f8a if native.ru else 0x362d,struct.pack('<HH',0,0x4000))
   assert accepted==(category!=1000) and native.hud==3,(language,category,accepted,native.hud)
  manifest=native.path/'analysis-manifest.json';m=json.loads(manifest.read_text(encoding='utf-8-sig'))
  assert m['sha256_before']==m['sha256_after']
  report['languages'][language]=dict(stat_vectors=len(vectors),pickup_callback_vectors=8,manifest_sha256=sha(manifest),actor_sha256=sha(native.path/'actor.bin'),sample=m['sample'],sha256=m['sha256_after'])
 Path('analysis/g12-original-verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f'G12 original x86: {2*len(vectors)} stat vectors and16 item callback vectors passed')
if __name__=='__main__':main()
