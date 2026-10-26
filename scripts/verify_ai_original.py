"""G14 original x86 slices. External abs/wake boundaries are explicit.
Not a DOS session: rendering, scheduler closure and whole callback are not emulated.
"""
import hashlib,json,math,random,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def table():
 out=[0]*101;out[100]=45;i=0;previous=0
 for degree in range(46):
  theta=degree*3.14159265359/180
  limit=int(abs(math.sin(theta))*1000)*100//int(abs(math.cos(theta))*1000)
  while i<limit:out[i]=previous;i+=1
  previous=degree
 return out
class Original:
 def __init__(self,lang):
  self.ru=lang=='ru';self.seg=0x1068 if self.ru else 0x1020;self.math=0x10c8 if self.ru else 0x1058;self.tables=0x1180 if self.ru else 0x10c0
  self.path=Path('build/ai')/lang;self.c=Uc(UC_ARCH_X86,UC_MODE_16);self.c.mem_map(0,0x100000);self.wakes=0
  self.c.hook_add(UC_HOOK_CODE,self.hook)
 def ret(self):
  c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4));c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
 def hook(self,c,address,size,unused):
  cs=c.reg_read(R.UC_X86_REG_CS);ip=address-cs*16
  if cs==0x1000: # signed32 abs helper used by exact native heading
   sp=c.reg_read(R.UC_X86_REG_SP);n=abs(struct.unpack('<i',c.mem_read(0x30000+sp+4,4))[0]);c.reg_write(R.UC_X86_REG_AX,n&65535);c.reg_write(R.UC_X86_REG_DX,n>>16);self.ret()
  elif cs==self.seg and ip==(0x3747 if self.ru else 0x3eb0):self.wakes+=1;self.ret()
 def run(self,seg,entry,args):
  c=self.c
  for reg,n in ((R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,seg),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,n)
  c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args);c.emu_start(seg*16+entry,0x6fff0,count=3000)
  assert c.reg_read(R.UC_X86_REG_CS)==0x6000
  return c.reg_read(R.UC_X86_REG_AX)
 def headings(self,vectors):
  c=self.c;c.mem_write(self.math*16,(self.path/'math.bin').read_bytes());c.mem_write(self.tables*16+0xb48,struct.pack('<101h',*table()))
  return [(360-self.run(self.math,0x1c5,struct.pack('<ii',z,x)))%360 for x,z in vectors]
 def near(self,x,z,d,state=0):
  c=self.c;c.mem_write(self.seg*16,(self.path/'actor.bin').read_bytes());c.mem_write(0x3266a if self.ru else 0x3271a,struct.pack('<HH',0,0x7000));c.mem_write(0x70442,struct.pack('<HH',0,0x5000));c.mem_write(0x50026,struct.pack('<i',x));c.mem_write(0x5002e,struct.pack('<i',z));c.mem_write(0x40026,bytes(12));c.mem_write(0x400a2,struct.pack('<h',state));c.mem_write(0x400ba,struct.pack('<i',d));self.wakes=0
  self.run(self.seg,0x3eca if self.ru else 0x4633,struct.pack('<HH',0,0x4000));return bool(self.wakes)
 def course(self,counter,period):
  c=self.c;c.mem_write(self.seg*16,(self.path/'actor.bin').read_bytes());c.reg_write(R.UC_X86_REG_CS,self.seg);c.reg_write(R.UC_X86_REG_SS,0x3000);c.reg_write(R.UC_X86_REG_BP,0xf800);c.mem_write(0x3f806,struct.pack('<HH',0,0x4000));c.mem_write(0x400ac,struct.pack('<hh',counter,period))
  start,yes,no=(0x3b86,0x3b9e,0x3d39) if self.ru else (0x42ef,0x4307,0x44a2)
  # Stop as soon as either branch rejoins its next semantic operation.
  def stop(cpu,address,size,unused):
   if address in (self.seg*16+yes,self.seg*16+no):cpu.emu_stop()
  h=c.hook_add(UC_HOOK_CODE,stop);c.emu_start(self.seg*16+start,0xfffff,count=100);c.hook_del(h)
  return struct.unpack('<h',c.mem_read(0x400ac,2))[0]
def main():
 rng=random.Random(14);vectors=[(x,z) for x in (-30000,-480,-1,0,1,480,30000) for z in (-30000,-480,-1,0,1,480,30000)]+[(rng.randint(-30000,30000),rng.randint(-30000,30000)) for _ in range(1000)]
 host=list(map(int,subprocess.check_output(['build/native/dok-ai-tests.exe','heading'],input=''.join(f'{x} {z}\n' for x,z in vectors).encode()).split()))
 report=dict(task='G14',status='passed',whole_DOS_session=False,languages={})
 for lang in ('ru','en'):
  o=Original(lang);assert o.headings(vectors)==host
  count=0
  for distance in (3400,4000,9000,20000):
   for x in (-distance,-distance+1,0,distance-1,distance):
    for z in (-distance,-distance+1,0,distance-1,distance):
     assert o.near(x,z,distance)==(max(abs(x),abs(z))<distance);assert not o.near(x,z,distance,1);count+=2
  for period in (0,4,8,10,24):
   for counter in (0,1,period,period+1,32767):
    expected=0 if counter==period else (counter+1+32768)%65536-32768
    assert o.course(counter,period)==expected,(lang,counter,period,o.course(counter,period),expected)
  m=json.loads((o.path/'analysis-manifest.json').read_text(encoding='utf-8-sig'));assert m['sha256_before']==m['sha256_after']
  report['languages'][lang]=dict(heading_vectors=len(vectors),perception_vectors=count,course_vectors=25,sample=m['sample'],sha256_before=m['sha256_before'],sha256_after=m['sha256_after'],manifest_sha256=sha(o.path/'analysis-manifest.json'))
 report['limits']='Heading uses regenerated ctor table (1000 scale). Native integer heading, threshold getter, counter branches executed; abs/wake external boundaries intercepted. Full AI callback, FPU ctor and whole DOS session are not claimed.'
 Path('analysis/g14-original-verification.json').write_text(json.dumps(report,indent=2)+'\n');print('G14 original x86 heading, proximity and course branches passed')
if __name__=='__main__':main()

