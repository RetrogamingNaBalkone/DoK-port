"""OG LEVEL4 current-bank reachability diagnostic; production data unchanged."""
import argparse,hashlib,json,math,struct,subprocess,sys,time
from pathlib import Path
from check_movement_oracle import Original,ROOT,OUT as MOVEMENT_OUT,sha,R
OUT=ROOT/'research/evidence/source-port/sp10-current-bank'

class BankOriginal(Original):
 def __init__(self):
  model=json.loads(subprocess.run([str(ROOT/'build/sdl2/windows/dok_inspect.exe'),'--json',str(ROOT/'originals/Old_Gold_3D/OLD_GOLD.RES')],capture_output=True,text=True,check=True).stdout)['levels'][0]
  super().__init__('old-gold',model)
  self.cpu.mem_write(0x30400,(ROOT/'research/evidence/source-port/sp10-interactions/ghidra/old-gold/vtables.bin').read_bytes())
  for d in model['objects']:
   base=0x41000+d['glyph']*0x80
   for kind,offset,value in d['fields']:
    if kind in [0,1,2] and offset+4<=0x80:self.cpu.mem_write(base+offset,struct.pack('<I',value&0xffffffff) if kind==1 else struct.pack('<H',value&65535))
   if d['class']==12:self.cpu.mem_write(base+0x10,struct.pack('<H',0x69e))
  self.surface_pose=None

 def hook(self,c,address,size,unused):
  if address==0x50908:self.surface_pose=list(struct.unpack('<iii',c.mem_read(0x4e026,12)))
  super().hook(c,address,size,unused)

 def setup(self,x=14616,z=19599,heading=45,elapsed=416):
  self.reset_maps();self.elapsed=elapsed;self.trace=[]
  actor=bytearray(0xde)
  for offset,fmt,value in [(0x12,'h',1),(0x15,'h',4),(0x26,'i',x),(0x2a,'i',-200),(0x2e,'i',z),(0x32,'h',heading),(0x34,'h',5),(0x38,'i',60),(0x5a,'h',4),(0x80,'h',1),(0x82,'h',0),(0x90,'i',10000),(0x94,'i',10000),(0x9e,'h',-174),(0xa0,'h',-226)]:struct.pack_into('<'+fmt,actor,offset,value)
  struct.pack_into('<h',actor,0x5e,100);struct.pack_into('<h',actor,0x62,100)
  actor[0x5c]=self.hero;self.cpu.mem_write(0x4e000,bytes(actor))
  cell=z//self.side*self.width+x//self.side
  self.cpu.mem_write(0x30000+self.layout['maps']+cell,bytes([self.hero]))

 def tick(self,forward=1,strafe=-1,heading=45,run=1):
  c=self.cpu
  flags=(0x10 if forward>0 else 0x20 if forward<0 else 0)|(0x100000 if strafe>0 else 0x80000 if strafe<0 else 0)|(0x2000 if run else 0)
  c.mem_write(0x4e01c,struct.pack('<I',flags));c.mem_write(0x4e032,struct.pack('<h',heading))
  before=list(struct.unpack('<iii',c.mem_read(0x4e026,12)));self.surface_pose=None
  self.call(0x1e16,struct.pack('<I',0x4000e000),0x5000)
  after=list(struct.unpack('<iii',c.mem_read(0x4e026,12)))
  return dict(before=before,surface=self.surface_pose,after=after,translations=self.trace[:])

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--full',action='store_true');args=parser.parse_args()
 OUT.mkdir(parents=True,exist_ok=True)
 originals=['originals/Old_Gold_3D/OG.EXE','originals/Old_Gold_3D/OLD_GOLD.RES']
 before={p:sha(ROOT/p) for p in originals}
 inventory=json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles']
 assert before[originals[0]]==next(p['sha256_before'] for p in inventory if p['id']=='old-gold')
 b=BankOriginal();o=Original('old-gold',b.model);invariant=[]
 for x in range(14613,14619):
  for z in range(19488,19653):
   assert o.run([1,x,z,0,0])==[1]
   result=o.run([0,x,z,x+100,z]);invariant.append([x,z,*result])
 assert all(v[3]==19656 for v in invariant)
 (OUT/'native-bank-invariant.json').write_text(json.dumps(dict(count=len(invariant),start_x=[14613,14618],start_z=[19488,19652],surface_x=100,minimum_result_z=19656,maximum_result_z=19656,rows=invariant),indent=2)+'\n')
 b.setup(14616,19599,136,1248);witness=b.tick(1,-1,136,1)
 assert witness['after'][0]==14618 and witness['after'][2]==19339
 (OUT/'native-late-witness.json').write_text(json.dumps(dict(elapsed=1248,heading=136,forward=1,strafe=-1,run=True,result=witness),indent=2)+'\n')
 if args.full:
  # No doors occur in this pocket; map resets suffice between fixtures.
  def reset_maps(empty=False):
   for plane,source in enumerate([b.maps[1],b.maps[0],b.maps[2]]):b.cpu.mem_write(0x30000+b.layout['maps']+plane*4356,bytes(source))
  b.reset_maps=reset_maps;rows=[]
  for elapsed in [416,832,1248,1664,2496]:
   start=time.time();best=None;entered=[];count=0
   for heading in range(360):
    for forward,strafe in [(1,0),(-1,0),(0,1),(0,-1),(1,1),(1,-1),(-1,1),(-1,-1)]:
     b.setup(14616,19599,heading,elapsed);result=b.tick(forward,strafe,heading,1);count+=1
     example=dict(heading=heading,forward=forward,strafe=strafe,result=result)
     if best is None or result['after'][2]<best['result']['after'][2]:best=example
     if result['after'][2]<19488 and 14448<=result['after'][0]<14784:entered.append(example)
   rows.append(dict(elapsed=elapsed,count=count,minimum=best,entered=entered,seconds=time.time()-start))
   (OUT/'native-digital-sweep.json').write_text(json.dumps(rows,indent=2)+'\n')
   print(elapsed,count,'native cases;',len(entered),'entries',flush=True)
  assert not rows[0]['entered'] and not rows[1]['entered'] and rows[2]['entered']
 command=[str(ROOT/'build/sdl2/windows/dok_movement_tests.exe'),str(ROOT/originals[1])]
 p=subprocess.run(command,capture_output=True,text=True,check=True)
 (OUT/'cpp-regression.log').write_text(p.stdout+p.stderr)
 after={p:sha(ROOT/p) for p in originals};assert before==after
 sources={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in [Path(__file__).resolve(),ROOT/'ports/sdl2/src/movement.cpp',ROOT/'ports/sdl2/src/items.cpp',ROOT/'ports/sdl2/tests/movement_tests.cpp']}
 report=dict(native_invariant_poses=len(invariant),fixed416_maximum_north_bound=3*(30*math.sqrt(2)+2),required_north_crossing=165,native_late_witness=witness,cpp_command=command,cpp_exit=p.returncode,cpp_stdout=p.stdout,sha256_before=before,sha256_after=after,source_sha256=sources,full_sweep_rerun=args.full,boundary='Explicit local fixture; original player1e16 instructions with controlled stopwatch; no spawn playthrough or live sound')
 (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print('990 native bank poses; late1248 witness; signed axis/freeflow/occupancy and coin pickup regression PASS; hashes unchanged')
 print(p.stdout,end='')

if __name__=='__main__':main()
