"""G11: execute original Ghidra-exported billboard x86 instructions.

Far pointers are mapped to real-mode test segments. Only the compiler array
constructor is intercepted. No DOS session or complete raster equivalence.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,struct
from pathlib import Path
from verify_surfaces import Original,R

def main():
 report=dict(task='G11',status='passed',whole_DOS_session=False,languages={})
 for language in ('ru','en'):
  original=Original(language);c=original.c;path=Path('research/evidence/sprites')/language
  c.mem_write(0x50000,(path/'geometry.bin').read_bytes());delta=original.delta
  count=0;negative_width=0
  for depth in (120,300,600,1200,2400,6000):
   for lateral in (-200,-40,0,40,200):
    for width in (-3,25,84,480,700):
     for height in (60,480,700):
      original.setup(0x5000)
      for offset,fmt,value in [(0xaa16,'i',200),(0xaa1e,'h',159),(0xaa20,'h',79),(0xaa32,'h',8),
        (0xaa3e,'i',32768),(0xaa42,'i',0),(0xaa46,'i',15120),(0xaa4a,'i',-300),(0xaa4e,'i',22800),
        (0xaa22,'i',0),(0xaa26,'i',0),(0xaa2a,'i',319),(0xaa2e,'i',159)]:
       c.mem_write(0x30000+offset+delta,struct.pack('<'+fmt,value))
      c.mem_write(0x30000+(0x934 if language=='ru' else 0xa44),struct.pack('<h',4096))
      c.mem_write(0x41000,struct.pack('<iii',15120-depth,-250,22800+lateral));c.mem_write(0x42000,bytes(128))
      args=struct.pack('<6H2h4H',0xfff0,0x5000,0x2000,0x4000,0x1000,0x4000,height,width,8,0x100,0x4000,0)
      c.mem_write(0x3f000,args)
      c.emu_start(0x50000+(0xccb if language=='ru' else 0xce3),0x5fff0,count=10000)
      assert c.reg_read(R.UC_X86_REG_SP)==0xf004
      b=bytes(c.mem_read(0x42000,128));factor=6553600//(depth+200)
      left=((lateral-(width//2))*factor)//32768+159;right=((lateral+(width//2))*factor)//32768+159
      top=((50-height)*factor)//32768+79;bottom=(50*factor)//32768+79
      visible=min(left,right)<=319 and max(left,right)>=0 and bottom>0 and top<159
      assert bool(b[6]&1)==visible,(language,depth,lateral,width,height,b[6],visible)
      if visible:
       actual=[struct.unpack_from('<3i',b,0xf+k*12) for k in range(4)]
       expected=[(left,top,depth),(right,top,depth),(right,bottom,depth),(left,bottom,depth)]
       assert actual==expected,(language,actual,expected)
       assert struct.unpack_from('<i',b,7)[0]==max(0,depth-(width//4))
      count+=1;negative_width+=width<0
  manifest=json.loads((path/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
  assert manifest['sha256_before']==manifest['sha256_after']
  report['languages'][language]=dict(billboard_vectors=count,negative_odd_width_vectors=negative_width,
     manifest_sha256=hashlib.sha256((path/'analysis-manifest.json').read_bytes()).hexdigest(),sample=manifest['sample'],sha256=manifest['sha256_after'])
 Path('analysis/gba/g11-original-verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print('G11 original billboard: 900 x86 constructor vectors, including SAR16 negative odd width, passed')

if __name__=='__main__':main()
