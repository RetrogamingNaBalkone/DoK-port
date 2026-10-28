"""G11 independent original-resource billboard geometry and masked pixel probe."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,struct,subprocess
from pathlib import Path
from verify_packing import originals,Package
from probe_render_layout import frames,image
from verify_renderer_host import write_frame_png

def main():
 out=Path('build/gba/g11/sprites-host');out.mkdir(parents=True,exist_ok=True)
 source=originals();package=Package(Path('build/gba/assets/assets.bin').read_bytes(),source)
 results=[];static_checks=[]
 for language in ('ru','en'):
  model=json.loads(Path('research/evidence/runtime/ru/LEVEL1-runtime.json').read_text())
  id=next(i for i,o in enumerate(model['objects']) if o['class']==8)
  obj=model['objects'][id];metadata,pixels=frames(source[language]['resources'][obj['animation']])[0]
  width=metadata['width']*obj['width_parameter'];height=metadata['height']*obj['width_parameter']
  def render(name,instance=()):
   prefix=out/(language+'-'+name)
   cmd=['build/gba/host/dok-render.exe','build/gba/assets/assets.bin','1',language,str(prefix)+'.idx',str(prefix)+'.pal','15120','-300','22800','90',*map(str,instance)]
   result=json.loads(subprocess.check_output(cmd));frame=Path(str(prefix)+'.idx').read_bytes();pal=Path(str(prefix)+'.pal').read_bytes()
   write_frame_png(Path(str(prefix)+'.png'),frame,pal)
   return result,frame
  baseline,base=render('base')
  maps=[bytes.fromhex(m) for m in model['maps']]
  def geometry(wx,wy,wz,w,h):
   depth=15120-wx;lateral=wz-22800
   if depth<=0:return None
   factor=6553600//(depth+200)
   x0=((lateral-w//2)*factor//32768+159)*3//4
   x1=((lateral+w//2)*factor//32768+159)*3//4
   top=((wy-h+300)*factor//32768+79)*9//10;bottom=((wy+300)*factor//32768+79)*9//10
   return dict(x0=x0,x1=x1,top0=top,top1=top,bottom0=bottom,bottom1=bottom,depth0=depth,depth1=depth,key=max(0,depth-w//4))
  for face in baseline['faces']:
   definition=model['objects'][face['image_index']]
   if definition['class'] not in (3,6,9,10):continue
   raw=source[language]['resources'][definition['animation'] if definition['flags']&0x800 else definition['sprite']]
   meta=frames(raw)[0][0] if definition['flags']&0x800 else image(raw)[0]
   w,h=meta['width']*definition['width_parameter'],meta['height']*definition['width_parameter']
   candidates=[]
   for plane in (0,1):
    for cell,glyph in enumerate(maps[plane]):
     if glyph!=definition['glyph']:continue
     wx,wz=cell%64*480+240,cell//64*480+240
     if definition['class']==9:wx,wz=definition['actor']['x'],definition['actor']['z']
     fc=model['objects'][model['glyph_index'][maps[2][cell]]-1]
     wy=-fc['height_parameter']-definition['height_parameter']
     candidate=geometry(wx,wy,wz,w,h)
     if candidate:candidates.append(candidate)
   assert any(all(face[k]==v for k,v in expected.items()) for expected in candidates),(definition['glyph'],face,candidates)
   static_checks.append(dict(language=language,object=face['image_index'],class_id=definition['class'],glyph=definition['glyph'],geometry_verified=True))
  for depth in (300,600,1200,6000):
   result,frame=render('projectile-'+str(depth),(id,15120-depth,-250,22800))
   face=next(f for f in result['faces'] if f['image_index']==id)
   factor=6553600//(depth+200)
   expected=dict(x0=(((-width//2)*factor//32768+159)*3//4),x1=(((width//2)*factor//32768+159)*3//4),
      top0=(((50-height)*factor//32768+79)*9//10),bottom0=((50*factor//32768+79)*9//10),key=max(0,depth-(width//4)))
   for k,v in expected.items():assert face[k]==v,(language,depth,k,face[k],v)
   assert face['top0']==face['top1'] and face['bottom0']==face['bottom1'] and face['depth0']==face['depth1']==depth
   changed=sum(a!=b for a,b in zip(base[:34560],frame[:34560]))
   # Nearest instance is entirely in front of the source cell geometry.
   if depth==300:
    assert changed>10
    x0,x1,top,bottom=(face[k] for k in ('x0','x1','top0','bottom0'))
    expected_frame=bytearray(base)
    for x in range(max(0,x0),min(239,x1)+1):
     u=(metadata['width']-1)*(x-x0)//(x1-x0)
     step=((metadata['height']-1)<<16)//(bottom-top)
     for y in range(max(0,top),min(143,bottom)+1):
      v=((y-top)*step)>>16;color=pixels[v*metadata['width']+u]
      if color:expected_frame[y*240+x]=color
    assert frame==expected_frame,'Nearest sprite pixels differ from independent mask/scale oracle'
   results.append(dict(language=language,depth=depth,object=id,geometry=expected,changed_pixels=changed,frame_sha256=hashlib.sha256(frame).hexdigest()))
  assert results[-1]['changed_pixels']==0,'Far projectile should be occluded by the closed start door/walls'
 assert {3,9}.issubset({x['class_id'] for x in static_checks})
 report=dict(task='G11',status='passed',static_scene_sprite_faces=static_checks,geometry_and_pixels=results,source={l:dict(exe=d['exe_sha256'],res=d['archive_sha256']) for l,d in source.items()},scope='Independent original dimensions, native billboard projection, nearest masked texels and far wall occlusion; no whole-DOS raster equivalence')
 Path('analysis/gba/g11-sprites-host.json').write_text(json.dumps(report,indent=2)+'\n')
 print('G11 host: eight independent sprite projections, near masked pixels, far occlusion passed')

if __name__=='__main__':main()
