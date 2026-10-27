"""G13 executes original NE instructions, with explicit external-call stubs.
This is not a complete DOS session or proof of the full combat loop.
"""
import hashlib,json,random,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def word(v):return (v+32768)%65536-32768
class Original:
 def __init__(self,lang):
  self.ru=lang=='ru';self.actor=0x1068 if self.ru else 0x1020;self.world=0x1098 if self.ru else 0x1030
  self.path=Path('research/evidence/combat')/lang;self.c=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
  # NE loader segment numbers are not DOS selectors: their physical bases
  # overlap in a real-mode emulator. Load only the segment under test.
  c.mem_write(self.actor*16,(self.path/'actor.bin').read_bytes())
  c.hook_add(UC_HOOK_CODE,self.hook);self.reclaimed=[];self.stub_calls=[]
 def ret(self):
  c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
  c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
 def hook(self,c,address,size,unused):
  cs=c.reg_read(R.UC_X86_REG_CS);ip=address-cs*16
  # Slot destructor boundary: record exact slot, clear it as the native remover
  # does; heap deallocation/vtables are not emulated in this isolated probe.
  if cs==self.world and ip==(0x205 if self.ru else 0x2aa):
   sp=c.reg_read(R.UC_X86_REG_SP);off,seg,slot=struct.unpack('<HHH',c.mem_read(0x30000+sp+4,6));self.reclaimed.append(slot)
   c.mem_write(seg*16+off+0x30e+slot*4,bytes(4));self.ret()
  elif cs not in (self.actor,self.world,0x6000):
   self.stub_calls.append((cs,ip));self.ret()
 def run(self,segment,entry,args):
  c=self.c;self.stub_calls=[]
  for r,v in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,segment),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:c.reg_write(r,v)
  c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args)
  c.emu_start(segment*16+entry,0x6fff0,count=20000)
  assert c.reg_read(R.UC_X86_REG_CS)==0x6000 and c.reg_read(R.UC_X86_REG_IP)==0xfff0
  return word(c.reg_read(R.UC_X86_REG_AX))
 def ready(self,state):
  self.c.mem_write(0x40083,struct.pack('<h',state));r=self.run(self.actor,0x2d71 if self.ru else 0x3414,struct.pack('<HH',0,0x4000))
  return r,struct.unpack('<h',self.c.mem_read(0x40083,2))[0]
 def slot(self,null,reclaim):
  c=self.c;c.mem_write(self.world*16,(self.path/'world.bin').read_bytes());self.reclaimed=[];c.mem_write(0x70000,bytes(0x800))
  for i in range(256):
   c.mem_write(0x70000+0x30e+i*4,struct.pack('<HH',i*0x20,0x5000) if i!=null else bytes(4))
   c.mem_write(0x50000+i*0x20+0x15,struct.pack('<H',0x400 if i==reclaim else 0))
  r=self.run(self.world,0x17d if self.ru else 0x1ea,struct.pack('<HH',0,0x7000))
  count=struct.unpack('<H',c.mem_read(0x7073c,2))[0]
  c.mem_write(self.actor*16,(self.path/'actor.bin').read_bytes());return r,count,self.reclaimed
 def spawn(self):
  c=self.c;c.mem_write(0x40000,bytes(0x90));c.mem_write(0x4002a,struct.pack('<i',-250));c.mem_write(0x50000,struct.pack('<iiih',1234,-900,5678,271))
  for reg,value in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,self.actor),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:c.reg_write(reg,value)
  c.mem_write(0x3f000,struct.pack('<8H',0xfff0,0x6000,0,0x4000,0,0x5000,0,0x8000))
  # Stop before native closure-construction helper: its stack ABI belongs to
  # scheduler setup, not to the position-copy claim checked here.
  c.emu_start(self.actor*16+(0x2669 if self.ru else 0x2d0c),self.actor*16+(0x26b4 if self.ru else 0x2d57),count=200)
  assert struct.unpack('<iiih',c.mem_read(0x40026,14))==(1234,-250,5678,271)
 def decay(self,damage,floor):
  c=self.c;start,end=(0x2c53,0x2c61) if self.ru else (0x32f6,0x3304)
  c.mem_write(0x4005c,struct.pack('<hh',damage,floor));c.reg_write(R.UC_X86_REG_CS,self.actor);c.reg_write(R.UC_X86_REG_ES,0x4000);c.reg_write(R.UC_X86_REG_BX,0)
  c.emu_start(self.actor*16+start,self.actor*16+end,count=20)
  return struct.unpack('<h',c.mem_read(0x4005c,2))[0]
 def monster(self,health,state,damage):
  c=self.c;c.mem_write(0x400a2,struct.pack('<hh',state,health));c.mem_write(0x50000,struct.pack('<I',0x40000));c.mem_write(0x6005c,struct.pack('<h',damage))
  self.run(self.actor,0x37cf if self.ru else 0x3f38,struct.pack('<6H',0,0x4000,0,0x5000,0,0x6000))
  return struct.unpack('<hh',c.mem_read(0x400a2,4))
def main():
 rng=random.Random(130026);report=dict(task='G13',status='passed',whole_DOS_session=False,languages={})
 host=subprocess.check_output(['build/native/dok-combat-tests.exe','build/gba/assets/assets.bin','readiness']).decode().splitlines()
 for lang in ('ru','en'):
  o=Original(lang)
  for line in host:
   state,accepted,after=map(int,line.split());assert o.ready(state)==(accepted,after)
  cases=[(n,r) for n in (-1,0,1,31,127,255) for r in (-1,0,1,31,127,255)]
  for null,reclaim in cases:
   expected=min([v for v in (null,reclaim) if v>=0],default=-1);actual,count,removed=o.slot(null,reclaim)
   assert actual==expected and count==(expected>=0),(lang,null,reclaim,actual,count)
   assert removed==([reclaim] if reclaim>=0 and reclaim==expected and reclaim!=null else [])
  o.spawn()
  vectors=[(rng.randint(-32768,32767),rng.randint(-32768,32767)) for _ in range(1000)]+[(14,7),(10,7),(8,7),(7,7),(4,2),(3,2)]
  for d,f in vectors:assert o.decay(d,f)==word(d+f)//2
  monsters=0
  for h in (0,1,4,14,100,32767):
   for state in (1,2,3,4):
    for damage in (-32768,-1,0,1,4,14,32767):
     actual=o.monster(h,state,damage);expected=(state,0) if not h else (3,max(0,word(h-damage)))
     assert actual==expected,(lang,h,state,damage,actual,expected);monsters+=1
  m=json.loads((o.path/'analysis-manifest.json').read_text(encoding='utf-8-sig'));assert m['sha256_before']==m['sha256_after']
  report['languages'][lang]=dict(readiness=len(host),slot_cases=len(cases),spawn_xyz_heading=1,decay_vectors=len(vectors),active_monster_damage=monsters,
   sample=m['sample'],sha256_before=m['sha256_before'],sha256_after=m['sha256_after'],manifest_sha256=sha(o.path/'analysis-manifest.json'),actor_sha256=sha(o.path/'actor.bin'),world_sha256=sha(o.path/'world.bin'))
 report['intercepted']='Slot destructor and external scheduler/clock calls; readiness, scan, copy and ADD/SAR/SUB execute original instructions. Sleeping AI wake is source-reviewed, not executed here.'
 Path('analysis/g13-original-verification.json').write_text(json.dumps(report,indent=2)+'\n');print('G13 original x86: readiness, slot order/reclaim/full, spawn XYZ, ADD/SAR and monster SUB/clamp passed')
if __name__=='__main__':main()
