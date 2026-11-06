"""Repeated native/C++ callbacks for exact embedded-footprint witnesses.
The unmodified original map and original player spawn are used; release scenarios
explicitly make the supporting terrain passable on callback3 as a controlled
activation experiment, not a claimed walkthrough input.
"""
from pathlib import Path
import json,subprocess,struct,argparse
from check_monster_ai_oracle import ROOT,OUT,RES,AIOracle,far,sha,s16,u32,fnv

def snapshot(o,v,actor,hero):
 c=o.cpu;clip,frame=o.images.get(u32(c,actor+0x44),(-1,-1));mmap=bytes(c.mem_read(0x30000+o.layout['maps'],o.width*o.length))
 return [*struct.unpack('<iii',c.mem_read(actor+0x26,12)),s16(c,actor+0x32),s16(c,actor+0xa2),s16(c,actor+0x50),clip,frame,s16(c,actor+0xac),s16(c,actor+0xaa),struct.unpack('<i',c.mem_read(actor+0x90,4))[0],s16(c,hero+0x5e),fnv(mmap),len(o.spawned),o.interval,o.burst]
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--before',action='store_true');args=parser.parse_args()
 baseline=json.loads((OUT/'baseline-live-regressions.json').read_text())['cases'];reports=[];before={p:sha(ROOT/p)for p in RES.values()}
 for profile,res in RES.items():
  models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(ROOT/res)],text=True))['levels']
  for model in models:
   cases=[v for v in baseline if v['profile']==profile and v['level']==model['name']]
   for witness in cases:
    for release in (-1,3):
     v=witness['input'].copy();v[0]=4 if release<0 else 7;v[2]=8;v[7]=0;v[8]=416;v[6]=32767
     rounds=12;cppcmd=[str(ROOT/'build/sdl2/windows/dok_monster_ai_probe.exe'),str(ROOT/res),model['name']];payload=' '.join(map(str,v))+f' {rounds}'+(f' {release}'if release>=0 else '')+'\n'
     result=subprocess.run(cppcmd,input=payload,capture_output=True,text=True,check=True,timeout=30);cpp=[list(map(int,line.split(',')))for line in result.stdout.splitlines()];assert len(cpp)==rounds
     o=AIOracle(profile,model);_,actor,hero=o.setup(v);native=[];initial=struct.unpack('<iii',o.cpu.mem_read(actor+0x26,12));terrain=o.maps_current[0][initial[2]//o.side*o.width+initial[0]//o.side]
     for step in range(rounds):
      if step==release:o.w16((o.ptrs[terrain]>>16)*16+(o.ptrs[terrain]&65535)+0x15,o.defs[terrain]['flags']|0x10)
      prior=o.counts.get('actor_motion',0);o.call(o.ai['callback'],struct.pack('<I',far(actor)),0x5000);native.append(snapshot(o,v,actor,hero));o.elapsed=o.interval if o.counts.get('actor_motion',0)>prior else o.elapsed+o.interval;o.burst+=o.interval
     placed=next((p for p in model['world']['actors']if p['glyph']==v[1]),None)
     row=dict(profile=profile,level=model['name'],glyph=v[1],source_placed=bool(placed),input=v,release=release,rounds=rounds,initial=list(initial),native=native,port=cpp)
     assert all(len(n)==len(p)==16 for n,p in zip(native,cpp))
     row['strict_equal']=native==cpp
     row['non_position_differences']=[dict(step=i,field=k,native=n[k],port=p[k])for i,(n,p)in enumerate(zip(native,cpp))for k in range(16)if k not in(0,2)and n[k]!=p[k]]
     row['max_position_delta']=max(max(abs(n[0]-p[0]),abs(n[2]-p[2]))for n,p in zip(native,cpp))
     row['native_displacement']=[native[-1][0]-initial[0],native[-1][2]-initial[2]];row['port_displacement']=[cpp[-1][0]-initial[0],cpp[-1][2]-initial[2]]
     if not args.before:assert row['strict_equal'],row
     reports.append(row)
 after={p:sha(ROOT/p)for p in RES.values()};assert before==after
 assert len(reports)==50,len(reports)
 (OUT/('embedded-trajectories-before.json'if args.before else 'embedded-trajectories-after.json')).write_text(json.dumps(dict(cases=len(reports),callbacks=sum(r['rounds']for r in reports),source_placed=sum(r['source_placed']for r in reports),reports=reports,sha256_before=before,sha256_after=after),indent=2));print('Recorded50embeddedtrajectories/600callbacks; strict',sum(r['strict_equal']for r in reports),'nonpositiondiffcases',sum(bool(r['non_position_differences'])for r in reports))
if __name__=='__main__':main()
