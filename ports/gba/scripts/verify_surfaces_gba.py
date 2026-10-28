"""G10 integrated surface crop phases in mGBA, with the host renderer oracle.

Select real LEVEL2 water views with visible phase differences. The frame field
is supplied by this probe; automatic animation scheduling belongs to G11.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse,ctypes as ct,hashlib,json,os,struct,subprocess
from pathlib import Path
from gba_rom import elf_info
from verify_gba import Frontend,sha
from verify_renderer_gba import NAMES,stack_usage,freeze_visual
from verify_renderer_host import write_frame_png,palette555
from analyze_resources import parse

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,default=Path('build/gba/g10'))
 p.add_argument('--core',type=Path,default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
 a=p.parse_args();build=a.build_dir;out=build/'surface-check';out.mkdir(exist_ok=True)
 _,syms,_=elf_info(build/'dok-diagnostic.elf');f=Frontend(a.core.resolve(),build);f.load(build/'dok-diagnostic.gba')
 def write(at,b):ct.memmove(f.pointer(at,len(b)),b,len(b))
 def diag():return dict(zip(NAMES,struct.unpack('<32I',f.read(syms['g_render_diag'],128))))
 def until(predicate):
  for _ in range(6000):
   f.run(1);d=diag();basic=f.diag(syms['g_diag']);freeze_visual(f,syms,d['magic'])
   if basic['magic']==0x444f4b31:assert basic['fault']==0,basic
   if d['draws']:assert d['status']==0 and d['guard_ok']==1,d
   if predicate(d):return d
  raise AssertionError(('timeout',diag()))
 until(lambda d:d['draws']>=3 and d['phase']==2)
 write(syms['g_motion_probe'],struct.pack('<I',1));write(syms['g_camera_capture_hold'],struct.pack('<I',1))
 model=json.loads(Path('research/evidence/runtime/ru/LEVEL2-runtime.json').read_text());fc=bytes.fromhex(model['maps'][2]);request=0;captures=[]
 def host(language,camera,glyph,phase):
  cmd=['build/gba/host/dok-render-reference.exe','build/gba/assets/assets.bin','2',language,str(out/'host.idx'),str(out/'host.pal'),*map(str,camera),str(glyph),str(phase),'18']
  subprocess.run(cmd,stdout=subprocess.DEVNULL,check=True)
  return (out/'host.idx').read_bytes()
 selections=[]
 for glyph in (ord('<'),ord('>'),ord('{'),ord('}')):
  index=next(i for i,o in enumerate(model['objects']) if o['glyph']==glyph)
  cells=[i for i,g in enumerate(fc) if g==glyph];best=None
  for cell in cells[::max(1,len(cells)//6)]:
   for heading in (0,90,180,270):
    camera=[cell%64*480+240,-300,cell//64*480+240,heading]
    before=host('ru',camera,glyph,0);after=host('ru',camera,glyph,9)
    difference=sum(before[i]!=after[i] for i in range(144*240))
    if best is None or difference>best[0]:best=(difference,camera)
  assert best and best[0]>10,(glyph,best)
  selections.append(dict(glyph=glyph,object=index,camera=best[1],phase9_changed_pixels=best[0]))
 for selected in selections:
  camera=selected['camera'];request+=1
  write(syms['g_camera_probe']+8,struct.pack('<I4i',1,*camera));write(syms['g_camera_probe'],struct.pack('<I',request))
  until(lambda d:struct.unpack('<I',f.read(syms['g_camera_probe']+4,4))[0]==request and d['phase']==2)
  for item in selections:write(syms['g_render_world']+4+item['object']*36+2,struct.pack('<H',0))
  for language in ('ru','en'):
   target=int(language=='en')
   if diag()['language']!=target:
    old=diag()['switches']
    for _ in range(6000):
     f.run(1,[2])
     if diag()['switches']>old:break
    until(lambda d:d['last_keys']==0 and d['language']==target and d['phase']==2)
   for phase in (0,1,9,17):
    write(syms['g_render_world']+4+selected['object']*36+2,struct.pack('<H',phase))
    draws=diag()['draws'];d=until(lambda d:d['draws']>=draws+2 and d['phase']==2)
    reference=host(language,camera,selected['glyph'],phase)
    actual=f.read(0x06000000+d['visible_page']*0xa000,38400);compared=0
    for y in range(144):
     for x in range(240):
      if y<12 and (x<56 or x>=192) or y>=132 and x>=192:continue
      assert actual[y*240+x]==reference[y*240+(x&~1)],(selected,language,phase,x,y)
      compared+=1
    path=out/f'{language}-{selected["glyph"]}-{phase}.png'
    write_frame_png(path,actual,(out/'host.pal').read_bytes(),True)
    captures.append(dict(glyph=selected['glyph'],language=language,phase=phase,camera=camera,scene_pixels=compared,cycles=d['last_cycles'],frame_sha256=hashlib.sha256(actual).hexdigest(),path=path.as_posix()))
 palettes=[]
 for level in range(3):
  m=json.loads(Path(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json').read_text())
  request+=1
  # Only resource/palette selection matters here; the camera stays bounded.
  write(syms['g_camera_probe']+8,struct.pack('<I4i',level,15120,-300,22800,90));write(syms['g_camera_probe'],struct.pack('<I',request))
  until(lambda d:struct.unpack('<I',f.read(syms['g_camera_probe']+4,4))[0]==request and d['phase']==2)
  for language,path in [('ru','RUS/PKR'),('en','ENG/PKE')]:
   if diag()['language']!=int(language=='en'):
    old=diag()['switches']
    for _ in range(6000):
     f.run(1,[2])
     if diag()['switches']>old:break
    until(lambda d:d['last_keys']==0 and d['language']==int(language=='en') and d['phase']==2)
   archive=Path(f'originals/pkr/{path}.RES').read_bytes();entries=parse(archive)
   def resource(name):
    entry=next(e for e in entries if e['name_hex']==name)
    return archive[entry['offset']:entry['offset']+entry['size']]
   palette=resource(m['palette']);light=resource(m['light'])
   assert len(palette)==768 and max(palette)<=63 and len(light)==4096 and light[:256]==bytes(range(256))
   assert all(light[row*256]==0 for row in range(16))
   expected=palette555(palette);assert f.read(0x05000000,512)==expected
   palettes.append(dict(level=level+1,language=language,palette555_sha256=hashlib.sha256(expected).hexdigest(),light_sha256=hashlib.sha256(light).hexdigest(),light_row15_nonzero_to_zero=sum(light[15*256+i]==0 for i in range(1,256))))
 report=dict(task='G10',status='passed',rom_sha256=sha(build/'dok-diagnostic.gba'),selections=selections,captures=captures,palettes=palettes,stack=stack_usage(f),automatic_surface_animation=False,light_row_startup_cases=True)
 Path('analysis/gba/g10-surface-gba.json').write_text(json.dumps(report,indent=2)+'\n');f.core.retro_unload_game();f.core.retro_deinit()
 print(f'G10: {len(captures)} ARM crop views, four directions, both languages passed')
if __name__=='__main__':main()
