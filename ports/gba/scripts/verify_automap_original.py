"""Execute Ghidra-exported DOS automap instructions, intercepting VGA primitives.

Default scale is supplied as 3x3 after the two x87 round calls (constant 3.0,
mode13 scale 1). Clip setup and VGA device virtuals are replaced by an indexed
canvas. Original traversal, class rules, colours, neighbours and arrow execute.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,struct,sys,subprocess
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R

sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

class OriginalMap:
 def __init__(self,lang):
  root=Path('research/evidence/automap')/lang
  manifest=json.loads((root/'manifest.json').read_text())
  assert sha(manifest['sample'])==manifest['sha256_before']==manifest['sha256_after']
  self.ru=lang=='ru';self.c=Uc(UC_ARCH_X86,UC_MODE_16);self.c.mem_map(0,0x100000)
  self.c.mem_write(0x50000,(root/('1098.bin' if self.ru else '1030.bin')).read_bytes())
  self.c.mem_write(0x30000,(root/('1190.bin' if self.ru else '10d0.bin')).read_bytes())
  self.delta=0 if self.ru else 0xb0
  self.entry=0x1290 if self.ru else 0x1374
  self.c.mem_write(0x60000,b'\xcb'*0x1000)
  self.c.mem_write(0x40000,struct.pack('<H',0x800))
  for off,stub in ((4,0x100),(0x14,0x200),(0x18,0x300),(0x38,0x400)):
   self.c.mem_write(0x30800+off,struct.pack('<HH',stub,0x6000))
  self.put(0xb48c+self.delta,'<HH',0,0x4000)
  for off,value in ((0x30,320),(0x34,200),(0x38,0),(0x3c,0),(0x40,319),(0x44,199)):
   self.c.mem_write(0x40000+off,struct.pack('<i',value))
  self.c.hook_add(UC_HOOK_CODE,self.hook)
 def put(self,off,fmt,*values):self.c.mem_write(0x30000+off,struct.pack(fmt,*values))
 def hook(self,c,address,size,data):
  # Preserve original BP/DS prologue and bypass only x87 scaling.
  if address==0x50000+self.entry+0x10:
   bp=c.reg_read(R.UC_X86_REG_BP);self.put(bp-6,'<hh',3,3)
   # DOS uses these locals before locating the hero in the first row.
   # Supply their eventual values to exclude undefined stale-stack pixels.
   self.put(bp-0x26,'<hh',7+self.hero_z*3,64+(64-self.hero_x)*3)
   c.reg_write(R.UC_X86_REG_IP,self.entry+0x34)
  if c.reg_read(R.UC_X86_REG_CS) in (0x1110,0x1068):
   c.mem_write(address,b'\xcb');return
  if c.reg_read(R.UC_X86_REG_CS)!=0x6000:return
  sp=c.reg_read(R.UC_X86_REG_SP);args=bytes(c.mem_read(0x30000+sp+8,18))
  op=c.reg_read(R.UC_X86_REG_IP)
  colour=c.mem_read(0x4000e,1)[0]
  if op==0x100:
   x,y=struct.unpack_from('<ii',args);self.pixel(x,y,args[8]);return
  if op in (0x200,0x300):
   a,b,d=struct.unpack_from('<iii',args);colour=args[12]
   if op==0x200:
    for x in range(min(a,d),max(a,d)+1):self.pixel(x,b,colour)
   else:
    for y in range(min(b,d),max(b,d)+1):self.pixel(a,y,colour)
  elif op==0x400:
   x,y,x2,y2=struct.unpack_from('<iiii',args)
   for yy in range(min(y,y2),max(y,y2)+1):
    for xx in range(min(x,x2),max(x,x2)+1):self.pixel(xx,yy,colour)
 def pixel(self,x,y,col):
  if 0<=x<320 and 0<=y<200:self.frame[y*320+x]=col
 def draw(self,model,heading=None,revealed=False,visited=None):
  c=self.c;self.frame=bytearray(64000);world=0x41000
  hero=model['objects'][model['player_index']-1]['actor'];self.hero_x=hero['x']//480;self.hero_z=hero['z']//480
  c.mem_write(world,bytes(0x800));c.mem_write(world,struct.pack('<hhh',64,0,64))
  c.mem_write(world+0xc,struct.pack('<h',int(revealed)))
  c.mem_write(world+0x742,struct.pack('<i',64))
  for i,o in enumerate(model['objects']):
   ptr=0x100+i*0x180;c.mem_write(0x70000+ptr,bytes.fromhex(o['bytes']))
   c.mem_write(world+0x30e+o['glyph']*4,struct.pack('<HH',ptr,0x7000))
   if o['class']==1:
    c.mem_write(world+0x734,struct.pack('<HH',ptr,0x7000))
    if heading is not None:c.mem_write(0x70000+ptr+0x32,struct.pack('<h',heading))
  for i,plane in enumerate(model['maps']):
   self.c.mem_write(0x30000+(0x608a if self.ru else 0x613a)+i*0x1104,bytes.fromhex(plane))
  self.put(0x9396 if self.ru else 0x9446,'4096s',bytes(visited or [1]*4096))
  self.put(0xf000,'<4H',0xfff0,0x5000,0x1000,0x4000)
  for reg,val in ((R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):
   c.reg_write(reg,val)
  # Device clip setup called outside our exported code segment.
  c.mem_write(0x112d8,b'\xcb')
  c.emu_start(0x50000+self.entry,0x5fff0,count=10000000)
  assert c.reg_read(R.UC_X86_REG_SP)==0xf004
  return bytes(self.frame)

def main():
 out=Path('research/evidence/automap');results=[]
 for lang in ('ru','en'):
  native=OriginalMap(lang)
  for level in (1,2,3):
   model=json.loads(Path(f'research/evidence/runtime/{lang}/LEVEL{level}-runtime.json').read_text())
   for reveal in (False,True):
    frame=native.draw(model,revealed=reveal)
    stem=out/lang/f'level{level}-{int(reveal)}';stem.with_suffix('.idx').write_bytes(frame)
    hero=model['objects'][model['player_index']-1]['actor']
    fixture=struct.pack('<8H',64,64,len(model['objects']),model['player_index'],int(reveal),hero['x']//480,hero['z']//480,hero['heading'])
    for obj in model['objects']:
     fixture+=struct.pack('<4H',obj['glyph'],obj['class'],obj['marker'],struct.unpack_from('<H',bytes.fromhex(obj['bytes']),0xa2)[0])
    fixture+=b''.join(bytes.fromhex(p) for p in model['maps'][:3])+bytes([1]*4096)
    stem.with_suffix('.fixture').write_bytes(fixture)
    errors=[]
    for scroll in (0,56):
     subprocess.run(['build/gba/ui01-dos-map/automap-dump.exe',str(stem.with_suffix('.fixture')),str(stem.with_suffix('.gba-idx')),str(scroll)],check=True)
     actual=stem.with_suffix('.gba-idx').read_bytes()
     errors += [(x,y+scroll,frame[(y+scroll)*320+x+24],actual[y*240+x]) for y in range(144) for x in range(240) if frame[(y+scroll)*320+x+24]!=actual[y*240+x]]
    print(lang,level,reveal,'different pixels',len(errors),errors[:8],flush=True)
    results.append(dict(language=lang,level=level,revealed=reveal,sha256=hashlib.sha256(frame).hexdigest(),different_pixels=len(errors)))
 (out/'execution.json').write_text(json.dumps(results,indent=2)+'\n')
 assert not any(r['different_pixels'] for r in results),results
 print('Original automap x86 comparison passed')
if __name__=='__main__':main()
