"""G18 original PKR/PKE far-call slices, not a full DOS session.
Original stat equality and player death/respawn execute. External sound/HUD
calls are no-ops; respawn's external memcpy is a controlled 30-byte copy,
separately checked against the native world+716/helper arguments.
"""
import hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 report=dict(task='G18',status='passed',whole_DOS_session=False,languages={},external_hooks='sound/HUD no-op; world restore helper copies audited 30 bytes; helper memcpy external runtime recording/copy')
 for lang in ('ru','en'):
  ru=lang=='ru';directory=Path('build/flow')/lang;seg=0x1068 if ru else 0x1020
  worldseg=0x1098 if ru else 0x1030;global_world=0x266a if ru else 0x271a
  entry_stat=0x1cc2 if ru else 0x2276;entry_death=0x193e if ru else 0x1efc
  c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
  binary=(directory/f'{seg:04x}.bin').read_bytes();c.mem_write(seg*16,binary)
  asm=(directory/'evidence.asm').read_text()
  helper=next(line.split()[1] for line in asm.splitlines() if line.startswith(f'FUNCTION {worldseg:04x}:') and ('125b' in line if ru else '')) if ru else None
  # Find helper called from the death function in the English listing.
  if not ru:
   block=asm[asm.index('FUNCTION 1020:1efc'):asm.index('FUNCTION 1020:20c4') if 'FUNCTION 1020:20c4' in asm else asm.index('FUNCTION 1020:2120')]
   import re
   helper=re.search(r'CALLF 0x1030:([0-9a-f]+)',block).group(1);helper=f'1030:{helper}'
  helper_entry=int(helper.split(':')[1],16)
  calls=[]
  def hook(cpu,address,size,unused):
   cs=cpu.reg_read(R.UC_X86_REG_CS);ip=cpu.reg_read(R.UC_X86_REG_IP)
   if cs==seg:return
   if cs==0x7000:return
   sp=cpu.reg_read(R.UC_X86_REG_SP);calls.append((cs,ip))
   if cs==worldseg and ip==helper_entry:
    off,ss=struct.unpack('<HH',cpu.mem_read(0x30000+sp+4,4));assert (off,ss)==(0,0x5000)
    initial=bytes(cpu.mem_read(0x50716,30));cpu.mem_write(0x40026,initial)
   ret,retcs=struct.unpack('<HH',cpu.mem_read(0x30000+sp,4));cpu.reg_write(R.UC_X86_REG_SP,sp+4);cpu.reg_write(R.UC_X86_REG_CS,retcs);cpu.reg_write(R.UC_X86_REG_IP,ret)
  c.hook_add(UC_HOOK_CODE,hook)
  def run(entry,args):
   for reg,value in ((R.UC_X86_REG_CS,seg),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,value)
   c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x7000)+args);calls.clear();c.emu_start(seg*16+entry,0x7fff0,count=100000)
   assert c.reg_read(R.UC_X86_REG_SP)==0xf004
   return c.reg_read(R.UC_X86_REG_AX)
  c.mem_write(0x30000+global_world,struct.pack('<HH',0,0x5000));c.mem_write(0x50000,b'\0'*0x800)
  stats=[]
  for score,total in ((0,0),(0,1),(1,0),(1,1),(32767,32767),(-32768,-32768),(-1,0),(-1,-1)):
   hero=bytearray(0xe0);struct.pack_into('<h',hero,0x5e,77);struct.pack_into('<h',hero,0xd8,score);c.mem_write(0x40000,bytes(hero));c.mem_write(0x50710,struct.pack('<h',total))
   run(entry_stat,struct.pack('<HHhh',0,0x4000,1000,1));hp=struct.unpack('<h',c.mem_read(0x4005e,2))[0];assert hp==(-1 if score==total else 77),(lang,score,total,hp)
   stats.append(dict(score=score,total=total,health=hp))
  deaths=[]
  for hp,lives in ((100,3),(-1,3),(0,3),(0,2),(0,1),(0,0),(0,-32768)):
   hero=bytearray((i*17+9)&255 for i in range(0xe0));struct.pack_into('<ii',hero,0x26,960,-300);struct.pack_into('<i',hero,0x2e,960);hero[0x5c]=ord('@');struct.pack_into('<hh',hero,0x5e,hp,42);struct.pack_into('<h',hero,0x62,111);struct.pack_into('<hh',hero,0xd6,lives,7)
   world=bytearray(0x800);struct.pack_into('<h',world,0,64);struct.pack_into('<h',world,4,64);struct.pack_into('<h',world,0x738,480);struct.pack_into('<I',world,0x742,64);struct.pack_into('<HH',world,0x734,0,0x4000);initial=bytearray(range(30));struct.pack_into('<iii',initial,0,480,-300,480);world[0x716:0x734]=initial
   c.mem_write(0x40000,bytes(hero));c.mem_write(0x50000,bytes(world));c.mem_write(0x30000+(0x608a if ru else 0x613a),b' '*4096)
   result=run(entry_death,struct.pack('<HH',0,0x4000));actual=bytes(c.mem_read(0x40000,len(hero)));expected=hero[:]
   left=(lives-1+32768)%65536-32768
   if hp==0:
    struct.pack_into('<h',expected,0xd6,left)
    if left!=0:expected[0x26:0x44]=initial;struct.pack_into('<h',expected,0x5e,111)
   assert actual==expected,(lang,hp,lives,[(i,a,b) for i,(a,b) in enumerate(zip(actual,expected)) if a!=b])
   assert result==(3 if hp<0 else 2 if hp==0 and left==0 else 1 if hp==0 else 0),(lang,hp,lives,result)
   deaths.append(dict(health=hp,lives=lives,result=result,lives_after=struct.unpack_from('<h',actual,0xd6)[0],preserved_other_fields=True,external_calls=calls[:]))
  # Execute the original world restore helper separately; preserve its
  # non-C ABI CX=30 and callee-pop far memcpy stack arguments.
  u=Uc(UC_ARCH_X86,UC_MODE_16);u.mem_map(0,0x100000)
  u.mem_write(worldseg*16,(directory/f'{worldseg:04x}.bin').read_bytes())
  u.mem_write(0x50000,bytes(world));u.mem_write(0x40000,bytes(hero));memcpy_calls=[]
  def copy_hook(cpu,address,size,unused):
   if cpu.reg_read(R.UC_X86_REG_CS)!=0x1000:return
   assert cpu.reg_read(R.UC_X86_REG_IP)==0x5cb3
   sp=cpu.reg_read(R.UC_X86_REG_SP)
   src,ss,dst,ds=struct.unpack('<4H',cpu.mem_read(0x30000+sp+4,8))
   count=cpu.reg_read(R.UC_X86_REG_CX);assert (src,ss,dst,ds,count)==(0x716,0x5000,0x26,0x4000,30)
   cpu.mem_write(ds*16+dst,bytes(cpu.mem_read(ss*16+src,count)));memcpy_calls.append(count)
   ip,cs=struct.unpack('<HH',cpu.mem_read(0x30000+sp,4));cpu.reg_write(R.UC_X86_REG_SP,sp+12);cpu.reg_write(R.UC_X86_REG_CS,cs);cpu.reg_write(R.UC_X86_REG_IP,ip)
  u.hook_add(UC_HOOK_CODE,copy_hook)
  for reg,value in ((R.UC_X86_REG_CS,worldseg),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):u.reg_write(reg,value)
  u.mem_write(0x3f000,struct.pack('<4H',0xfff0,0x7000,0,0x5000));u.emu_start(worldseg*16+helper_entry,0x7fff0,count=10000)
  restored=bytes(u.mem_read(0x40000,len(hero)));assert restored[:0x26]==hero[:0x26] and restored[0x26:0x44]==initial and restored[0x44:]==hero[0x44:] and memcpy_calls==[30]
  manifest=json.loads((directory/'analysis-manifest.json').read_text(encoding='utf-8-sig'));assert manifest['sha256_before']==manifest['sha256_after']==sha(manifest['sample'])
  report['languages'][lang]=dict(sample=manifest['sample'],sha256=manifest['sha256_after'],manifest_sha256=sha(directory/'analysis-manifest.json'),asm_sha256=sha(directory/'evidence.asm'),stat_entry=f'{seg:04x}:{entry_stat:04x}',death_entry=f'{seg:04x}:{entry_death:04x}',respawn_helper=helper,helper_native_far_args_and_CX30=True,equality_vectors=stats,death_vectors=deaths)
 report['script_sha256']=sha(__file__);Path('analysis/g18-original-flow.json').write_text(json.dumps(report,indent=2)+'\n');print('G18 original PKR/PKE equality/death: passed')
if __name__=='__main__':main()
