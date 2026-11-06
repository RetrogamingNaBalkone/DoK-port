"""Independent SP12 full-AI oracle: native actor/world/LOS/course/math/animation instructions.
Clock, event queue and audio devices are controlled external boundaries. No native
LOS, path, course, movement or animation function is replaced by a Python formula.
"""
from pathlib import Path
import sys,struct,json,subprocess,re,hashlib,argparse
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
from check_movement_oracle import Original,LAYOUT,RES,fnv
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp12-native-complete'
AI={
'ru':dict(callback=0x3845,sight=0x1276,spawn=0x136a,activate=0x3eca,math_segment=0x10c8,atan=0x1c5,animation_segment=0x1050,animation_offset=0,interval=(0x1038,0x265),audio=[(0x1108,0x1f7),(0x1108,0x2b9),(0x1108,0x298),(0x1108,0x26d)],angles_segment=0x1180,angles_offset=0xb48),
'en':dict(callback=0x3fae,sight=0x1780,spawn=0x1874,activate=0x4633,math_segment=0x1058,atan=0x1c5,animation_segment=0x1018,animation_offset=0xf0e,interval=(0x1010,0x161b),audio=[(0x1060,0x173d),(0x1060,0x17ff),(0x1060,0x17de),(0x1060,0x17b3)],angles_segment=0x10c0,angles_offset=0xb48),
'old-gold':dict(callback=0x3323,sight=0xfe1,spawn=0x10bc,activate=0x3989,math_segment=0x26f6,atan=0x185,animation_segment=0x1aa1,animation_offset=0,interval=(0x2a8d,0x266),audio=[(0x18a6,0x1f7),(0x18a6,0x29d),(0x18a6,0x280),(0x18a6,0x25d)],angles_segment=0x3000,angles_offset=0xbf02),
}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def far(addr):return ((addr>>4)<<16)|(addr&15)
def linear(ptr):return (ptr>>16)*16+(ptr&65535)
def u16(c,at):return struct.unpack('<H',c.mem_read(at,2))[0]
def s16(c,at):return struct.unpack('<h',c.mem_read(at,2))[0]
def u32(c,at):return struct.unpack('<I',c.mem_read(at,4))[0]
class AIOracle(Original):
 def __init__(self,profile,model):
  super().__init__(profile,model);self.ai=a=AI[profile];c=self.cpu;self.counts={};self.trace=[];self.coverage=set()
  folder=OUT/'ghidra'/profile;manifest=json.loads((folder/'manifest.json').read_text());self.modules={}
  for name,base,orig in [('actor',0x50000,self.layout['actor']),('world',0x60000,self.layout['world']),('runtime',0x70000,0x1000),('math',0x80000,a['math_segment'])]:
   raw=(folder/(name+'.bin')).read_bytes();assert sha(folder/(name+'.bin'))==manifest['bytes_sha256'][name];c.mem_write(base,raw);self.modules[name]=(base,orig,len(raw));self.raw[name]=raw
  af=ROOT/'research/evidence/source-port/sp11-combat'/profile;am=json.loads((af/'manifest.json').read_text());raw=(af/'animation.bin').read_bytes();assert sha(af/'animation.bin')==am['bytes_sha256']['animation'];c.mem_write(0x90000+a['animation_offset'],raw);self.modules['animation']=(0x90000,a['animation_segment'],a['animation_offset']+len(raw))
  table=list(map(int,re.findall(r'-?\d+', (ROOT/'ports/sdl2/src/ai_angles.inc').read_text().split('{{')[1].split('}}')[0])));assert len(table)==101;c.mem_write(a['angles_segment']*16+a['angles_offset'],struct.pack('<101h',*table))
  self.entrypoints={(0x5000,self.ai['callback']):'ai_callback',(0x5000,self.ai['sight']):'line_of_sight',(0x5000,self.layout['motion']):'actor_motion',(0x5000,self.layout['move']):'three_substeps',(0x6000,self.layout['resolve']):'world_resolve',(0x6000,self.layout['clear']):'footprint',(0x8000,self.ai['atan']):'native_direction',(0x9000,0x54 if self.profile=='ru' else 0xf62 if self.profile=='en' else 0x55):'animation_next',(0x9000,0xdf if self.profile=='ru' else 0xfed if self.profile=='en' else 0xcc):'animation_clamped'}
  self.defs={o['glyph']:o for o in model['objects']};self.ptrs={g:far(0xa0000+g*0x200) for g in range(256)}
  for g in range(256):c.mem_write(0x4030e+g*4,struct.pack('<I',self.ptrs[g]))
  # The original fixed glyph M slot is LEVEL's player pointer.
  assert self.hero==ord('M'),self.hero
  raw=(ROOT/RES[profile]).read_bytes();n=struct.unpack_from('<H',raw)[0];self.resources={}
  for i in range(n):at=2+i*30;name=raw[at:at+16].split(b'\0')[0].decode('cp866');size,off=struct.unpack_from('<II',raw,at+16);self.resources[name]=raw[off:off+size]
 def relocate(self,c,ip,base):c.reg_write(R.UC_X86_REG_CS,base>>4);c.reg_write(R.UC_X86_REG_IP,ip)
 def hook(self,c,address,size,unused):
  if not hasattr(self,'modules'):return super().hook(c,address,size,unused)
  cs=c.reg_read(R.UC_X86_REG_CS);ip=c.reg_read(R.UC_X86_REG_IP)
  if address in (0x5fff0,0x6fff0,0x8fff0):c.emu_stop();return
  if cs in (0x5000,0x6000,0x7000,0x8000,0x9000):
   self.coverage.add((cs,ip))
   entry=self.entrypoints.get((cs,ip))
   if entry:self.counts[entry]=self.counts.get(entry,0)+1
   if cs==0x5000 and ip==self.ai['spawn']:
    sp=c.reg_read(R.UC_X86_REG_SP);self.spawned.append(u16(c,0x30000+sp+8)&255);self.ret();return
   return
  l=self.layout;sp=c.reg_read(R.UC_X86_REG_SP)
  if address==l['clock_segment']*16+l['clock_read']:
   ptr=u32(c,0x30000+sp+4);self.ret(self.burst if linear(ptr)==self.actor+0xb0 else self.elapsed);return
  if address in (l['clock_segment']*16+l['clock_stop'],l['clock_segment']*16+l['clock_start']):
   ptr=u32(c,0x30000+sp+4)
   if linear(ptr)==self.actor+0xb0:self.burst=0
   self.ret();return
  if (cs,ip) in [(0x1038,0x108),(0x1010,0x14be),(0x2a8d,0x112)]:self.scheduled+=1;self.ret(1);return
  if (cs,ip)==self.ai['interval']:
   self.interval=u32(c,0x30000+sp+8);self.ret();return
  if (cs,ip) in self.ai['audio']:
   self.trace.append(dict(cs=cs,ip=ip,args=bytes(c.mem_read(0x30000+sp+4,12)).hex()));self.ret();return
  if (cs,ip) in [(0x1038,0x214),(0x1010,0x15ca),(0x2a8d,0x223)]:self.cancelled=True;self.ret();return
  if (cs,ip) in [(0x10c0,0x1278),(0x1050,0x15db),(0x1f1d,0xae2)]:self.ret();return
  if (cs,ip) in [(0x10c0,0x1395),(0x1050,0x16f8),(0x1f1d,0xb5b)]:self.ret();return
  # Ghidra NE selectors have no real-mode linear meaning: redirect each callee
  # into its independently exported segment while preserving its native offset.
  for name,(base,original,length) in self.modules.items():
   if self.profile!='old-gold':
    if cs==original:self.relocate(c,ip,base);return
   else:
    offset=address-original*16
    if 0<=offset<length:self.relocate(c,offset,base);return
  raise RuntimeError(f'Unmodelled AI callee {self.profile} {cs:04x}:{ip:04x} stack='+bytes(c.mem_read(0x30000+sp,32)).hex())
 def w16(self,at,value):self.cpu.mem_write(at,struct.pack('<H',value&65535))
 def w32(self,at,value):self.cpu.mem_write(at,struct.pack('<I',value&0xffffffff))
 def setup(self,v):
  op,g,board,state,frame,course,shots,burst,motion,heading,dx,dz,hp=v;self.budget=10000000 if board==9 else 100000;c=self.cpu;d=self.defs[g];side=self.side;self.elapsed=motion;self.burst=burst;self.interval=416;self.spawned=[];self.trace=[];self.scheduled=0;self.cancelled=False
  for glyph in range(256):
   data=bytearray(0x200);defn=self.defs.get(glyph);self.w16(0x4030e+glyph*4,self.ptrs[glyph]&65535);self.w16(0x40310+glyph*4,self.ptrs[glyph]>>16)
   if defn:
    struct.pack_into('<H',data,0x12,defn['class']);struct.pack_into('<H',data,0x15,defn['flags']);data[0x5c]=glyph;struct.pack_into('<h',data,0x19,defn['height'])
   c.mem_write(linear(self.ptrs[glyph]),bytes(data))
  self.actor=actor=linear(self.ptrs[g]);hero=linear(self.ptrs[self.hero]);x=z=20*side+side//2
  maps=[bytearray.fromhex(v) for v in self.model['world']['maps']]
  # inspect world maps order TMap/MMap/FCMap, already vertically normalised.
  if board<7 or board==9:
   maps=[bytearray(b' '*(self.width*self.length)) for _ in range(3)];wall=next(o['glyph'] for o in self.model['objects'] if o['class']==2)
   for row in range(self.length):
    for col in range(self.width):
     if board!=9 and(not row or not col or row==self.length-1 or col==self.width-1):maps[0][row*self.width+col]=wall
   if board==1:maps[0][22*self.width+20]=wall
   if board==3:maps[0][21*self.width+20]=wall
   if board==4:
    other=next((o['glyph'] for o in self.model['objects'] if o['class']==9 and o['glyph']!=g),None)
    if other is not None:maps[1][21*self.width+20]=other
   if board==5:
    shot=next((o['glyph'] for o in self.model['objects'] if o['class']==8),None)
    if shot is not None:maps[1][21*self.width+20]=shot
   if board==6:
    for row in range(19,24):maps[0][row*self.width+21]=wall
  else:
   maps[1]=maps[1].replace(bytes([self.hero]),b' ').replace(bytes([g]),b' ')
   placed=next((o for o in self.model['world']['actors'] if o['glyph']==g),None)
   if placed:x,z=placed['x'],placed['z']
  cell=lambda x,z:max(0,min(self.length-1,z//side))*self.width+max(0,min(self.width-1,x//side))
  hx,hz=(self.spawn['x'],self.spawn['z']) if board==8 else (x+dx*(1 if op==5 else side),z+dz*(1 if op==5 else side))
  maps[1][cell(x,z)]=g;maps[1][cell(hx,hz)]=self.hero
  for plane,source in enumerate([maps[1],maps[0],maps[2]]):c.mem_write(0x30000+self.layout['maps']+plane*4356,b' '*4356);c.mem_write(0x30000+self.layout['maps']+plane*4356,bytes(source))
  self.maps_current=maps
  for obj,px,pz,angle,height in [(actor,x,z,heading,d['height']),(hero,hx,hz,180,self.defs[self.hero]['height'])]:
   self.w32(obj+0x26,px);self.w32(obj+0x2a,-height);self.w32(obj+0x2e,pz);self.w16(obj+0x32,angle)
  fields={f[1]:f[2] for f in d['fields'] if isinstance(f[2],int)}
  for off in (0x34,0xa6,0xa8,0xae):self.w16(actor+off,fields.get(off,0))
  for off in (0x38,0xba):self.w32(actor+off,fields.get(off,0))
  for off,value in [(0xa4,hp),(0xa2,state),(0x50,frame),(0xac,course),(0xaa,shots),(0x5e,0),(0x80,1),(0x82,0)]:self.w16(actor+off,value)
  self.w16(hero+0x5e,30000);self.w16(hero+0x62,30000);self.w16(hero+0x10,0x100);self.w32(0x30000+0x114,{"ru":0x10681b10,"en":0x102020c4,"old-gold":0x1ae11694}[self.profile])
  # Source resource frames feed the *actual* animation-next/clamped instructions.
  names=[d['animation'],next(f[2] for f in d['fields'] if f[1]==0xe8),next(f[2] for f in d['fields'] if f[1]==0xec)]
  self.images={}
  for clip,name in enumerate(names):
   raw=self.resources[bytes.fromhex(name).decode('cp866')];count=struct.unpack_from('<H',raw)[0];header=0xd0000+clip*0x100;entries=0xd1000+clip*0x1000;self.w16(header,count);self.w32(header+4,far(entries));self.w32(actor+[0x48,0xe8,0xec][clip],far(header))
   for frame_id in range(count):
    interval,pa,pb=struct.unpack_from('<HHH',raw,8+frame_id*10);image=0xe0000+clip*0x2000+frame_id*0x40;self.images[far(image)]=(clip,frame_id);c.mem_write(entries+frame_id*10,struct.pack('<HHHI',interval,pa,pb,far(image)))
  self.w32(actor+0x44,next(k for k,val in self.images.items() if val==(0,0)))
  for off in (0x4c,0xf0,0xf4):self.w32(actor+off,far(0xf0000+off*16))
  return op,actor,hero
 def call(self,entry,args,segment):
  c=self.cpu
  for reg,value in [(R.UC_X86_REG_CS,segment),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_ES,0x4000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xee00),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,value)
  c.mem_write(0x3f000,struct.pack('<HH',0xfff0,segment)+args);c.emu_start(segment*16+entry,0,count=getattr(self,'budget',100000))
  assert c.reg_read(R.UC_X86_REG_IP)==0xfff0,(self.profile,entry,'native budget exhausted or no return')
  assert c.reg_read(R.UC_X86_REG_SP)==0xf004,(self.profile,entry,'native stack imbalance')
  return c.reg_read(R.UC_X86_REG_AX)
 def run(self,v):
  op,actor,hero=self.setup(v)
  if op==5:
   self.call(self.ai['activate'],struct.pack('<I',far(actor)),0x5000);return [s16(self.cpu,actor+0xa2),s16(self.cpu,actor+0x50),s16(self.cpu,actor+0xac),s16(self.cpu,actor+0xaa),self.scheduled,self.burst]
  if op==3:
   c=self.cpu;x,z=struct.unpack('<ii',c.mem_read(actor+0x26,4)+c.mem_read(actor+0x2e,4));cell=(z//self.side)*self.width+x//self.side;c.mem_write(0x30000+self.layout['maps']+cell,b' ');c.mem_write(0x4f000,struct.pack('<ii',x,z));self.call(self.layout['resolve'],struct.pack('<IIIii',0x40000000,0x4000f000,0x4000f004,x+v[8],z+v[8]),0x6000);nx,nz=struct.unpack('<ii',c.mem_read(0x4f000,8));valid=self.call(self.layout['clear'],struct.pack('<Iiih',0x40000000,nx,nz,self.radius),0x6000);initial=self.call(self.layout['clear'],struct.pack('<Iiih',0x40000000,x,z,self.radius),0x6000);return [nx,nz,valid,initial]
  if op==2:self.call(self.ai['atan'],struct.pack('<ii',v[10],v[11]),0x8000);return [self.cpu.reg_read(R.UC_X86_REG_AX)]
  self.call(self.ai['sight'] if op==1 else self.ai['callback'],struct.pack('<I',far(actor))+(struct.pack('<I',far(hero)) if op==1 else b''),0x5000)
  if op==1:return [self.cpu.reg_read(R.UC_X86_REG_AX)]
  c=self.cpu;clip,display=self.images.get(u32(c,actor+0x44),(-1,-1));mmap=bytes(c.mem_read(0x30000+self.layout['maps'],self.width*self.length))
  pose=list(struct.unpack('<iii',c.mem_read(actor+0x26,12)))
  if v[12]<=0:pose[1]=-s16(c,actor+0x19)
  if self.cancelled:self.interval=0
  return [*pose,s16(c,actor+0x32),s16(c,actor+0xa2),s16(c,actor+0x50),clip,display,s16(c,actor+0xac),s16(c,actor+0xaa),struct.unpack('<i',c.mem_read(actor+0x90,4))[0],s16(c,hero+0x5e),fnv(mmap),len(self.spawned),self.interval,self.burst]
def vectors_for(oracle,quick=False):
 model=oracle.model;monsters=[o for o in model['objects'] if o['class']==9];vectors=[]
 for d in monsters[:1] if quick else monsters:
  g=d['glyph'];f={f[1]:f[2] for f in d['fields']};period=f[0xae];limit=f[0xa6];delay=max(0,f[0xa8]);ready=min(0xffffffff,(delay+1)*10000+1)
  v=lambda board=0,state=1,frame=-1,course=0,shots=0,burst=0,motion=10000,heading=0,dx=0,dz=3,hp=100:[0,g,board,state,frame,course,shots,burst,motion,heading,dx,dz,hp]
  vectors.extend([v(),v(course=period),v(course=period-1,heading=45),v(course=32767,heading=359),v(burst=ready),v(burst=ready,shots=limit),v(burst=delay*10000),v(burst=ready,board=1),v(burst=ready,board=3),v(burst=ready,board=4),v(burst=ready,board=5),v(board=6,course=period,dx=3,dz=0),v(board=7,course=period),v(board=2,dz=1,state=1),v(board=2,dz=1,state=2,frame=0),v(board=2,dz=1,state=3,frame=0)])
  attack_name=bytes.fromhex(next(f[2] for f in d['fields'] if f[1]==0xe8)).decode('cp866');count=struct.unpack_from('<H',oracle.resources[attack_name])[0]
  vectors.extend([v(board=2,dz=1,state=2,frame=count-1),v(state=3,burst=ready,course=period),v(motion=832,heading=90,dx=-3,dz=0),v(motion=20000,heading=180,dx=0,dz=-3)])
  # The same original LOS entry also receives explicit cardinal/diagonal and
  # opaque/actor/projectile obstacle cases without invoking movement.
  for board in (0,1,3,4,5,6):
   for heading,dx,dz in [(0,0,3),(45,-3,3),(90,-3,0),(135,-3,-3),(180,0,-3),(225,3,-3),(270,3,0),(315,3,3)]:
    item=v(board=board,heading=heading,dx=dx,dz=dz);item[0]=1;vectors.append(item)
 return vectors

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--profile',choices=['all',*RES],default='all');parser.add_argument('--quick',action='store_true');parser.add_argument('--exhaust',action='store_true');parser.add_argument('--death',action='store_true');parser.add_argument('--activation',action='store_true');parser.add_argument('--angles',action='store_true');args=parser.parse_args()
 inventory=json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles'];originals=[p['sample'] for p in inventory]+list(RES.values());before={p:sha(ROOT/p) for p in originals}
 for sample in inventory:assert before[sample['sample']]==sample['sha256_before']
 reports=[]
 for profile in RES if args.profile=='all' else [args.profile]:
  models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(ROOT/RES[profile])],text=True))['levels']
  for model in models[:1] if args.quick or args.exhaust else models:
   oracle=AIOracle(profile,model);vectors=vectors_for(oracle,args.quick)
   if args.activation:
    vectors=[]
    for d in model['objects']:
     if d['class']!=9:continue
     distance=next(f[2] for f in d['fields'] if f[1]==0xba)
     for dx,dz in [(0,0),(distance-1,0),(distance,0),(distance+1,0),(-distance+1,0),(-distance,0),(0,distance-1),(0,distance),(distance-1,distance-1),(distance,distance),(distance-1,-distance+1)]:vectors.append([5,d['glyph'],0,0,-1,0,0,0,416,0,dx,dz,100])
   if args.death:
    vectors=[]
    for d in model['objects']:
     if d['class']!=9:continue
     death=bytes.fromhex(next(f[2]for f in d['fields']if f[1]==0xec)).decode('cp866');count=struct.unpack_from('<H',oracle.resources[death])[0]
     for state,frame in [(1,0),(4,0),(4,count-1),(4,count)]:vectors.append([0,d['glyph'],0,state,frame,0,0,0,416,0,0,3,0])
   if args.exhaust:
    g=next(d['glyph']for d in model['objects']if d['class']==9);vectors=[[1,g,9,1,-1,0,0,0,416,0,3,0,100]]
   if args.angles:
    g=next(d['glyph']for d in model['objects']if d['class']==9);vectors=[[2,g,0,1,-1,0,0,0,416,0,z,x,100]for z in (-30000,-1000,-101,-100,-99,-2,-1,0,1,2,99,100,101,1000,30000)for x in (-30000,-1000,-101,-100,-99,-2,-1,0,1,2,99,100,101,1000,30000)]
   assert vectors
   payload=''.join(' '.join(map(str,v))+'\n' for v in vectors);result=subprocess.run([str(ROOT/'build/sdl2/windows/dok_monster_ai_probe.exe'),str(ROOT/RES[profile]),model['name']],input=payload,capture_output=True,text=True,check=True,timeout=180)
   (OUT/(profile+'-'+model['name']+('-quick' if args.quick and not args.exhaust else '')+('-exhaust' if args.exhaust else '-death' if args.death else '-activation' if args.activation else '-angles' if args.angles else '')+'-cpp.csv')).write_text(result.stdout)
   cpp=[list(map(int,line.split(','))) for line in result.stdout.splitlines()];assert len(cpp)==len(vectors),(profile,model['name'],len(cpp),len(vectors))
   results=[]
   for i,(v,port) in enumerate(zip(vectors,cpp)):
    try:native=oracle.run(v)
    except Exception:print('FAIL',profile,model['name'],i,v,port,flush=True);raise
    row=dict(input=v,native=native,port=port,equal=native==port,accepted=native==port)
    if native!=port:
     print('DIFF',profile,model['name'],i,v,'native',native,'port',port,flush=True)
     if v[0]==0 and all(native[k]==port[k] for k in range(len(native)) if k not in (0,2)):
      c=oracle.cpu;actor=oracle.actor;at=(native[2]//oracle.side)*oracle.width+native[0]//oracle.side;c.mem_write(0x30000+oracle.layout['maps']+at,b' ')
      clear=oracle.call(oracle.layout['clear'],struct.pack('<Iiih',0x40000000,native[0],native[2],oracle.radius),0x6000)
      if clear==0:
       witness=v.copy();witness[0]=3;witness[8]=10;expected=oracle.run(witness);actual=subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_monster_ai_probe.exe'),str(ROOT/RES[profile]),model['name']],input=' '.join(map(str,witness))+'\n',text=True).strip();actual=list(map(int,actual.split(',')))
       placed=next((o for o in model['world']['actors']if o['glyph']==v[1]),None);start_x,start_z=(placed['x'],placed['z'])if v[2]==7 and placed else (20*oracle.side+oracle.side//2,20*oracle.side+oracle.side//2)
       if expected==actual and expected[2:]==[0,0] and expected[:2]==[native[0],native[2]] and [port[0],port[2]]==[start_x,start_z]:
        row.update(regression='Blanket host guard rejects original invalid-footprint candidate',witness=dict(input=witness,native=expected,port=actual))
     if v[0]==0 and v[12]<=0 and all(native[k]==port[k]for k in range(len(native))if k!=14) and 32768<=port[14]<=65535 and native[14]==(port[14]|0xffff0000):row.update(accepted=True,adaptation='Documented unsigned monster frame duration fixes original signed WORD deadline wrap')
    results.append(row)
   report=dict(profile=profile,level=model['name'],count=len(results),strict_matches=sum(v['equal']for v in results),adaptations=sum(bool(v.get('adaptation'))for v in results),mismatches=sum(not v['accepted'] for v in results),instructions=len(oracle.coverage),native_entry_visits=oracle.counts,vectors=results)
   (OUT/(profile+'-'+model['name']+('-quick' if args.quick and not args.exhaust else '')+('-exhaust' if args.exhaust else '-death' if args.death else '-activation' if args.activation else '-angles' if args.angles else '')+'-report.json')).write_text(json.dumps(report,indent=2))
   reports.append({k:v for k,v in report.items() if k!='vectors'});print(reports[-1],flush=True)
   assert not report['mismatches']
 after={p:sha(ROOT/p) for p in originals};assert before==after
 (OUT/(('quick-' if args.quick and not args.exhaust else '')+('exhaust-summary.json' if args.exhaust else 'death-summary.json' if args.death else 'activation-summary.json' if args.activation else 'angles-summary.json' if args.angles else 'report.json'))).write_text(json.dumps(dict(profiles=reports,sha256_before=before,sha256_after=after),indent=2))
if __name__=='__main__':main()
