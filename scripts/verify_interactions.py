"""G09 original door instructions vs C, with platform/scheduler services hooked.
Not a whole DOS session. Far-pointer memory is controlled real-mode Unicorn.
"""
import hashlib,json,random,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R

class Original:
 def __init__(self,lang):
  self.ru=lang=='ru';self.c=Uc(UC_ARCH_X86,UC_MODE_16);self.c.mem_map(0,0x100000)
  self.c.mem_write(0x50000,(ROOT/'research/evidence/interactions'/lang/'actor.bin').read_bytes())
  self.c.mem_write(0x30000,(ROOT/'research/evidence/interactions'/lang/'data.bin').read_bytes())
  self.actorseg=0x1068 if self.ru else 0x1020
  self.entries=[0x347e,0x3262,0x334e,0x3427,0x521] if self.ru else [0x3b33,0x3917,0x3a03,0x3adc,0xa01]
  self.scheduler=0x10380 if self.ru else 0x10100
  self.schedule=0x108 if self.ru else 0x14be
  self.cancel=0x214 if self.ru else 0x15ca
  self.interval=0x265 if self.ru else 0x161b
  self.copy=0x10000+(0x5cd5 if self.ru else 0x5cd5)
  self.c.hook_add(UC_HOOK_CODE,self.hook)
 def reg(self,name,value=None):
  r=getattr(R,'UC_X86_REG_'+name)
  if value is None:return self.c.reg_read(r)
  self.c.reg_write(r,value)
 def ret(self):
  sp=self.reg('SP');ip,cs=struct.unpack('<HH',self.c.mem_read(0x30000+sp,4))
  self.reg('SP',sp+4);self.reg('CS',cs);self.reg('IP',ip)
 def hook(self,c,address,size,ctx):
  if 0x50000<=address<0x55100:return
  sp=self.reg('SP')
  if address==self.copy:
   ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4));ptr=self.reg('DX')*16+self.reg('AX')
   c.mem_write(0x30000+sp-4,bytes(c.mem_read(ptr,8)));self.reg('SP',sp-4);self.reg('CS',cs);self.reg('IP',ip);return
  if address==self.scheduler+self.schedule:
   cb,seg,param,interval=struct.unpack('<HHII',c.mem_read(0x30000+sp+8,12))
   assert seg==self.actorseg,(hex(address),hex(seg))
   self.queue.append([self.entries.index(cb),interval])
  elif address==self.scheduler+self.cancel:self.keep=False
  elif address==self.scheduler+self.interval:
   self.queue[0][1]=struct.unpack('<I',c.mem_read(0x30000+sp+8,4))[0]
  elif address in self.services:self.reg('AX',0)
  else:raise AssertionError(('unhooked',hex(address)))
  self.ret()
 def run(self,v,sensor=False):
  op,flags,frame,phase,ready,occupancy,extent,lock,mode,delay,interval,event,keys,source=v
  b=bytearray(0x100);struct.pack_into('<H',b,0x15,flags);struct.pack_into('<hh',b,0x50,frame,interval)
  struct.pack_into('<BBhhhhh',b,0x54,phase,ready,occupancy,0,0,lock,mode)
  struct.pack_into('<h',b,0x60,extent);struct.pack_into('<iH',b,0x66,delay,64)
  struct.pack_into('<H',b,0x10,0x900)
  self.c.mem_write(0x40000,bytes(b));src=bytearray(0x100);struct.pack_into('<H',src,0x12,source);struct.pack_into('<H',src,0x72,keys)
  if sensor:
   src[0x66]=ord('h');struct.pack_into('<I',src,0x67,event)
   self.c.mem_write(0x30000+(0x266a if self.ru else 0x271a),struct.pack('<HH',0x1000,0x4000))
   self.c.mem_write(0x41000+0x30e+ord('h')*4,struct.pack('<HH',0,0x4000))
   self.c.mem_write(0x30900+0x14,struct.pack('<HH',self.entries[0],0x5000))
  self.c.mem_write(0x40200,bytes(src));self.c.mem_write(0x40400,struct.pack('<I',event))
  self.c.mem_write(0x300c2,struct.pack('<I',30));self.queue=[] if op in (0,4) else [[op,0]];self.keep=True
  self.reg('SS',0x3000);self.reg('CS',0x5000);self.reg('SP',0xf000);self.reg('BP',0xf800)
  self.c.mem_write(0x3f000,struct.pack('<8H',0xfff0,0x5000,0x200 if sensor else 0,0x4000,0x400,0x4000,0x200,0x4000))
  self.c.emu_start(0x50000+((0x91e if self.ru else 0xe1c) if sensor else self.entries[op]),0x5fff0,count=10000)
  assert self.reg('SP')==0xf004
  if not self.keep:self.queue.pop(0)
  b=bytes(self.c.mem_read(0x40000,0x100));out=[struct.unpack_from('<H',b,0x15)[0],struct.unpack_from('<h',b,0x50)[0],b[0x54],b[0x55],struct.unpack_from('<h',b,0x56)[0],struct.unpack_from('<h',b,0x60)[0],struct.unpack_from('<h',b,0x52)[0],struct.unpack('<I',self.c.mem_read(0x40400,4))[0],len(self.queue)]
  for i in range(3):out+=self.queue[i] if i<len(self.queue) else [0,0]
  return out

def crop_check(lang):
 """Execute original crop body without intercepted callees, all 30 phases."""
 c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
 c.mem_write(0x50000,(ROOT/'research/evidence/interactions'/lang/'image.bin').read_bytes());count=0
 for width,height in ((96,128),(96,192)):
  pixels=bytes((i*37+i//width*13)%256 for i in range(width*height));c.mem_write(0x60000,pixels)
  for flags in (0x48,0x108):
   for frame in range(30):
    header=struct.pack('<5HIHH',width,height,flags,30,frame,width*height,0,0x6000)
    c.mem_write(0x40000,header);c.mem_write(0x3f000,struct.pack('<4H',0xfff0,0x5000,0,0x4000))
    c.reg_write(R.UC_X86_REG_SS,0x3000);c.reg_write(R.UC_X86_REG_CS,0x5000);c.reg_write(R.UC_X86_REG_SP,0xf000);c.reg_write(R.UC_X86_REG_BP,0xf800)
    c.emu_start(0x50000+(0x5c9 if lang=='ru' else 0x6d9),0x5fff0,count=100000)
    pointer=c.reg_read(R.UC_X86_REG_DX)*16+c.reg_read(R.UC_X86_REG_AX);got=bytes(c.mem_read(pointer,width*height))
    shift=(width//30 if flags==0x48 else height//30)*frame
    if flags==0x48:expected=b''.join(bytes(shift)+pixels[y*width:y*width+width-shift] for y in range(height))
    else:expected=bytes(shift*width)+pixels[:(height-shift)*width]
    assert got==expected,(lang,'crop',width,height,flags,frame,hex(pointer));count+=1
 return count

def main():
 rng=random.Random(9009);vectors=[]
 for op in range(5):
  for _ in range(1200):
   vectors.append([op,rng.choice([64,72,88,80]),rng.randrange(0,31),rng.choice([1,2]),rng.randrange(2),rng.choice([-1,0,1,2]),30,rng.choice([0,1,2,3,4,8]),rng.choice([0,1,2]),rng.choice([0,100,40000]),rng.choice([0,10,400]),rng.choice([0,0x800,0x40000,0x40800]),rng.randrange(16),rng.choice([1,13])])
   # Direct callback oracle does not run an appended zero-delay callback in
   # the same dispatch. Real LEVEL autoclose doors use delay40000.
   if op==3 and vectors[-1][9]==0:vectors[-1][9]=100
 expected=[list(map(int,l.split())) for l in subprocess.check_output([ROOT/'build/native/dok-interactions-probe.exe'],input=''.join(' '.join(map(str,v))+'\n' for v in vectors),text=True).splitlines()]
 report={'task':'G09','status':'passed','whole_dos_session':False,'verifier_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'languages':{},'artifacts':{}}
 for lang in ('ru','en'):
  o=Original(lang)
  # All remaining external calls here are sound state or message output.
  o.services={0x11080+x for x in (0x1f7,0x232,0x26d,0x2b9)}|{0x10c00+0x1278} if lang=='ru' else {0x10600+x for x in (0x173d,0x1778,0x17b3,0x17ff)}|{0x10500+0x15db}
  for i,(v,want) in enumerate(zip(vectors,expected)):
   got=o.run(v);assert got==want,(lang,i,v,want,got)
  sensors=0
  for v,want in zip(vectors,expected):
   if v[0]==0 and v[-1]==13:
    got=o.run(v,sensor=True);want=want.copy();want[7]=v[11]
    assert got==want,(lang,'sensor',v,want,got);sensors+=1
  manifest=json.loads((ROOT/'research/evidence/interactions'/lang/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
  report['languages'][lang]={'vectors':len(vectors),'sensor_routes':sensors,'crop_vectors':crop_check(lang),'manifest':manifest}
  for p in (ROOT/'research/evidence/interactions'/lang).iterdir():
   if p.suffix in ('.bin','.asm','.c','.json'):report['artifacts'][p.relative_to(ROOT).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
 (ROOT/'analysis/g09-original-verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print('G09 original door/base handlers and sensor routes matched C')
if __name__=='__main__':main()
