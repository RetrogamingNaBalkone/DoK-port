"""Typed original-instruction projectile sound consumers for SP13 native backend replay."""
import json,subprocess
from check_projectile_native import ROOT,OUT,TABLES,RES,Native,description,sha

def main():
 result=[];before={p:sha(ROOT/p)for p in [*RES.values(),'originals/Old_Gold_3D/OG.EXE','originals/pkr/RUS/PKR.EXE','originals/pkr/ENG/PKE.EXE']}
 for profile in TABLES:
  res=ROOT/RES[profile];models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(res)],text=True))['levels']
  for model in models:
   exe=ROOT/'build/sdl2/windows/dok_projectile_native_probe.exe';defs,clips=description(exe,res,model['name']);native=Native(profile,model,defs,clips);native.trace_sound=True
   reps={}
   for g,d in defs.items():reps.setdefault(d['cls'],g)
   vectors=[];labels=[]
   for glyph in clips:
    for cls in (1,2,9):
     target=reps[cls];td=defs[target]
     for hp in ([100,99,95,7,0]if cls==1 else [100]):
      vectors.append([0,glyph,target,9 if cls==1 else 1,td['flags'],hp,1,0,-120,1,416,0,0 if cls in(1,9)else 1,td['height'],0,1]);labels.append('contact-class'+str(cls)+'-hp'+str(hp))
    for actor in (0,1):
     target=reps[0];td=defs[target];kind=1 if actor else 2;vectors.append([1,glyph,target,1,td['flags'],100,1,0,-120,1,416,1 if profile=='old-gold'else 0,1,td['height'],actor,len(clips[glyph][kind])+3]);labels.append('effect-cancel-'+str(actor))
   cpp=subprocess.run([str(exe),str(res),model['name'],'--trace'],input=''.join(' '.join(map(str,v))+'\n'for v in vectors),capture_output=True,text=True,check=True)
   calls={i:[]for i in range(len(vectors))}
   for line in cpp.stderr.splitlines():
    if not line.startswith('T,'):raise RuntimeError(line)
    _,case,action,owner,resource=line.split(',');calls[int(case)].append(dict(action=int(action),sound_id=int(owner),resource_hex=resource))
   for i,(v,label) in enumerate(zip(vectors,labels)):
    native.run(v);result.append(dict(profile=profile,level=model['name'],label=label,input=v,native=native.audio_calls,port=calls[i]))
 after={p:sha(ROOT/p)for p in before};assert before==after
 path=OUT/'sound-traces.json';path.write_text(json.dumps(dict(cases=result,sha256_before=before,sha256_after=after,boundary='Actual native contact/hit/effect consumers execute. Audio wrapper calls captured with typed owner/resource; audio_isplaying controlled idle at this boundary. SP13 replays requests through real native sound wrappers/admission/driver.'),indent=2),'utf8');print(path,len(result))
if __name__=='__main__':main()
