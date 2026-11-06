"""SP11 executable original contact/effect/allocation oracle; no substituted hit results."""
from pathlib import Path
import argparse,json,struct,subprocess,itertools,hashlib,sys
from collections import Counter
from check_movement_oracle import Original,RES,R,sha
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp11-native-complete'
TABLES={
 'ru':[0x775,0x66d,0x75d,0x741,0x729,0x685,0x63d,0x625,0x655,0x60d,0x70d,0,0x6c9,0x69d],
 'en':[0x7e6,0x696,0x7c6,0x7a2,0x782,0x6b6,0x656,0x636,0x676,0x616,0x75e,0,0x70a,0x6d6],
 'old-gold':[0x6ca,0x642,0x5ca,0x5ae,0x596,0x65a,0x612,0x5fa,0x62a,0x5e2,0x57a,0,0x69e,0x672]}
BOUNDARIES={
 'ru':dict(owner_destroy=0x103df,heap_free=0x10b37,schedule=0x10488,cancel=0x10594,interval=0x105e5,audio_once=0x11277,audio_stop=0x112ed,audio_stop_all=0x11318,audio_isplaying=0x11339),
 'en':dict(owner_destroy=0x1150f,heap_free=0x10c82,profile_enter=0x10900,heap_size_accounting=0x10b8b,schedule=0x115be,cancel=0x116ca,interval=0x1171b,audio_once=0x11d3d,audio_stop=0x11db3,audio_stop_all=0x11dde,audio_isplaying=0x11dff),
 'old-gold':dict(owner_destroy=0x2a92e,heap_free=0x19a9f,schedule=0x2a9e2,cancel=0x2aaf3,interval=0x2ab36,audio_once=0x18c57,audio_stop=0x18cbd,audio_stop_all=0x18ce0,audio_isplaying=0x18cfd)}

def description(exe,res,level):
 rows=[list(map(int,line.split(',')[1:]))for line in subprocess.check_output([str(exe),str(res),level,'--describe'],text=True).splitlines()]
 definitions={};clips={}
 for row in rows:
  if len(row)==5:definitions[row[0]]=dict(cls=row[1],flags=row[2],scale=row[3],height=row[4])
  else:clips.setdefault(row[0],{}).setdefault(row[1],[]).append(row[2:])
 return definitions,clips

class Native(Original):
 def __init__(self,profile,model,definitions,clips):
  super().__init__(profile,model);self.definitions=definitions;self.clips=clips;self.called=set();self.events=[];self.trace_sound=False;self.original_objects={o['glyph']:o for o in model['objects']}
  folder=OUT/'ghidra'/profile;manifest=json.loads((folder/'manifest.json').read_text())
  for name,value in manifest['bytes_sha256'].items():assert sha(folder/(name+'.bin'))==value
  for name,address in [('objects',0x50000),('world',0x60000),('animation',0x70000)]:
   data=(folder/(name+'.bin')).read_bytes();self.cpu.mem_write(address,data)
   if name in ('objects','world'):self.raw['actor'if name=='objects'else'world']=data
  self.cpu.mem_write(0x30400,(folder/'vtables.bin').read_bytes())
  self.copy=0x1347 if profile=='old-gold'else 0x5cd5;self.cpu.mem_write(0x80000+self.copy,(folder/'copy-stack.bin').read_bytes())
  if profile=='old-gold':self.cpu.mem_write(0xe0000,(folder/'reclaim-flag.bin').read_bytes())
  self.animation_segment=0x1aa1 if profile=='old-gold'else 0x1050 if profile=='ru'else 0x1018
  self.animation_base=0xf0e if profile=='en'else 0
  for glyph,d in definitions.items():
   self.cpu.mem_write(0x41010+glyph*0x80,struct.pack('<H',TABLES[profile][d['cls']]))
   self.cpu.mem_write(0x41019+glyph*0x80,struct.pack('<h',d['height']))
  self.cpu.mem_write(0x4d012,struct.pack('<H',1))
 def hook(self,c,address,size,unused):
  cs=c.reg_read(R.UC_X86_REG_CS)
  if cs in (0x7000,0x8000,0xe000):return
  if self.profile=='old-gold'and address==0x24745:c.reg_write(R.UC_X86_REG_CS,0xe000);c.reg_write(R.UC_X86_REG_IP,0);return
  if address==0x10000+self.copy:c.reg_write(R.UC_X86_REG_CS,0x8000);c.reg_write(R.UC_X86_REG_IP,self.copy);return
  if cs==self.animation_segment:
   c.reg_write(R.UC_X86_REG_CS,0x7000);c.reg_write(R.UC_X86_REG_IP,c.reg_read(R.UC_X86_REG_IP)-self.animation_base);return
  if self.profile=='old-gold'and 0x1aa10<=address<0x1ab52:
   c.reg_write(R.UC_X86_REG_CS,0x7000);c.reg_write(R.UC_X86_REG_IP,address-0x1aa10);return
  # Only hardware/host services are boundaries. Every class event filter and
  # stat/monster hit routine is reached through the original vtable and executed.
  services=BOUNDARIES[self.profile]
  if address in services.values():
   kind=next(k for k,v in services.items()if v==address);self.called.add(kind);self.service_counts[kind]+=1;sp=c.reg_read(R.UC_X86_REG_SP)
   if kind=='interval':self.interval=struct.unpack('<I',c.mem_read(0x30000+sp+8,4))[0]
   elif kind=='cancel':self.cancelled=True
   elif kind.startswith('audio'):
    self.audio.append(kind)
    if self.trace_sound:
     pointer=struct.unpack('<I',c.mem_read(0x30000+sp+4,4))[0]if kind!='audio_stop_all'else 0
     self.audio_calls.append(dict(kind=kind,pointer=pointer,**self.sound_tokens.get(pointer,{})))
   elif kind=='schedule':self.events.append(bytes(c.mem_read(0x30000+sp+4,16)).hex())
   self.ret(1 if kind=='schedule'else 0);return
  # HUD notification is presentation; original hit arithmetic still executes.
  if (self.profile=='ru'and address in(0x1d0c7,0x11f95))or(self.profile=='en'and address in(0x14957,0x11bf8))or(self.profile=='old-gold'and address in(0x1fd2b,0x1fd42,0x1fd68,0x1fd7f)):
   self.called.add('hud');self.ret();return
  super().hook(c,address,size,unused)
 def setup_clips(self,glyph):
  c=self.cpu;self.images={};self.image_hashes={}
  for kind,frames in self.clips[glyph].items():
   header=0x92000+kind*0x100;entries=0x93000+kind*0x400;c.mem_write(header,struct.pack('<HHI',len(frames),0,0x90000000+(entries-0x90000)))
   for frame in frames:
    index,interval,a,b,width,height,pixels_hash=frame;image=0x95000+kind*0x2000+index*0x20;self.images[0x90000000+(image-0x90000)]=(kind,index);self.image_hashes[0x90000000+(image-0x90000)]=pixels_hash
    c.mem_write(entries+index*10,struct.pack('<HHHI',interval,a,b,0x90000000+(image-0x90000)))
    c.mem_write(image,struct.pack('<HHH',width,height,0)+bytes(12))
 def run(self,v):
  op,glyph,target,source,flags,hp,state,heading,speed,damage,elapsed,frame,plane,height,actor_impact,steps=v
  if op==2:return self.allocate(v)
  c=self.cpu;og=self.profile=='old-gold';self.setup_clips(glyph);self.reset_maps(empty=True);self.trace=[];self.elapsed=elapsed
  d=self.definitions[glyph];td=self.definitions[target];self.interval=416;self.cancelled=False;self.audio=[];self.events=[];self.service_counts=Counter();self.audio_calls=[];self.sound_tokens={}
  slot=next(x for x in range(256)if x not in self.definitions);self.slot=slot
  for key in self.definitions:
   off=0x1000+key*0x80;c.mem_write(0x4030e+key*4,struct.pack('<HH',off,0x4000))
  c.mem_write(0x4030e+target*4,struct.pack('<HH',0xc000,0x4000));c.mem_write(0x4030e+slot*4,struct.pack('<HH',0xe000,0x4000))
  c.mem_write(0x4073c,struct.pack('<H',1))
  for area in (0x4c000,0x4d000,0x4e000):c.mem_write(area,bytes(0x200))
  body=bytearray(0x200)
  for at,fmt,value in [(0x10,'H',TABLES[self.profile][td['cls']]),(0x12,'H',td['cls']),(0x14,'B',target),(0x15,'H',flags),(0x19,'h',height),(0x26,'i',10000),(0x2e,'i',10000),(0x54,'B',state if td['cls']==7 else 0),(0x5e,'h',hp),(0x62,'h',100),(0xa2,'h',state),(0xa4,'h',hp),(0xae,'h',4)]:struct.pack_into('<'+fmt,body,at,value)
  c.mem_write(0x4c000,bytes(body));c.mem_write(0x4d012,struct.pack('<H',source));c.mem_write(0x30000+self.layout['global_world'],struct.pack('<HH',0,0x4000))
  c.mem_write(0x30000+self.layout['maps']+(4356 if plane else 0),bytes([target])*(self.width*self.length))
  body=bytearray(0x200);selected=1 if actor_impact else 2
  for at,fmt,value in [(0x10,'H',TABLES[self.profile][8]),(0x12,'H',8),(0x14,'B',slot),(0x15,'H',d['flags']|0x1000),(0x17,'h',d['scale']),(0x19,'h',d['height']),(0x26,'i',10000),(0x2a,'i',-250),(0x2e,'i',10000),(0x32,'h',heading),(0x3c,'i',speed),(0x44,'I',0x90005000+(selected*0x2000 if op==1 else 0)+frame*0x20),(0x48,'I',0x90002000),(0x50,'h',frame),(0x52,'H',self.clips[glyph][selected if op==1 and not og else 0][frame][1]),(0x58,'h',1 if op==1 else 0),(0x5a if og else 0x5c,'h',damage),(0x5c if og else 0x5e,'h',damage//2),(0x67 if og else 0x69,'I',0x4000d000),(0x77,'I',0x90002100),(0x7b,'I',0x90002200),(0x7f,'I',0x90002000+selected*0x100)]:struct.pack_into('<'+fmt,body,at,value)
  c.mem_write(0x4e000,bytes(body))
  if self.trace_sound:
   def bind(address,object_glyph,owner):
    original=self.original_objects[object_glyph];values=[(0x4c,original['sound'])]+[(offset,value)for kind,offset,value in original['fields']if kind==5]
    for binding,resource in values:
     if not resource:continue
     pointer=0x9000b000+len(self.sound_tokens)*0x80;self.sound_tokens[pointer]=dict(resource_hex=resource,owner=owner,binding=binding,sound_id=(owner<<16)|binding)
     c.mem_write(address+binding,struct.pack('<I',pointer))
   bind(0x4c000,target,next(i+1 for i,o in enumerate(self.model['objects'])if o['glyph']==target));bind(0x4e000,glyph,0x10000+slot)
  if og and op==1:c.mem_write(0x4e050,struct.pack('<h',frame))
  for step in range(steps):
   if self.cancelled:break
   self.call(0x22fa if og else 0x26e8 if self.profile=='ru'else 0x2d8b,struct.pack('<I',0x4000e000),0x5000)
  if op==3:
   assert self.cancelled,"Native effect did not reach cancellation"
   status=struct.unpack('<H',c.mem_read(0x4e015,2))[0];assert status&0x400 and not status&0x1000,"Native completion/reclaim flags"
   assert struct.unpack('<H',c.mem_read(0x4073c,2))[0]==0,"Native active count after completion"
   return self.allocate_existing()
  b=c.mem_read(0x4e000,0x200);target_data=c.mem_read(0x4c000,0x200)
  health=struct.unpack_from('<h',target_data,0x5e if td['cls']==1 else 0xa4)[0]if td['cls']in(1,9)else 0
  ms=struct.unpack_from('<h',target_data,0xa2)[0]if td['cls']==9 else 0
  active=not self.cancelled
  if active:
   pos=list(struct.unpack_from('<iii',b,0x26));dam=struct.unpack_from('<h',b,0x5a if og else 0x5c)[0];cursor=struct.unpack_from('<h',b,0x50)[0];impact=cursor>0 if og else struct.unpack_from('<h',b,0x58)[0]>0
   ptr=struct.unpack_from('<I',b,0x44)[0];clip,shown=self.images[ptr];effect=op==1 or(impact and steps>1);actor=impact and(td['cls']in(1,9)if op==0 else bool(actor_impact))
   # Normalize the retained flight image independently of animation cursor.
   display=shown;cell=max(0,min(self.length-1,pos[2]//self.side))*self.width+max(0,min(self.width-1,pos[0]//self.side));occupied=c.mem_read(0x30000+self.layout['maps']+cell,1)[0]==slot
   result=[1,*pos,dam,cursor,int(impact),int(effect),int(actor),int(occupied)]
  else:
   result=[0]+[0]*9;status=struct.unpack_from('<H',b,0x15)[0];assert status&0x400 and not status&0x1000,"Missing native completion flags"
  self.last_native_details=dict(flags=struct.unpack_from('<H',b,0x15)[0],cursor=struct.unpack_from('<h',b,0x50)[0],active_count=struct.unpack('<H',c.mem_read(0x4073c,2))[0],services=dict(self.service_counts))
  return result+[health,ms,int(active),self.interval if active else 0,len([x for x in self.audio if x=='audio_once']),len([x for x in self.audio if x=='audio_stop']),self.image_hashes[ptr]if active else 0]

 def allocate_existing(self):
  entry=0x1d4 if self.profile=='old-gold'else 0x17d if self.profile=='ru'else 0x1ea
  raw=self.call(entry,struct.pack('<I',0x40000000),0x6000);slot=raw if raw<32768 else raw-65536
  count=struct.unpack('<H',self.cpu.mem_read(0x4073c,2))[0]
  self.last_native_details=dict(selected_slot=slot,active_count=count,services=dict(self.service_counts),reclaimed=bool(self.service_counts['heap_free']))
  return [slot,count]
 def allocate(self,v):
  op,glyph,target,source,flags,hp,state,heading,speed,damage,elapsed,candidate,mode,height,actor_impact,steps=v;c=self.cpu
  self.cancelled=False;self.interval=416;self.audio=[];self.events=[];self.service_counts=Counter()
  for slot in range(256):
   off=0x1000+slot*0x80;c.mem_write(0x40000+off,bytes(0x80));c.mem_write(0x40000+off+0x10,struct.pack('<HH',TABLES[self.profile][8],8));c.mem_write(0x40000+off+0x15,struct.pack('<H',0x1000));c.mem_write(0x4030e+slot*4,struct.pack('<HH',off,0x4000))
  count=256-len(self.definitions)
  if candidate>=0:
   if mode==0:c.mem_write(0x4030e+candidate*4,bytes(4))
   else:
    c.mem_write(0x4e000,bytes(0x200));c.mem_write(0x4e010,struct.pack('<HH',TABLES[self.profile][8],8));c.mem_write(0x4e015,struct.pack('<H',0x400));c.mem_write(0x4030e+candidate*4,struct.pack('<HH',0xe000,0x4000))
   count-=1
  c.mem_write(0x4073c,struct.pack('<H',count));return self.allocate_existing()

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--profile',choices=tuple(TABLES));parser.add_argument('--limit',type=int,default=0);args=parser.parse_args();OUT.mkdir(parents=True,exist_ok=True)
 before={p:sha(ROOT/p)for p in list(RES.values())+['originals/Old_Gold_3D/OG.EXE','originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE']};reports=[]
 for profile in ([args.profile]if args.profile else TABLES):
  res=ROOT/RES[profile];models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(res)],text=True))['levels']
  for model in models:
   exe=ROOT/'build/sdl2/windows/dok_projectile_native_probe.exe';defs,clips=description(exe,res,model['name']);native=Native(profile,model,defs,clips)
   vectors=[];tags=[]
   def add(v,tag):vectors.append(v);tags.append(tag)
   representatives={}
   for g,d in defs.items():representatives.setdefault(d['cls'],g)
   for glyph in clips:
    for target,td in defs.items():
     for source in [1,9]:
      if td['cls'] not in(0,1,2,3,4,6,7,8,9,10,12,13):continue
      add([0,glyph,target,source,td['flags'],7,2 if td['cls']==7 else 1,0,-120,3,416,0,1 if td['cls']in(0,2,3,4,6,7,10,12,13)else 0,td['height'],0,1],'all-resource-contact')
    for cls,target in representatives.items():
     td=defs[target]
     if cls==8:continue
     plane=0 if cls in(1,9)else 1
     base=[0,glyph,target,9 if cls==1 else 1,td['flags'],7,2 if td['cls']==7 else 1,0,-120,3,416,0,plane,td['height'],0,1]
     for heading,elapsed in itertools.product([0,44,90,180,270,359],[0,416,832,3328]):
      v=base[:];v[7]=heading;v[10]=elapsed;add(v,'angle-elapsed-contact')
     for height in [-32768,-1,0,1,200,32767]:
      v=base[:];v[13]=height;add(v,'impact-height')
     if cls==7:
      for flags,state in itertools.product([0,8,16,24,64,72,80],[1,2]):
       v=base[:];v[4]=flags;v[6]=state;add(v,'door-filter')
     if cls in(1,9):
      for hp,state,damage in itertools.product([0,1,7,100,-1,32767,-32768],range(5)if cls==9 else [0],[0,1,7,32767,-32768]):
       v=base[:];v[5]=hp;v[6]=state;v[9]=damage;add(v,'class-hit-signed-state')
    for actor in [0,1]:
     target=representatives[9 if actor else 2];td=defs[target];kind=1 if actor else 2
     base=[0,glyph,target,1,td['flags'],100,1,0,-120,1,416,0,0 if actor else 1,td['height'],0,1]
     for steps in range(1,len(clips[glyph][kind])+4):
      v=base[:];v[15]=steps;add(v,'contact-through-effect-release')
     target=representatives[0];td=defs[target]
     for frame,steps in itertools.product(range(1 if profile=='old-gold'else 0,len(clips[glyph][kind])),[1,2,len(clips[glyph][kind])+1]):
      v=[1,glyph,target,1,td['flags'],7,2 if td['cls']==7 else 1,0,-120,3,416,frame,1,td['height'],actor,steps];add(v,'effect-tail')
   free_slots=[g for g in range(256)if g not in defs]
   for glyph in clips:
    floor=representatives[0];td=defs[floor]
    for candidate,mode in [(-1,0),*[(slot,mode)for slot in free_slots for mode in (0,1)]]:
     add([2,glyph,floor,1,td['flags'],7,1,0,-120,3,416,candidate,mode,td['height'],0,1],'allocation-null-reclaim-full')
    for actor in (0,1):
     target=representatives[9 if actor else 2];td=defs[target]
     add([3,glyph,target,1,td['flags'],100,1,0,-120,1,416,0,0 if actor else 1,td['height'],0,len(clips[glyph][1 if actor else 2])+4],'effect-cancel-native-reclaim-port-respawn')
   if args.limit:vectors=vectors[:args.limit];tags=tags[:args.limit]
   cpp=subprocess.run([str(exe),str(res),model['name']],input=''.join(' '.join(map(str,v))+'\n'for v in vectors),capture_output=True,text=True,check=True);port=[list(map(int,line.split(',')))for line in cpp.stdout.splitlines()];assert len(port)==len(vectors),(len(port),len(vectors))
   rows=[]
   for v,expected,tag in zip(vectors,port,tags):
    try:actual=native.run(v)
    except Exception:
     print('ERROR',profile,model['name'],v,hex(native.cpu.reg_read(R.UC_X86_REG_CS)),hex(native.cpu.reg_read(R.UC_X86_REG_IP)),native.called,flush=True);raise
    # Audio identity/policy has separate SP13 acceptance; retain calls as evidence.
    equal=(actual==[expected[0],expected[2]])if v[0]in(2,3)else(actual[:14]==expected[:14]and actual[16]==expected[16]);rows.append(dict(kind=tag,input=v,native=actual,port=expected,equal=equal,native_details=native.last_native_details))
   mismatches=[r for r in rows if not r['equal']];print(profile,model['name'],len(rows),len(mismatches),mismatches[:2],flush=True)
   report=dict(profile=profile,level=model['name'],count=len(rows),mismatches=len(mismatches),vectors=rows,services=sorted(native.called),case_groups=dict(Counter(tags)),resource_classes=sorted(representatives),static_reclaimable_definitions=[g for g,d in defs.items()if d['flags']&0x400],comparison='Alive/pose/signed damage/raw cursor/impact/effect/actor effect/occupancy/target hp+state/events/interval and actual image pixel hash; allocation selected slot+active count; audio columns recorded but accepted separately in SP13');(OUT/f'{profile}-{model["name"]}.json').write_text(json.dumps(report,indent=2),'utf8');reports.append({k:v for k,v in report.items()if k!='vectors'})
 after={p:sha(ROOT/p)for p in before};assert before==after;(OUT/'report.json').write_text(json.dumps(dict(checks=reports,sha256_before=before,sha256_after=after),indent=2),'utf8');assert not any(r['mismatches']for r in reports)
if __name__=='__main__':main()
