"""G10: run Ghidra-exported original horizontal constructors and light arithmetic.

Unicorn replaces valid far addresses with real-mode pointers. The array
constructor is intercepted; projection instructions are not. Not a DOS session.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,struct,subprocess,sys
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def crop_check(language):
 c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
 c.mem_write(0x50000,(ROOT/'research/evidence/surfaces'/language/'image.bin').read_bytes());count=0
 width,height=96,128;pixels=bytes((i*37+i//width*13)%256 for i in range(width*height));c.mem_write(0x60000,pixels)
 for flags in [0x408,0x808,0x1008,0x2008,0x48,0x88,0x108,0x208]:
  for phase in range(18):
   c.mem_write(0x40000,struct.pack('<5HIHH',width,height,flags,18,phase,width*height,0,0x6000))
   c.mem_write(0x3f000,struct.pack('<4H',0xfff0,0x5000,0,0x4000))
   for reg,v in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:c.reg_write(reg,v)
   c.emu_start(0x50000+(0x5c9 if language=='ru' else 0x6d9),0x5fff0,count=100000)
   pointer=c.reg_read(R.UC_X86_REG_DX)*16+c.reg_read(R.UC_X86_REG_AX)
   dx=dy=0;wrap=False
   if flags&0x440:dx=-(width//18)*phase;wrap=bool(flags&0x400)
   elif flags&0x880:dx=(width//18)*phase;wrap=bool(flags&0x800)
   elif flags&0x1100:dy=-(height//18)*phase;wrap=bool(flags&0x1000)
   else:dy=(height//18)*phase;wrap=bool(flags&0x2000)
   expected=bytes(pixels[((y+dy)%height)*width+(x+dx)%width] if wrap else
     (pixels[(y+dy)*width+x+dx] if 0<=x+dx<width and 0<=y+dy<height else 0)
     for y in range(height) for x in range(width))
   assert bytes(c.mem_read(pointer,width*height))==expected,(language,flags,phase);count+=1
 return count
class Original:
 def __init__(self,language):
  self.ru=language=='ru';self.path=ROOT/'research/evidence/surfaces'/language
  self.c=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
  c.mem_write(0x50000,(self.path/'geometry.bin').read_bytes())
  c.mem_write(0x60000,(self.path/'raster.bin').read_bytes())
  self.delta=0 if self.ru else 0xb0
  c.hook_add(UC_HOOK_CODE,self.hook)
 def hook(self,c,address,size,unused):
  if address!=0x15718:return
  sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
  c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
 def setup(self,cs):
  c=self.c
  for reg,value in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,cs),
                    (R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:c.reg_write(reg,value)
 def quad(self,camera,cell,height,inset):
  c=self.c;self.setup(0x5000);delta=self.delta
  for offset,fmt,value in [(0xaa16,'i',200),(0xaa1e,'h',159),(0xaa20,'h',79),(0xaa32,'h',8),
      (0xaa3e,'i',camera['sine']),(0xaa42,'i',camera['cosine']),(0xaa46,'i',camera['x']),
      (0xaa4a,'i',camera['y']),(0xaa4e,'i',camera['z'])]:
   c.mem_write(0x30000+offset+delta,struct.pack('<'+fmt,value))
  c.mem_write(0x30000+(0x934 if self.ru else 0xa44),struct.pack('<h',4096))
  c.mem_write(0x41000,struct.pack('<iii',cell[0]*480,-height,cell[1]*480))
  c.mem_write(0x42000,bytes(128))
  if inset<0:
   args=struct.pack('<6H2h3H',0xfff0,0x5000,0x2000,0x4000,0x1000,0x4000,480,24,0x100,0x4000,0)
   entry=0x15f4 if self.ru else 0x1624
  else:
   args=struct.pack('<6H3h4HH',0xfff0,0x5000,0x2000,0x4000,0x1000,0x4000,961-inset-height,480,24,0x100,0x4000,0x200,0x4000,0)
   entry=0x1007 if self.ru else 0x102b
  c.mem_write(0x3f000,args)
  c.emu_start(0x50000+entry,0x5fff0,count=10000)
  assert c.reg_read(R.UC_X86_REG_SP)==0xf004
  b=bytes(c.mem_read(0x42000,128));assert b[6]&1
  return [struct.unpack_from('<3i',b,(0xf if inset<0 else 0x3f)+k*12) for k in range(4)], [struct.unpack_from('<3i',b,0xf+k*12) for k in range(4)],struct.unpack_from('<i',b,7)[0]
 def light(self,depth,near):
  c=self.c;self.setup(0x6000)
  c.mem_write(0x3f7f4,struct.pack('<i',depth))
  c.mem_write(0x30000+(0xdfa if self.ru else 0xea2),struct.pack('<h',near))
  c.emu_start(0x60000+(0x147d if self.ru else 0x1483),0x60000+(0x1497 if self.ru else 0x149d),count=30)
  n=c.reg_read(R.UC_X86_REG_AX)&65535
  return min(n,15)

def main():
 report={'task':'G10','whole_DOS_session':False,'languages':{}}
 package=(ROOT/'build/gba/assets/assets.bin').read_bytes()
 u16=lambda off:struct.unpack_from('<H',package,off)[0]
 u32=lambda off:struct.unpack_from('<I',package,off)[0]
 out=ROOT/'build/gba/g10/original-check';out.mkdir(parents=True,exist_ok=True)
 for language in ['ru','en']:
  original=Original(language);count=0;rows=0;negative=0;insets=set();inventory=[]
  for level in range(1,4):
   view=u32(28)+((level-1)*2+(language=='en'))*80;bindings=u32(view+72)
   links={(u16(bindings+i*8),u16(bindings+i*8+2)):u16(bindings+i*8+4) for i in range(u16(view+76))}
   model=json.loads(subprocess.check_output([ROOT/'build/gba/host/dok-runtime.exe',ROOT/f'originals/pkr/{"RUS/PKR" if language=="ru" else "ENG/PKE"}.RES',f'LEVEL{level}']))
   cells=Counter(bytes.fromhex(model['maps'][2]));definitions={d['glyph']:d for d in model['objects']};records=[]
   for glyph,cells_used in sorted(cells.items()):
    d=definitions[glyph];inset=next(f['number'] for f in d['fields'] if f['offset']==0x54)
    assert d['class'] in [0,12,13] and d['height_parameter']==0 and inset>=0
    records.append(dict(glyph=glyph,cells=cells_used,class_id=d['class'],height=d['height_parameter'],ceiling_inset=inset,
      sprite_flags=d['sprite_flags'],floor_override=d['sprite'] if d['class'] in [12,13] else None))
   inventory.append(dict(level=level,fcmap_cells=sum(cells.values()),definitions=records,palette=model['palette'],light=model['light']))
   cmd=[ROOT/'build/gba/host/dok-render.exe',ROOT/'build/gba/assets/assets.bin',str(level),language,out/'frame.idx',out/'palette.rgb6']
   initial=json.loads(subprocess.check_output(cmd));cam=initial['camera']
   cameras=[[cam['x'],cam['y'],cam['z'],h] for h in range(0,360,45)]
   cameras += [[cam['x']//480*480+1,-10,cam['z']//480*480+1,45],[1,-10,1,45],[30719,-10,30719,225]]
   for camera in cameras:
    actual=json.loads(subprocess.check_output(cmd+list(map(str,camera))))
    for f in actual['surfaces']:
     d=model['objects'][f['object']];inset=next(x['number'] for x in d['fields'] if x['offset']==0x54)
     slot=0x5e if f['plane']==2 else (0x56 if f['plane'] else (0xfff0 if d['class'] in [12,13] else 0x5a))
     rid=links[f['object'],slot];entry=u32(24)+(rid-1)*32+16+8*(language=='en');offset=u32(entry)
     assert f['texture_offset']==offset+18 and f['texture_size']==[u16(offset),u16(offset+2)]
     if f['plane']>1:continue
     floor,ceiling,key=original.quad(actual['camera'],f['cell'],d['height_parameter'],inset)
     vertices=[(x*3//4,y*9//10,z) for x,y,z in (ceiling if f['plane'] else floor)]
     assert vertices==[tuple(x) for x in f['vertices']],(language,level,camera,f,vertices)
     assert key==f['key'];count+=1;negative+=any(v[2]<0 for v in vertices);insets.add(inset)
  # Synthetic one-plane branch, absent from the six supplied FCMaps.
  mutant=bytearray(package)
  for variant in range(2):
   view=u32(28)+variant*80;defs=u32(view+36);fields=u32(view+40)
   default=u16(u32(view+32)+32*2)-1
   begin=u16(defs+default*32+14);end=begin+u16(defs+default*32+16)
   field=next(i for i in range(begin,end) if u16(fields+i*8)==0x54)
   struct.pack_into('<i',mutant,fields+field*8+4,-1);struct.pack_into('<h',mutant,defs+default*32+8,120)
  mutant_path=out/f'one-plane-{language}.bin';mutant_path.write_bytes(mutant)
  actual=json.loads(subprocess.check_output([ROOT/'build/gba/host/dok-render.exe',mutant_path,'1',language,out/'single.idx',out/'palette.rgb6']))
  singles=0
  for f in actual['surfaces']:
   if f['object']!=default:continue
   assert f['plane']==0
   floor,_,key=original.quad(actual['camera'],f['cell'],120,-1)
   assert [(x*3//4,y*9//10,z) for x,y,z in floor]==[tuple(v) for v in f['vertices']] and key==f['key'];singles+=1
  assert singles>0
  vectors=[];expected_rows=[]
  for near in [-32768,-1920,0,1920,32767]:
   for d in list(range(-32768,32768,31))+[near-1,near,near+119,near+120,near+1800,65535,65536]:
    depth=(d*65536+12345+2**31)%2**32-2**31
    word=((depth>>16)+32768)%65536-32768
    delta=(word-near+32768)%65536-32768 if word>=near else 0
    expected=min(15,(int(abs(delta)//120)*(-1 if delta<0 else 1))&65535)
    assert original.light(depth,near)==expected,(language,d,near);rows+=1
    vectors.append(f'{depth>>16} {near}\n');expected_rows.append(expected)
  actual=list(map(int,subprocess.check_output([ROOT/'build/gba/host/dok-render.exe','--light-probe'],input=''.join(vectors).encode()).split()))
  assert actual==expected_rows
  report['languages'][language]={'horizontal_vertices':count*4,'quads':count,'negative_depth_quads':negative,
    'insets':sorted(insets),'level_inventory':inventory,'synthetic_single_planes':singles,'original_light_vectors':rows,'crop_vectors':crop_check(language),'manifest_sha256':sha(original.path/'analysis-manifest.json')}
 report['status']='passed';(ROOT/'analysis/gba/g10-original-verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':main()
