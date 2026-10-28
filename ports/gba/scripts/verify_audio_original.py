"""Execute native x86 audio policy. Hardware/DOS IRQ boundaries are intercepted."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 report=dict(task='G16',status='passed',whole_DOS_session=False,script_sha256=sha(__file__),languages={})
 for lang in ('ru','en'):
  ru=lang=='ru';path=Path('research/evidence/audio')/lang;c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
  base=0x10f0 if ru else 0x1058;entries=(0x5e,0xea,0x125) if ru else (0x186a,0x18f6,0x1931)
  # Give the NE segment a distinct physical real-mode load base. Ghidra NE
  # selectors are not real-mode paragraphs; this avoids overlapping aliases.
  c.mem_write(0x70000,(path/f'{base:04x}.bin').read_bytes())
  calls=[]
  def ret():
   sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4));c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
  def hook(cpu,address,size,unused):
   if cpu.reg_read(R.UC_X86_REG_CS)==0x6000 and address<0x6fff0:calls.append(address);ret()
  c.hook_add(UC_HOOK_CODE,hook)
  def run(entry,args):
   for reg,n in ((R.UC_X86_REG_CS,0x7000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,n)
   c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args);c.emu_start(0x70000+entry,0x6fff0,count=1000)
   assert c.reg_read(R.UC_X86_REG_CS)==0x6000;return c.reg_read(R.UC_X86_REG_AX)
  current=0xbca if ru else 0xc7e
  c.mem_write(0x50000,struct.pack('<H',0x1000));c.mem_write(0x31014,struct.pack('<HHHH',0,0x6000,4,0x6000))
  c.mem_write(0x40014,struct.pack('<II',5,0x90000000));args=struct.pack('<HHHH',0,0x5000,0,0x4000)
  assert run(entries[0],args)==1 and c.mem_read(0x40007,1)==b'\x80'
  assert run(entries[0],args)==0 and len(calls)==1
  different=struct.pack('<HHHH',0,0x5000,0x100,0x4000)
  assert run(entries[0],different)==0 and len(calls)==1
  run(entries[2],different);assert c.mem_read(0x40007,1)==b'\x80'
  run(entries[2],args);assert c.mem_read(0x40007,1)==b'\0' and len(calls)==2
  assert run(entries[0],different)==1
  run(entries[1],struct.pack('<HH',0,0x5000));assert c.mem_read(0x40107,1)==b'\0' and len(calls)==4
  assert c.mem_read(0x30000+current,4)==struct.pack('<HH',0x100,0x4000)
  # Wrapper flag mutation executes independently; reject the external base call.
  wrapper=0x1108 if ru else 0x1060;c.mem_write(0x70000,(path/f'{wrapper:04x}.bin').read_bytes())
  handle=c.hook_add(UC_HOOK_CODE,lambda cpu,address,size,unused: (cpu.reg_write(R.UC_X86_REG_AX,0),ret()) if cpu.reg_read(R.UC_X86_REG_CS)==base else None)
  c.mem_write(0x30000+(0xbd6 if ru else 0xc8a),struct.pack('<HH',0,0x5000))
  for entry,initial,expected in ((0x1f7 if ru else 0x173d,0xc9,0x89),(0x232 if ru else 0x1778,0x89,0xc9)):
   c.mem_write(0x40007,bytes([initial]));assert run(entry,struct.pack('<HH',0,0x4000))==0;assert c.mem_read(0x40007,1)==bytes([expected])
  c.hook_del(handle)
  # Execute the actual DMA-begin latch prefix, stopping before DMA refill.
  dma=0x1118 if ru else 0x1070;c.mem_write(0x70000,(path/f'{dma:04x}.bin').read_bytes())
  end=0x86f if ru else 0x883
  stop=c.hook_add(UC_HOOK_CODE,lambda cpu,address,size,unused:cpu.emu_stop() if address==0x70000+end else None)
  for flags in (9,0x49,0x89,0xc9):
   c.mem_write(0x40007,bytes([flags]))
   for reg,n in ((R.UC_X86_REG_CS,0x7000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):c.reg_write(reg,n)
   c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args);c.emu_start(0x70000+(0x829 if ru else 0x83d),0xfffff,count=100)
   assert struct.unpack('<H',c.mem_read(0x30000+(0xd5a if ru else 0xdfa),2))[0]==bool(flags&0x40)
  c.hook_del(stop)
  asm=(path/'evidence.asm').read_text();assert ('[0xd5a]' if ru else '[0xdfa]') in asm
  m=json.loads((path/'analysis-manifest.json').read_text(encoding='utf-8-sig'));assert sha(m['sample'])==m['sha256_before']==m['sha256_after']
  report['languages'][lang]=dict(base_policy_cases=7,wrapper_mutations=2,backend_latch_cases=4,hardware_calls=len(calls),manifest=m,manifest_sha256=sha(path/'analysis-manifest.json'),evidence_sha256=sha(path/'evidence.asm'))
 report['limits']='Native base mono admission, same/different busy rejection, identity flag stop, global stop, wrapper loop flag mutations and DMA-begin loop latch execute. DMA fill and callsites reviewed in Ghidra ASM; DOS DSP/IRQ and whole DOS playback not executed. Ghidra selectors are separate from this isolated Unicorn real-mode load address.'
 Path('analysis/gba/g16-original-verification.json').write_text(json.dumps(report,indent=2)+'\n');print('G16 native RU/EN policy passed')
if __name__=='__main__':main()
