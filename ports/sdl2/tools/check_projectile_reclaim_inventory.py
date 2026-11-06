"""Inventory original static flags and executable projectile completion writers."""
import json,re,subprocess
from check_projectile_native import ROOT,OUT,TABLES,RES,description,sha

def main():
 rows=[]
 for profile in TABLES:
  res=ROOT/RES[profile];models=json.loads(subprocess.check_output([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(res)],text=True))['levels']
  asm=OUT/'ghidra'/profile/'objects.asm';writers=[line for line in asm.read_text().splitlines()if re.search(r'OR word ptr ES:\[BX \+ 0x15\],0x400$',line)];assert len(writers)==1,writers
  for model in models:
   defs,clips=description(ROOT/'build/sdl2/windows/dok_projectile_native_probe.exe',res,model['name']);flagged=[g for g,d in defs.items()if d['flags']&0x400];assert not flagged
   rows.append(dict(profile=profile,level=model['name'],static_count=len(defs),classes=sorted({d['cls']for d in defs.values()}),static_flags0400=flagged,dynamic_completion_writers=writers,asm_sha256=sha(asm)))
 (OUT/'static-reclaim-inventory.json').write_text(json.dumps(dict(checks=rows,scope='All seven original LEVEL definitions; original actor-module completion writer inventory. Dynamic completion->actual world allocator->actual class8 destructor->reuse executed in check_projectile_native. Static forced0400 is an unreachable synthetic state for these source maps; immutable definition slots remain protected by port policy.'),indent=2),'utf8');print(len(rows),'levels: zero reclaimable static definitions; one completion writer per original actor module')
if __name__=='__main__':main()
