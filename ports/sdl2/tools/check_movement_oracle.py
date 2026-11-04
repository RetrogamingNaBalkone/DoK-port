"""SP10 independent movement x86 oracle for OG/PKR/PKE.

Reads fresh Ghidra byte exports. Native lookup, footprint, resolver and three
substeps execute without replacement callees. Only stopwatch reads are controlled.
GBA collision research was consulted for ABI documentation, not imported as code.
"""
from pathlib import Path
import csv,hashlib,json,random,re,struct,subprocess
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp10-movement'
LAYOUT={
 'ru':dict(actor=0x1068,world=0x1098,clear=0x448,resolve=0x901,move=0xeb1,motion=0xa85,maps=0x608a,global_world=0x266a,trig_segment=0x1180,trig_offset=0,clock_segment=0x1130,clock_read=0x26f,clock_stop=0x2b6,clock_start=0x239),
 'en':dict(actor=0x1020,world=0x1030,clear=0x52c,resolve=0x9e5,move=0x13bb,motion=0xf8f,maps=0x613a,global_world=0x271a,trig_segment=0x10c0,trig_offset=0,clock_segment=0x1078,clock_read=0x1905,clock_stop=0x194c,clock_start=0x18cf),
 'old-gold':dict(actor=0x1ae1,world=0x21b8,clear=0x412,resolve=0x68f,move=0xdc0,motion=0x908,maps=0x6faa,global_world=0x358a,trig_segment=0x3000,trig_offset=0xb3ba,clock_segment=0x2a5b,clock_read=0x237,clock_stop=0x273,clock_start=0x20a),
}
RES={'ru':'originals/pkr/RUS/PKR.RES','en':'originals/pkr/ENG/PKE.RES','old-gold':'originals/Old_Gold_3D/OLD_GOLD.RES'}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fnv(b):
 h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 return h

def host(profile,model,vectors):
 cmd=[str(ROOT/'build/sdl2/windows/dok_movement_probe.exe'),str(ROOT/RES[profile]),model['name']]
 payload=''.join(' '.join(map(str,v))+'\n' for v in vectors)
 p=subprocess.run(cmd,input=payload,capture_output=True,text=True,check=True)
 values=[list(map(int,line.split(','))) for line in p.stdout.splitlines()]
 assert len(values)==len(vectors)
 return values

class Original:
 def __init__(self,profile,model):
  self.profile=profile;self.layout=l=LAYOUT[profile];self.model=model
  self.cpu=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
  folder=OUT/'ghidra'/profile;manifest=json.loads((folder/'manifest.json').read_text())
  self.raw={}
  for name,address in [('actor',0x50000),('world',0x60000)]:
   raw=(folder/(name+'.bin')).read_bytes();assert sha(folder/(name+'.bin'))==manifest['bytes_sha256'][name]
   self.raw[name]=raw;c.mem_write(address,raw)
  self.width,_,self.length,self.side,_,_,_,self.radius=model['settings']
  hero=next(a for a in model['world']['actors'] if a['class_id']==1)
  self.hero=hero['glyph'];self.spawn=hero
  self.maps=[bytearray.fromhex(v) for v in model['world']['maps']]
  self.maps[1]=self.maps[1].replace(bytes([self.hero]),b' ')
  self.flags=[0]*256;self.classes=[0]*256
  for d in model['objects']:self.flags[d['glyph']]=d['flags'];self.classes[d['glyph']]=d['class']
  header=bytearray(0x750);struct.pack_into('<hhh',header,0,self.width,1,self.length)
  struct.pack_into('<h',header,10,self.radius);struct.pack_into('<h',header,0x738,self.side);struct.pack_into('<i',header,0x742,self.width)
  for glyph in range(256):
   off=0x1000+glyph*0x80;struct.pack_into('<HH',header,0x30e+glyph*4,off,0x4000)
   obj=bytearray(0x80);struct.pack_into('<H',obj,0x12,self.classes[glyph]);struct.pack_into('<H',obj,0x15,self.flags[glyph]);c.mem_write(0x40000+off,bytes(obj))
  c.mem_write(0x40000,bytes(header))
  c.mem_write(0x30000+l['global_world'],struct.pack('<HH',0,0x4000))
  # Frozen source-port trig values are independently checked against DOS RAM;
  # use one verified shared input instead of evaluating host sin/cos in an oracle.
  text=(ROOT/'ports/sdl2/src/trig.inc').read_text()
  trig=[]
  for section in text.split('= {{')[1:]:trig.extend(map(int,re.findall(r'-?\d+',section.split('}};')[0])))
  assert len(trig)==722
  self.trig=trig
  c.mem_write(l['trig_segment']*16+l['trig_offset'],struct.pack('<722i',*trig))
  c.hook_add(UC_HOOK_CODE,self.hook)
  self.reset_maps()
  self.elapsed=0;self.trace=[]

 def reset_maps(self,empty=False):
  # Native globals order is MMap/TMap/FCMap, each padded to 66x66.
  c=self.cpu;l=self.layout
  for plane,source in enumerate([self.maps[1],self.maps[0],self.maps[2]]):
   c.mem_write(0x30000+l['maps']+plane*4356,b' '*4356)
   c.mem_write(0x30000+l['maps']+plane*4356,bytes(b' '*len(source) if empty else source))
  for glyph in range(256):c.mem_write(0x41056+glyph*0x80,b'\0\0')

 def ret(self,value=0):
  c=self.cpu;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
  c.reg_write(R.UC_X86_REG_AX,value&65535);c.reg_write(R.UC_X86_REG_DX,(value>>16)&65535)
  c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)

 def hook(self,c,address,size,unused):
  cs=c.reg_read(R.UC_X86_REG_CS);l=self.layout
  if address in [0x5fff0,0x6fff0]:c.emu_stop();return
  if address==0x50000+l['move']:
   sp=c.reg_read(R.UC_X86_REG_SP)
   self.trace.append(list(struct.unpack('<hH',c.mem_read(0x30000+sp+8,4))))
  if cs in [0x5000,0x6000]:return
  if address==l['clock_segment']*16+l['clock_read']:self.ret(self.elapsed);return
  if address in [l['clock_segment']*16+l['clock_stop'],l['clock_segment']*16+l['clock_start']]:self.ret();return
  if self.profile!='old-gold':
   if cs==l['actor']:
    ip=c.reg_read(R.UC_X86_REG_IP);c.reg_write(R.UC_X86_REG_CS,0x5000);c.reg_write(R.UC_X86_REG_IP,ip);return
   if cs==l['world']:
    ip=c.reg_read(R.UC_X86_REG_IP);c.reg_write(R.UC_X86_REG_CS,0x6000);c.reg_write(R.UC_X86_REG_IP,ip);return
  else:
   # OG linker uses equivalent MZ segment:offset aliases for far calls.
   for name,target in [('actor',0x5000),('world',0x6000)]:
    offset=address-l[name]*16
    if 0<=offset<len(self.raw[name]):
     c.reg_write(R.UC_X86_REG_CS,target);c.reg_write(R.UC_X86_REG_IP,offset);return
  raise RuntimeError(f"Unmodelled native callee {self.profile} {cs:04x}:{c.reg_read(R.UC_X86_REG_IP):04x}")

 def call(self,entry,args,segment):
  c=self.cpu
  for reg,value in [(R.UC_X86_REG_CS,segment),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_ES,0x4000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xee00),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,value)
  c.mem_write(0x3f000,struct.pack('<HH',0xfff0,segment)+args)
  c.emu_start(segment*16+entry,0,count=100000)
  assert c.reg_read(R.UC_X86_REG_IP)==0xfff0,(self.profile,entry,'did not return')
  assert c.reg_read(R.UC_X86_REG_SP)==0xf004,(self.profile,entry,'stack imbalance')
  return c.reg_read(R.UC_X86_REG_AX)

 def run(self,v):
  op,x,z,nx,nz,*extra=v;c=self.cpu;l=self.layout
  if op==0:
   c.mem_write(0x4f000,struct.pack('<ii',x,z))
   self.call(l['resolve'],struct.pack('<IIIii',0x40000000,0x4000f000,0x4000f004,nx,nz),0x6000)
   return list(struct.unpack('<ii',c.mem_read(0x4f000,8)))
  if op==1:return [self.call(l['clear'],struct.pack('<Iiih',0x40000000,x,z,self.radius),0x6000)]
  self.reset_maps(empty=op in [3,4]);self.trace=[]
  actor=bytearray(0xde);struct.pack_into('<iii',actor,0x26,x,self.spawn['y'],z)
  actor[0x5c]=self.hero;struct.pack_into('<h',actor,0x32,nz if op==2 else nx)
  if op==2:
   cell=(z//self.side)*self.width+x//self.side
   c.mem_write(0x30000+l['maps']+cell,bytes([self.hero]));c.mem_write(0x4e000,bytes(actor))
   self.call(l['move'],struct.pack('<IhH',0x4000e000,nx,extra[0]),0x5000)
   pose=list(struct.unpack('<iii',c.mem_read(0x4e026,12)))
   mmap=bytes(c.mem_read(0x30000+l['maps'],self.width*self.length))
   counters=b''.join(bytes(c.mem_read(0x41056+g*0x80,2)) for g in range(256))
   return pose+[fnv(mmap),fnv(counters)]
  if op in [3,4]:
   count=extra.pop() if op==4 else 1
   self.elapsed,forward,strafe,turn,run,slow,aa,ab,direction,last_forward,last_strafe=extra
   flags=(0x10 if forward>0 else 0x20 if forward<0 else 0)|(0x100000 if strafe>0 else 0x80000 if strafe<0 else 0)|(8 if turn>0 else 4 if turn<0 else 0)|(0x2000 if run else 0)|(0x4000000 if slow else 0)
   remembered=(0x10 if last_forward>0 else 0x20 if last_forward<0 else 0)|(0x100000 if last_strafe>0 else 0x80000 if last_strafe<0 else 0)
   hero=next(o for o in self.model['objects'] if o['glyph']==self.hero)
   fields={f[1]:f[2] for f in hero['fields']}
   for offset,fmt,value in [(0x1c,'I',flags),(0x34,'h',fields[0x34]),(0x38,'i',fields[0x38]),(0x5a,'h',4 if self.profile=='old-gold' else 0),(0x80,'h',nz),(0x82,'h',direction),(0x90,'i',aa),(0x94,'i',ab),(0x9a,'I',remembered),(0x9e,'h',self.spawn['y']+26),(0xa0,'h',self.spawn['y']-26)]:struct.pack_into('<'+fmt,actor,offset,value)
   c.mem_write(0x4e000,bytes(actor))
   for step in range(count):
    c.mem_write(0x4e01c,struct.pack('<I',flags));self.call(l['motion'],struct.pack('<I',0x4000e000),0x5000)
   data=bytes(c.mem_read(0x4e000,0xde))
   return list(struct.unpack_from('<iii',data,0x26))+[struct.unpack_from('<h',data,0x32)[0],struct.unpack_from('<i',data,0x90)[0],struct.unpack_from('<i',data,0x94)[0],struct.unpack_from('<h',data,0x82)[0],struct.unpack_from('<h',data,0x80)[0]]

def vectors(model,seed=10):
 rng=random.Random(seed);width,_,length,side,_,_,_,radius=model['settings'];v=[]
 for row in range(length):
  for col in range(width):v.append([1,col*side+side//2,row*side+side//2,0,0])
 for _ in range(512):
  x=rng.randrange(-side,width*side+side);z=rng.randrange(-side,length*side+side)
  v.append([0,x,z,x+rng.randrange(-side//2,side//2),z+rng.randrange(-side//2,side//2)])
 for x,z in [(radius,radius),(0,0),(-side,-side),(width*side,length*side),(side*2+side//2,side*3+side//2)]:
  for dx,dz in [(0,0),(-1,0),(1,0),(0,-1),(0,1),(-side//3,side//3),(side//3,-side//3)]:v.append([0,x,z,x+dx,z+dz])
 for x in [-2147483648,-2147483000,2147483647,65536*side,-65536*side]:
  for z in [-side,0,side,2147483647]:v.append([1,x,z,0,0])
 return v

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 inv=json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles'];originals=[p['sample'] for p in inv]+list(RES.values());before={p:sha(ROOT/p) for p in originals}
 for p in inv:assert before[p['sample']]==p['sha256_before']
 reports=[];examples=[]
 for profile,path in RES.items():
  models=json.loads(subprocess.run([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(ROOT/path)],capture_output=True,text=True,check=True).stdout)['levels']
  for model in models:
   oracle=Original(profile,model);v=vectors(model);actual=host(profile,model,v)
   failures=[]
   for item,cpp in zip(v,actual):
    native=oracle.run(item)
    if native!=cpp:failures.append(dict(input=item,native=native,port=cpp))
   # Reset maps after static comparisons, before testing native wrappers.
   free=[item for item,a in zip(v,actual) if item[0]==1 and a==[1] and oracle.radius<=item[1]<oracle.width*oracle.side-oracle.radius and oracle.radius<=item[2]<oracle.length*oracle.side-oracle.radius]
   wrapper=[];rng=random.Random(101)
   for _ in range(100):
    _,x,z,_,_=rng.choice(free);wrapper.append([2,x,z,rng.choice([-240,-121,-120,-1,0,1,120,121,240]),rng.randrange(360),rng.randrange(2)])
   actual=host(profile,model,wrapper)
   wrapper_failures=[]
   for item,cpp in zip(wrapper,actual):
    native=oracle.run(item)
    if native!=cpp:wrapper_failures.append(dict(input=item,native=native,port=cpp))
   # Motion uses controlled free geometry; collisions are independently native above.
   motion=[];x=z=oracle.side*20+oracle.side//2
   for elapsed in [0,100,416,832,1248,2496,20000]:
    for forward,strafe,turn in [(0,0,0),(1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(1,1,0),(0,0,1),(0,0,-1),(1,0,1),(0,1,-1)]:
     for aa,ab in [(0,0),(10000,10000)]:
      for run in [0,1]:motion.append([3,x,z,83,1,elapsed,forward,strafe,turn,run,0,aa,ab,0,1,0])
   for bob_direction in [0,1]:
    for run in [0,1]:
     motion.append([4,x,z,83,bob_direction,416,1,0,0,run,0,0,0,0,1,0,40])
     motion.append([4,x,z,83,bob_direction,416,1,0,1,run,0,0,0,0,1,0,40])
    for elapsed in [416,1248,2496]:motion.append([3,x,z,83,bob_direction,elapsed,1,0,0,1,0,10000,10000,0,1,0])
   actual=host(profile,model,motion);motion_failures=[]
   for item,cpp in zip(motion,actual):
    native=oracle.run(item)
    if native!=cpp:motion_failures.append(dict(input=item,native=native,port=cpp,translations=oracle.trace))
   row=dict(profile=profile,level=model['name'],static_vectors=len(v),static_failures=failures,wrapper_vectors=len(wrapper),wrapper_failures=wrapper_failures,motion_vectors=len(motion),motion_failures=motion_failures)
   reports.append(row)
   print(profile,model['name'],'static',len(v),len(failures),'wrapper',len(wrapper_failures),'motion',len(motion_failures),flush=True)
   (OUT/f"{profile}-{model['name']}.json").write_text(json.dumps(row,indent=2)+'\n')
 after={p:sha(ROOT/p) for p in originals};assert before==after
 report=dict(results=reports,sha256_before=before,sha256_after=after,scope='Native original instructions; clock controlled; no complete DOS session or physical input',driver_sha256=sha(Path(__file__)))
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 failures=sum(len(r[k]) for r in reports for k in ['static_failures','wrapper_failures','motion_failures'])
 print('Total mismatches',failures)
 assert failures==0

if __name__=='__main__':main()
