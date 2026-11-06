"""Compose executed native projectile/player consumers with original SB backend.

Every audio query returns the actual wrapper/admission state. C++ command lists
come from actual World callbacks and run through the compiled MonoVoice probe.
"""
import json,struct,subprocess
from check_projectile_native import Native,description,BOUNDARIES
from check_movement_oracle import RES,R
from check_assets import read_archive
from native_sound import ROOT,OUT,NativeSound,CONFIG,sha
from check_sound_event_ai import normalize_port

class SoundProjectile(Native):
 def __init__(self,profile,model,defs,clips):
  self.backend=NativeSound(profile);super().__init__(profile,model,defs,clips);self.trace_sound=True
  self.resources={name.hex():raw for name,raw in read_archive((ROOT/RES[profile]).read_bytes())}
 def state(self):return (self.reverse.get(self.backend.active(),0),int(self.backend.loop()))
 def call(self,*args,**kwargs):
  if not getattr(self,'prepared',False):
   self.prepared=True;self.backend.command('stop_all');self.raw_audio=[];self.identities={};self.reverse={}
   for value in self.sound_tokens.values():
    sid=value['sound_id']
    if sid in self.identities:continue
    identity=len(self.identities)+1;self.identities[sid]=identity;self.reverse[identity]=sid
    raw=self.resources[value['resource_hex']];self.backend.install(identity,raw[36:],struct.unpack_from('<I',raw,16)[0])
   if self.initial:
    action,sid,resource=self.initial;identity=self.identities.get(sid)
    if identity is None:
     identity=len(self.identities)+1;self.identities[sid]=identity;self.reverse[identity]=sid
     raw=self.resources[resource];self.backend.install(identity,raw[36:],struct.unpack_from('<I',raw,16)[0])
    self.backend.command('loop' if action==1 else 'once',identity)
  return super().call(*args,**kwargs)
 def hook(self,c,address,size,unused):
  entries=dict(BOUNDARIES[self.profile],audio_loop=CONFIG[self.profile]['wrapper']*16+CONFIG[self.profile]['loop'])
  for kind,entry in entries.items():
   if address!=entry or not kind.startswith('audio'):continue
   sp=c.reg_read(R.UC_X86_REG_SP);pointer=0 if kind=='audio_stop_all' else struct.unpack('<I',c.mem_read(0x30000+sp+4,4))[0]
   assert kind=='audio_stop_all' or pointer in self.sound_tokens,(kind,hex(pointer))
   item=self.sound_tokens.get(pointer,{});sid=item.get('sound_id',0);action=kind[6:];before=self.state()
   result=self.backend.command('playing' if action=='isplaying' else action,self.identities.get(sid,0));after=self.state()
   self.raw_audio.append(dict(action=action,sound_id=sid,resource=item.get('resource_hex',''),before=before,after=after,native_address=hex(address)))
   self.ret(result['result']);return
  super().hook(c,address,size,unused)
 def observed(self):
  actions={'once':0,'loop':1,'stop':2,'stop_all':3}
  return [(actions[x['action']],x['sound_id'],x['resource'] if x['action']in('once','loop')else'',*x['before'][:1],*x['after']) for x in self.raw_audio if x['action']!='isplaying'and x['before']!=x['after']]

def main():
 source=ROOT/'research/evidence/source-port/sp11-native-complete/sound-traces.json';inputs=json.loads(source.read_text())['cases']
 before={p:sha(ROOT/p)for p in [*RES.values(),'originals/Old_Gold_3D/OG.EXE','originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE']};records=[]
 for profile in RES:
  models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(ROOT/RES[profile])],text=True))['levels']
  for model in models:
   cases=[x for x in inputs if x['profile']==profile and x['level']==model['name']];assert cases
   extra=[]
   for case in cases:
    if not case['label'].startswith('contact-class1-hp'):continue
    for damage in (-32768,-1,0,32767):
     vector=case['input'][:];vector[9]=damage
     extra.append(dict(profile=profile,level=model['name'],label='signed-hit-'+str(damage),input=vector,port=[]))
   if extra:
    result=subprocess.run([str(ROOT/'build/sdl2/windows/dok_projectile_native_probe.exe'),str(ROOT/RES[profile]),model['name'],'--trace'],input=''.join(' '.join(map(str,x['input']))+'\n'for x in extra),text=True,capture_output=True,check=True)
    assert len(result.stdout.splitlines())==len(extra)
    for line in result.stderr.splitlines():
     tag,index,action,sid,key=line.split(',');assert tag=='T';extra[int(index)]['port'].append(dict(action=int(action),sound_id=int(sid),resource_hex=key))
    for item in extra:
     item['native']=[dict(sound_id=x['sound_id'],resource_hex=x['resource_hex'])for x in item['port']if x['resource_hex']]
   cases+=extra
   defs,clips=description(ROOT/'build/sdl2/windows/dok_projectile_native_probe.exe',ROOT/RES[profile],model['name']);native=SoundProjectile(profile,model,defs,clips)
   payload=[];checks=[]
   for case in cases:
    candidates={x['sound_id']:x['resource_hex'] for x in case['native'] if x.get('resource_hex')}
    initial=[None]+[(0,sid,key)for sid,key in candidates.items()]
    if candidates:initial.append((1,0xdeadbeef,next(iter(candidates.values()))))
    for busy in initial:
     native.prepared=False;native.initial=busy;native.run(case['input']);commands=([dict(action=busy[0],sound_id=busy[1],resource_hex=busy[2])]if busy else[])+case['port']
     payload.append(str(len(commands))+' '+' '.join(f"{x['action']} {x['sound_id']} {x['resource_hex'] or '-'}"for x in commands))
     checks.append(dict(profile=profile,level=model['name'],label=case['label'],input=case['input'],initial=busy,native=native.raw_audio,normalized_native=native.observed(),native_state=native.state()))
   result=subprocess.run([str(ROOT/'build/sdl2/windows/dok_sound_sequence_probe.exe'),str(ROOT/RES[profile])],input='\n'.join(payload)+'\n',text=True,capture_output=True,check=True);rows=result.stdout.splitlines();assert len(rows)==len(checks)
   for item,row in zip(checks,rows):
    final,trace=row.split('|');parts=trace.split(';');trace=';'.join(parts[1:])if item['initial']else trace
    item['port_state']=tuple(map(int,final.split(',')));item['normalized_port']=normalize_port(trace);item['equal']=item['port_state']==item['native_state']and item['normalized_port']==item['normalized_native'];records.append(item)
   print(profile,model['name'],len(checks),'mismatches',sum(not x['equal']for x in checks),flush=True)
 after={p:sha(ROOT/p)for p in before};assert before==after
 report=dict(count=len(records),mismatches=sum(not r['equal']for r in records),cases=records,source_trace_sha256=sha(source),sha256_before=before,sha256_after=after)
 (OUT/'events-projectile-report.json').write_text(json.dumps(report,indent=2)+'\n');assert not report['mismatches'],next(x for x in records if not x['equal'])
if __name__=='__main__':main()
