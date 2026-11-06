"""Execute original sound getter twice with one cached raw input, proving wrapper identity."""
from pathlib import Path
import struct,json,hashlib
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'research/evidence/source-port/sp13-native-complete/sound-identity'
LAYOUT={'ru':dict(getter=(0x1060,0x145),raw=(0x10e0,0x1f0),alloc=(0x10a8,0x65),ctor=(0x1108,0xec),base=0), 'en':dict(getter=(0x1018,0x18d9),raw=(0x1058,0xe0a),alloc=(0x1038,0x8b0),ctor=(0x1060,0x1632),base=0x153a),'old-gold':dict(getter=(0x19b1,0x157),raw=(0x276a,0x1e9),alloc=(0x199e,0x80),ctor=(0x18a6,0xf6),base=1)}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def far(a):return ((a>>4)<<16)|(a&15)
def lin(p):return (p>>16)*16+(p&65535)
def main():
 reports=[]
 for profile,l in LAYOUT.items():
  folder=OUT/'ghidra'/profile;m=json.loads((folder/'manifest.json').read_text());assert sha(ROOT/m['sample'])==m['sha256_before']==m['sha256_after'];before=sha(ROOT/m['sample']);c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x200000)
  getter=(folder/'getter.bin').read_bytes();assert sha(folder/'getter.bin')==m['bytes_sha256']['getter'];c.mem_write(l['getter'][0]*16+l['getter'][1],getter)
  wf=ROOT/'research/evidence/source-port/sp13-audio/ghidra'/profile;wm=json.loads((wf/'manifest.json').read_text());assert sha(wf/'wrappers.bin')==wm['bytes_sha256']['wrappers'];c.mem_write(l['ctor'][0]*16+l['base'],(wf/'wrappers.bin').read_bytes())
  raw=bytearray(0x24);raw[2:5]=b'SVM';struct.pack_into('<H',raw,5,1);raw[7]=9;struct.pack_into('<I',raw,0x14,128);struct.pack_into('<I',raw,0x18,far(0xf1000));c.mem_write(0xf0000,bytes(raw));c.mem_write(0xb0000,b'SAME_SOUND\0');calls=[];allocations=[];ctors=[];executed=set()
  def ret(value=None):
   sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4));c.reg_write(R.UC_X86_REG_SP,sp+4)
   if value is not None:c.reg_write(R.UC_X86_REG_AX,value&65535);c.reg_write(R.UC_X86_REG_DX,value>>16)
   c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
  def hook(cpu,address,size,data):
   cs=cpu.reg_read(R.UC_X86_REG_CS);ip=cpu.reg_read(R.UC_X86_REG_IP);sp=cpu.reg_read(R.UC_X86_REG_SP)
   if cs==0x2000 and ip==0xfff0:cpu.emu_stop();return
   if address==l['raw'][0]*16+l['raw'][1]:
    name=struct.unpack('<I',cpu.mem_read(0x30000+sp+8,4))[0];assert name==far(0xb0000);calls.append(name);ret(far(0xf0000));return
   if address==l['alloc'][0]*16+l['alloc'][1]:
    size=struct.unpack('<H',cpu.mem_read(0x30000+sp+4,2))[0];assert size==0x24;at=0xa0000+len(allocations)*0x100;allocations.append(at);ret(far(at));return
   if profile=='en' and (cs,ip)==(0x1090,0):ret();return
   if address==l['ctor'][0]*16+l['ctor'][1]:ctors.append(struct.unpack('<II',cpu.mem_read(0x30000+sp+4,8)))
   executed.add((cs,ip))
  c.hook_add(UC_HOOK_CODE,hook);outputs=[]
  for turn in range(2):
   for reg,v in [(R.UC_X86_REG_CS,l['getter'][0]),(R.UC_X86_REG_IP,l['getter'][1]),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xee00),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,v)
   c.mem_write(0x3f000,struct.pack('<HHI',0xfff0,0x2000,far(0xb0000)));c.emu_start(l['getter'][0]*16+l['getter'][1],0,count=10000);assert c.reg_read(R.UC_X86_REG_CS)==0x2000 and c.reg_read(R.UC_X86_REG_IP)==0xfff0 and c.reg_read(R.UC_X86_REG_SP)==0xf004
   outputs.append((c.reg_read(R.UC_X86_REG_DX)<<16)|c.reg_read(R.UC_X86_REG_AX))
  assert len(calls)==len(allocations)==len(ctors)==len(outputs)==2;assert outputs[0]!=outputs[1];assert [lin(p)for p in outputs]==allocations
  wrappers=[bytes(c.mem_read(lin(p),0x24))for p in outputs];assert wrappers[0][2:]==wrappers[1][2:];assert wrappers[0][7]==0x29
  assert [struct.unpack_from('<I',w,0x18)[0]for w in wrappers]==[far(0xf1000)]*2
  c.mem_write(lin(outputs[0])+7,b'\x09');assert c.mem_read(lin(outputs[1])+7,1)==b'\x29';assert sha(ROOT/m['sample'])==before
  report=dict(profile=profile,status='PASS',getter=l['getter'],ctor=l['ctor'],raw_lookup_calls=calls,allocator_addresses=allocations,wrapper_far_pointers=outputs,shared_pcm=far(0xf1000),wrapper_flags_independent=True,instructions=len(executed),executed_addresses=[f'{cs:04x}:{ip:04x}'for cs,ip in sorted(executed)],sha256_before=before,sha256_after=sha(ROOT/m['sample']),boundaries=['name-keyed raw cache returns same existing input (cache ASM separately exported)','malloc fresh allocation semantics','PKE exception registration only']);reports.append(report);print(profile,'two same-name native getter calls allocate distinct wrappers, sharedPCM, independentflags PASS',flush=True)
 (OUT/'report.json').write_text(json.dumps(dict(profiles=reports),indent=2)+'\n')
if __name__=='__main__':main()
