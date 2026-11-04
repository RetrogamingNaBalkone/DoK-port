"""Bind native glyph objects/images using constructor layout and measured LDT descriptors."""
import argparse,json,struct,sys
from pathlib import Path
from check_render import ROOT,read_archive
from level_reference import parse
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('--language',choices=['ru','en','og'],required=True);p.add_argument('--water-phase',type=int);p.add_argument('--memory');p.add_argument('--output',default='native-state');p.add_argument('--level',required=True);a=p.parse_args()
r=json.loads((a.capture/'report.json').read_text());og=a.language=='og';source=ROOT/({'ru':'originals/pkr/RUS/PKR.RES','en':'originals/pkr/ENG/PKE.RES','og':'originals/Old_Gold_3D/OLD_GOLD.RES'}[a.language])
memory_path=a.capture/(a.memory or ('memory-6.bin' if og else 'memory.bin'));m=memory_path.read_bytes();m=__import__('zlib').decompress(m) if memory_path.suffix=='.z' else m;entries=dict(read_archive(source.read_bytes()));level=parse(entries[a.level.encode()],a.level,old_gold=og)
ldt=0 if og else int(r['surface_address']['ldt_linear'],16)
def far(at):
 off,sel=struct.unpack_from('<HH',m,at)
 if not sel:return None
 if og:return sel*16+off
 d=m[ldt+(sel&~7):ldt+(sel&~7)+8]
 if len(d)!=8 or not d[5]&0x80:return None
 return int.from_bytes(d[2:4],'little')+(d[4]<<16)+(d[7]<<24)+off
records=[];audit=[]
for d in level['objects']:
 glyph=d['glyph'];cls=d['class'];pattern=struct.pack('<HB',cls,glyph);pos=0;found=[]
 while True:
  pos=m.find(pattern,pos+1)
  if pos<0:break
  base=pos-0x12
  if base<0 or base+0x62>=len(m):continue
  if struct.unpack_from('<hh',m,pos+5)!=(d['width'],d['height']):continue
  if struct.unpack_from('<H',m,base+0x10)[0]==0:continue
  image=far(base+0x44)
  if image is None or image+18>=len(m):continue
  w,h,flags=struct.unpack_from('<HHH',m,image)
  pixels=far(image+14)
  if not w or not h or w>2048 or h>2048 or pixels is None or pixels+w*h>len(m):continue
  if struct.unpack_from('<I',m,image+10)[0]!=w*h:continue
  found.append((base,image,pixels,w,h,flags))
 item=dict(glyph=glyph,native_class=cls,candidates=[hex(x[0]) for x in found]);audit.append(item)
 if len(found)!=1:continue
 base,image,pixels,w,h,flags=found[0];has_pose=cls in [1,5,9];pose=struct.unpack_from('<iiih',m,base+0x26) if has_pose else (0,0,0,0)
 phase=struct.unpack_from('<h',m,base+0x50)[0];extent=struct.unpack_from('<h',m,base+0x60)[0] if cls==7 else 1
 state_flags=struct.unpack_from('<H',m,base+0x15)[0]
 payload=m[pixels:pixels+w*h]
 divisor,index=struct.unpack_from('<hh',m,image+6)
 if cls==12 and a.water_phase is not None:divisor,index=18,a.water_phase
 if flags&0x3fc0:
  if divisor<=0 or not 0<=index<=divisor:raise RuntimeError('Unsupported native crop state')
  for mask,wrap,dx,dy in [(0x440,0x400,-1,0),(0x880,0x800,1,0),(0x1100,0x1000,0,-1),(0x2200,0x2000,0,1)]:
   if flags&mask:
    shift=((w if dx else h)//divisor)*index;src=payload;dst=bytearray(w*h)
    for y in range(h):
     for x in range(w):
      sx=x+dx*shift;sy=y+dy*shift
      if flags&wrap:dst[y*w+x]=src[(sy%h)*w+sx%w]
      elif 0<=sx<w and 0<=sy<h:dst[y*w+x]=src[sy*w+sx]
    payload=bytes(dst);break
 records.append(struct.pack('<BHHBiiihhhHHHI',glyph,cls,state_flags,has_pose,*pose,phase,extent,w,h,flags,len(payload))+payload)
 item.update(pose=pose,image_linear=hex(image),pixels_linear=hex(pixels),width=w,height=h,image_flags=flags,phase=phase,extent=extent,crop_divisor=divisor,crop_index=index)
(a.capture/(a.output+'.bin')).write_bytes(b'DOKR1'+struct.pack('<H',len(records))+b''.join(records))
(a.capture/(a.output+'.json')).write_text(json.dumps(audit,indent=2));print(a.language,a.level,len(records),'of',len(audit),'native glyph images bound; unmatched',[(i['glyph'],len(i['candidates'])) for i in audit if len(i['candidates'])!=1])
