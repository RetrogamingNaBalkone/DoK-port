"""Original fire, pickup, door, generic stat and death consumer instructions.

Scheduling, notifications and the spawn-return cost are explicit fixture inputs;
the original audio wrappers/admission/SB backend always execute, including queries.
"""
import argparse,json,struct,subprocess
from collections import Counter
from check_projectile_native import description,TABLES
from check_sound_event_projectiles import SoundProjectile
from check_sound_event_ai import normalize_port
from check_movement_oracle import RES,R
from check_flow_oracle import Oracle as FlowOracle
from native_sound import ROOT,OUT,sha,NativeSound,CONFIG

class SoundFlow(FlowOracle):
 def hook(self,c,address,size,unused):
  if address==0x50000+self.settings['entry']:c.mem_write(0x910a6,struct.pack('<I',0x9000b000))
  cfg=CONFIG[self.profile]
  for action in ('once','stop_all'):
   if address!=cfg['wrapper']*16+cfg[action]:continue
   before=self.state();self.backend.command(action,1 if action=='once'else 0);after=self.state();self.raw_audio.append(dict(action=action,sound_id=self.sid if action=='once'else 0,resource=self.key if action=='once'else'',before=before,after=after,native_address=hex(address)))
   sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4));c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip);return
  super().hook(c,address,size,unused)
 def state(self):return(self.sid if self.backend.active()==1 else 0xdeadbeef if self.backend.active()==2 else 0,int(self.backend.loop()))
 def sound_run(self,hp,lives,key,sid,initial,raw):
  self.backend=NativeSound(self.profile);self.key=key;self.sid=sid;self.raw_audio=[]
  for identity in(1,2):self.backend.install(identity,raw[36:],struct.unpack_from('<I',raw,16)[0])
  if initial:self.backend.command('loop'if initial[0]==1 else'once',1 if initial[1]==sid else 2)
  self.run(hp,lives,0);assert self.cpu.reg_read(R.UC_X86_REG_SP)==0xf004,'Death callback stack imbalance';return self.raw_audio,self.state()

class Consumer(SoundProjectile):
 def hook(self,c,address,size,unused):
  if getattr(self,'mode',-1)==2 and address==0x50000+{'ru':0x136a,'en':0x1874,'old-gold':0x10bc}[self.profile]:
   self.ret(self.cost&65535);return
  if address in (0x11e78,0x11adb,0x11fb6,0x11c19,0x1fcb2,0x1fd2b,0x1fd42,0x1fd68,0x1fd7f):self.ret();return
  previous=len(getattr(self,'raw_audio',[]));super().hook(c,address,size,unused)
  if getattr(self,'mode',-1)==2 and len(getattr(self,'raw_audio',[]))>previous:
   self.raw_audio[-1]['charge']=struct.unpack('<h',c.mem_read(0x4d060,2))[0]
 def run_case(self,mode,glyph,x,y,z,initial):
  if mode==3:
   original=self.original_objects[glyph];key=next(v for k,o,v in original['fields']if o==0xa6);owner=next(i+1 for i,o in enumerate(self.model['objects'])if o['glyph']==glyph);sid=(owner<<16)|0xa6
   flow=SoundFlow(self.profile);self.raw_audio,final=flow.sound_run(x,y,key,sid,initial,self.resources[key]);self.entry=flow.settings['entry'];self.charge=100
   return self.observed(),final
  self.mode=mode;self.cost=z;self.prepared=False;self.initial=initial;self.trace=[];self.elapsed=0;self.service_counts=Counter();self.called=set();self.audio=[];self.audio_calls=[];self.events=[];self.cancelled=False
  c=self.cpu;c.mem_write(0x4c000,bytes(0x4000));self.sound_tokens={};self.interval=7
  actor=0x4c000;hero=0x4d000;weapon=0x4e000;obj=self.original_objects[glyph];hg=next(o['glyph']for o in self.model['objects']if o['class']==1)
  def word(at,value):c.mem_write(at,struct.pack('<H',value&65535))
  def dword(at,value):c.mem_write(at,struct.pack('<I',value))
  def handler(cls):
   ip,cs=struct.unpack('<HH',c.mem_read(0x30000+TABLES[self.profile][cls]+0x14,4))
   return cs*16+ip-self.layout['actor']*16 if self.profile=='old-gold'else ip
  def bind(at,g):
   original=self.original_objects[g];owner=next(i+1 for i,o in enumerate(self.model['objects'])if o['glyph']==g)
   for binding,resource in [(0x4c,original['sound'])]+[(offset,value)for kind,offset,value in original['fields']if kind==5]:
    if not resource:continue
    pointer=0x9000b000+len(self.sound_tokens)*0x80;self.sound_tokens[pointer]=dict(resource_hex=resource,owner=owner,binding=binding,sound_id=(owner<<16)|binding);dword(at+binding,pointer)
   word(at+0x12,original['class']);word(at+0x10,TABLES[self.profile][original['class']])
  bind(actor,glyph);bind(hero,hg)
  for kind,offset,value in obj['fields']:
   if kind in(0,1,2):word(actor+offset,value)
   elif kind==3:dword(actor+offset,value&0xffffffff)
  word(hero+0x5e,100);word(hero+0x62,100);word(hero+0x60,x if mode==2 else 100);word(hero+0x64,100)
  dword(0x30000+self.layout['global_world'],0x40000000)
  if self.profile=='old-gold':dword(0x3255a,1)
  if mode==0:
   word(actor+0x50,y);word(actor+0x52,7);word(actor+0x56,0);word(actor+0x60,30);word(actor+0x6a,0x2442);c.mem_write(actor+0x54,bytes([x,1]));dword(actor+0x66,40000)
   if z==4:
    word(hero+0x72,0);dword(0x4f000,0x800);offset=handler(7);args=struct.pack('<III',0x4000c000,0x4000f000,0x4000d000)
   else:offset={'old-gold':(0x2cdf,0x2dbb,0x2e8e),'ru':(0x3262,0x334e,0x3427),'en':(0x3917,0x3a03,0x3adc)}[self.profile][z-1];args=struct.pack('<I',0x4000c000)
  elif mode==1:offset={'ru':0x2f8a,'en':0x362d,'old-gold':0x29f3}[self.profile];args=struct.pack('<I',0x4000c000)
  elif mode==2:
   # Full original player event handler and weapon readiness callback execute;
   # allocation result is a controlled input, independently covered in SP11.
   dword(hero+0xaa,0x4000c000);word(hero+0xce,0);word(actor+0x83,y);dword(0x4f000,0x200)
   offset=handler(1);args=struct.pack('<III',0x4000d000,0x4000f000,0x4000c000)
  elif mode==4:
   word(hero+0x5e,x);offset={'ru':0x1cc2,'en':0x2276,'old-gold':0x18b9}[self.profile];args=struct.pack('<Ihh',0x4000d000,0,y)
  else:raise ValueError(mode)
  self.entry=offset;self.call(offset,args,0x5000)
  self.charge=struct.unpack('<h',c.mem_read(hero+0x60,2))[0]
  return self.observed(),self.state()

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--record-differences',action='store_true');parser.add_argument('--profile',choices=list(RES));args=parser.parse_args();allrows=[]
 before={p:sha(ROOT/p)for p in [*RES.values(),'originals/Old_Gold_3D/OG.EXE','originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE']}
 for profile in ([args.profile]if args.profile else RES):
  models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(ROOT/RES[profile])],text=True))['levels']
  for model in models:
   defs,clips=description(ROOT/'build/sdl2/windows/dok_projectile_native_probe.exe',ROOT/RES[profile],model['name']);oracle=Consumer(profile,model,defs,clips);cases=[]
   for obj in model['objects']:
    glyph=obj['glyph'];cls=obj['class'];fields={x[1]:x[2]for x in obj['fields']}
    if cls==7:
     cases += [(0,glyph,state,phase,event)for event,state,phase in [(1,2,0),(1,2,29),(2,1,29),(2,1,0),(3,1,29)]]
     if fields.get(0x5c,0):cases.append((0,glyph,2,0,4))
    if cls in(3,10):cases.append((1,glyph,1,0,0))
    if cls==10:cases += [(2,glyph,charge,state,cost)for charge in(0,1,100)for state in(0,3,7)for cost in(0,1,3,-32768)]
   hg=next(o['glyph']for o in model['objects']if o['class']==1);cases += [(4,hg,hp,delta,0)for hp in(0,1,99)for delta in(-5,0,5)]+[(3,hg,hp,lives,0)for hp in(-1,0,1)for lives in(0,1,2)]
   result=subprocess.run([str(ROOT/'build/sdl2/windows/dok_sound_consumer_probe.exe'),str(ROOT/RES[profile]),model['name']],input=''.join(' '.join(map(str,v))+'\n'for v in cases),text=True,capture_output=True,check=True);rows=result.stdout.splitlines();assert len(rows)==len(cases)
   checks=[];payload=[]
   for case,row in zip(cases,rows):
    charge,trace=row.split('|');commands=[]
    for item in trace.split(';'):
     if item:
      action,sid,key,observed_charge=item.split(',');commands.append(dict(action=int(action),sound_id=int(sid),resource_hex=key,charge=int(observed_charge)))
    obj=oracle.original_objects[case[1]];owner=next(i+1 for i,o in enumerate(model['objects'])if o['glyph']==case[1]);key=next(v for k,o,v in obj['fields']if o==0xa6)if case[0]==3 else obj['sound'];binding=0xa6 if case[0]==3 else 0x4c
    if case[0]==4:initials=[None]
    else:initials=[None]+([(0,(owner<<16)|binding,key),(1,0xdeadbeef,key)]if key else[])
    for initial in initials:
     try:native,final=oracle.run_case(*case,initial)
     except Exception:print('NATIVEFAIL',profile,model['name'],case,initial,hex(oracle.cpu.reg_read(R.UC_X86_REG_CS)),hex(oracle.cpu.reg_read(R.UC_X86_REG_IP)),flush=True);raise
     combined=([dict(action=initial[0],sound_id=initial[1],resource_hex=initial[2])]if initial else[])+commands
     payload.append(str(len(combined))+' '+' '.join(f"{x['action']} {x['sound_id']} {x['resource_hex']or'-'}"for x in combined))
     checks.append(dict(profile=profile,level=model['name'],input=case,initial=initial,native=oracle.raw_audio,normalized_native=native,native_state=final,native_entry=hex(oracle.entry),port_commands=commands,port_charge=int(charge),native_charge=oracle.charge))
   output=subprocess.run([str(ROOT/'build/sdl2/windows/dok_sound_sequence_probe.exe'),str(ROOT/RES[profile])],input='\n'.join(payload)+'\n',text=True,capture_output=True,check=True).stdout.splitlines();assert len(output)==len(checks)
   for item,row in zip(checks,output):
    final,trace=row.split('|');trace=';'.join(trace.split(';')[1:])if item['initial']else trace;item['normalized_port']=normalize_port(trace);item['port_state']=tuple(map(int,final.split(',')));item['equal']=item['port_state']==item['native_state']and item['normalized_native']==item['normalized_port']
    if item['input'][0]==2:
     item['native_audio_charge']=[x['charge']for x in item['native']if x['action']=='once'];item['port_audio_charge']=[x['charge']for x in item['port_commands']if x['action']==0]
     item['equal']=item['equal']and item['native_audio_charge']==item['port_audio_charge']and item['native_charge']==item['port_charge']
    allrows.append(item)
   print(profile,model['name'],len(checks),'differences',sum(not r['equal']for r in checks),flush=True)
   if any(not r['equal']for r in checks)and not args.record_differences:print(next(r for r in checks if not r['equal']),flush=True);break
 after={p:sha(ROOT/p)for p in before};assert before==after
 report=dict(count=len(allrows),mismatches=sum(not r['equal']for r in allrows),cases=allrows,sha256_before=before,sha256_after=after)
 (OUT/'events-consumers-report.json').write_text(json.dumps(report,indent=2)+'\n');assert args.record_differences or not report['mismatches']
if __name__=='__main__':main()

