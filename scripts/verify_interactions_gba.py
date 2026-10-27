"""G09 ARM scenarios in the integrated ROM using production input and events."""
import argparse,ctypes as ct,hashlib,json,os,struct,subprocess
from pathlib import Path
from gba_rom import elf_info
from verify_gba import Frontend,sha
from verify_renderer_gba import NAMES,stack_usage,freeze_visual,start_gameplay

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--build-dir',type=Path,default=Path('build/g09'))
 p.add_argument('--report',type=Path,default=Path('analysis/g09-gba-verification.json'))
 p.add_argument('--skip-visual-oracle',action='store_true',help='Quality prototypes only: retain all door/collision/sensor checks, omit original affine floor pixel comparison')
 p.add_argument('--core',type=Path,default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
 a=p.parse_args();build=a.build_dir;_,syms,_=elf_info(build/'dok-diagnostic.elf')
 f=Frontend(a.core.resolve(),build);f.load(build/'dok-diagnostic.gba');request=0;camera_request=0;captures=[];cycles=[];camera_pose=[0,0,0]
 world=syms['g_render_world'];motion=syms['g_motion_probe']
 def write(at,b):ct.memmove(f.pointer(at,len(b)),b,len(b))
 def render():return dict(zip(NAMES,struct.unpack('<32I',f.read(syms['g_render_diag'],128))))
 def interaction():return struct.unpack('<10I',f.read(syms['g_interaction_diag'],40))
 def until(pred,buttons=(),limit=2000):
  for _ in range(limit):
   f.run(1,buttons);r=render();basic=f.diag(syms['g_diag']);freeze_visual(f,syms,r['magic'])
   if r['magic']==0x524e4435 and r['draws']:start_gameplay(f,syms)
   if basic['magic']==0x444f4b31:assert not basic['fault'],basic
   if r['draws'] and r['magic']==0x524e4435:
    if 'g_camera_capture_hold' in syms:write(syms['g_camera_capture_hold'],struct.pack('<I',1))
    assert r['status']==0 and r['guard_ok'],r;cycles.append((r['last_cycles'],struct.unpack('<I',f.read(motion,4))[0]))
   if pred():return
  raise AssertionError(('timeout',render(),interaction()))
 def ready():until(lambda:render()['draws']>=3 and interaction()[0]==0x494e5439)
 def switch_language():
  previous=render();switches=previous['switches']
  if 'g_ui_probe' in syms:
   address=syms['g_ui_probe'];request=struct.unpack('<I',f.read(address+4,4))[0]+1
   write(address+8,struct.pack('<2I',2,previous['language']^1));write(address,struct.pack('<I',request))
   until(lambda:render()['switches']>switches and render()['phase']==2)
  else:until(lambda:render()['switches']>switches,[2])
  until(lambda:render()['last_keys']==0)
 def steps(n=1,cmd=0):
  nonlocal request
  while n:
   count=min(n,128);request+=1;write(motion+12,struct.pack('<II',cmd,count));write(motion+4,struct.pack('<I',request))
   until(lambda:struct.unpack('<I',f.read(motion+8,4))[0]==request);n-=count
  assert interaction()[1]==0,interaction()
 def model(level):return json.loads(Path(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json').read_text())
 def actor(m):return world+9220+64*m['objects'][m['player_index']-1]['actor_index']
 def obj(m,glyph):return world+4+36*next(i for i,o in enumerate(m['objects']) if o['glyph']==glyph)
 def pose(m,cell,heading=0):
  maps=bytearray(f.read(world+17472,4096));pg=m['objects'][m['player_index']-1]['glyph'];maps[:]=maps.replace(bytes([pg]),b' ');maps[cell]=pg
  write(world+17472,bytes(maps));write(actor(m)+4,struct.pack('<iii',(cell%64)*480+240,-300,(cell//64)*480+240));write(actor(m)+36,struct.pack('<h',heading))
 def camera(level,cell,heading):
  nonlocal camera_request
  camera_request+=1;write(syms['g_camera_probe']+8,struct.pack('<Iiiii',level,(cell%64)*480+240,-300,(cell//64)*480+240,heading));write(syms['g_camera_probe'],struct.pack('<I',camera_request))
  until(lambda:struct.unpack('<I',f.read(syms['g_camera_probe']+4,4))[0]==camera_request)
  camera_pose[:]=[level,cell,heading]
 def door(m,g):
  b=f.read(obj(m,g),36)
  return dict(flags=struct.unpack_from('<H',b)[0],frame=struct.unpack_from('<h',b,2)[0],phase=b[32],ready=b[33],occupancy=struct.unpack_from('<h',b,28)[0],extent=struct.unpack_from('<h',b,34)[0])
 def capture(name,m,g):
  draws=render()['draws'];until(lambda:render()['draws']>=draws+2 and render()['phase']==2)
  d=door(m,g);level,cell,heading=camera_pose;language='en' if render()['language'] else 'ru'
  idx=build/'interaction-reference.idx';pal=build/'interaction-reference.pal'
  subprocess.run(['build/native/dok-render-reference.exe','build/gba/assets/assets.bin',str(level+1),language,str(idx),str(pal),str(cell%64*480+240),'-300',str(cell//64*480+240),str(heading),str(g),str(d['frame']),str(d['extent'])],stdout=subprocess.DEVNULL,check=True)
  reference=idx.read_bytes();actual=f.read(syms['g_render_frame']+4,38400);compared=0
  for y in range(0 if a.skip_visual_oracle else 144):
   for x in range(240):
    if y<14 and (x<80 or x>=160) or y>=128 and x>=192:continue
    assert actual[y*240+x]==reference[y*240+(x&~1)],(name,x,y,actual[y*240+x],reference[y*240+(x&~1)])
    compared+=1
  f.screenshot(build/f'interaction-{name}.png');captures.append(dict(name=name,rgb_sha256=hashlib.sha256(f.rgb).hexdigest(),cycles=render()['last_cycles'],dynamic_scene_pixels_compared=compared))
 def reset(level=0):
  nonlocal request,camera_request
  f.core.retro_reset();ready();request=0;camera_request=0;write(motion,struct.pack('<I',1))
  if level:camera(level,64+1,0)
 def spot(m,g):
  maps=[f.read(world+17472+i*4096,4096) for i in range(3)];defs={o['glyph']:o for o in m['objects']}
  for cell,v in enumerate(maps[1]):
   if v!=g:continue
   for delta,heading in [(-64,0),(64,180),(-1,270),(1,90)]:
    neighbor=cell+delta
    if 0<=neighbor<4096 and maps[0][neighbor]==32 and defs[maps[1][neighbor]]['flags']&16:
     return cell,neighbor,heading
  raise AssertionError(('no adjacent cell',g))
 ready();assert interaction()[1]==0
 scenarios=[]
 for language in ('ru','en'):
  for glyph in (ord('a'),ord('d'),ord('g')):
   reset();m=model(0);cell,neighbor,heading=spot(m,glyph);pose(m,neighbor,heading);camera(0,neighbor,heading)
   if language=='en':
    switch_language()
   if glyph==ord('a'):
    steps(1,0x800);assert door(m,glyph)['frame']==0 and interaction()[3]==1
    write(world+17412+20,struct.pack('<H',2))
   steps(1,0x800);steps(6);middle=door(m,glyph);assert 0<middle['frame']<29 and not middle['ready'],middle
   if language=='ru' and glyph==ord('d'):
    before=f.read(world+4,render()['world_bytes']-4)
    for _ in range(2):
     switch_language()
     assert f.read(world+4,render()['world_bytes']-4)==before,'Language changed active door/world'
   capture(f'{language}-{chr(glyph)}-opening',m,glyph);steps(32);opened=door(m,glyph);assert opened['phase']==1 and opened['flags']&16,opened
   capture(f'{language}-{chr(glyph)}-open',m,glyph)
   # Actual collision movement reaches the door cell and increments shared occupancy.
   steps(3,0x10);occupied=door(m,glyph);assert occupied['occupancy']>0,occupied
   steps(230);assert door(m,glyph)['phase']==1
   # Finish crossing the cell and verify delayed close after occupancy releases.
   steps(12,0x10);assert door(m,glyph)['occupancy']==0,door(m,glyph)
   steps(350);closed=door(m,glyph);assert closed['phase']==2 and closed['flags']==64,closed
   scenarios.append(dict(language=language,glyph=glyph,middle=middle,opened=opened,occupied=occupied,closed=closed))
 # Actual GBA Start edge on a closed, unlocked door.
 reset();m=model(0);cell,neighbor,heading=spot(m,ord('d'));pose(m,neighbor,heading)
 write(motion,struct.pack('<I',0));until(lambda:door(m,ord('d'))['frame']>0,[3]);until(lambda:door(m,ord('d'))['ready']==1,[3]);live=door(m,ord('d'));assert live['phase']==1
 # Shared sensor glyph: two cells have one identity; exit and re-enter activates again.
 sensor_results=[]
 for level,target in ((1,ord('h')),(2,ord('k'))):
  reset(level);m=model(level);fc=f.read(world+17472+8192,4096);cells=[i for i,v in enumerate(fc) if v==ord('f')];assert len(cells)>=2
  pose(m,cells[0]);steps();steps(35);assert door(m,target)['phase']==1
  pose(m,cells[1]);steps();assert door(m,target)['phase']==1 # no new enter edge
  blank=next(i for i,v in enumerate(fc) if v==32 and f.read(world+17472+i,1)==b' ');pose(m,blank);steps();pose(m,cells[0]);steps();steps(35)
  assert door(m,target)['phase']==2,door(m,target)
  sensor_results.append(dict(level=level+1,cells=cells,target=target,closed=door(m,target)))
 # Every placed conveyor direction on the actual LEVEL2 ARM map.
 reset(1);m=model(1);conveyors=[];maps=[f.read(world+17472+i*4096,4096) for i in range(3)];defs={o['glyph']:o for o in m['objects']}
 for glyph in (ord('<'),ord('>'),ord('{'),ord('}')):
  fields={v['offset']:v.get('number') for v in defs[glyph]['fields']};delta=int(fields[104]/30);category=fields[102];neighbor_delta=(1 if delta>0 else -1)*(64 if category==102 else 1)
  cell=next(i for i,v in enumerate(maps[2]) if v==glyph and 0<=i+neighbor_delta<4096 and maps[0][i+neighbor_delta]==32 and defs[maps[1][i+neighbor_delta]]['flags']&16)
  pose(m,cell);before=struct.unpack('<iii',f.read(actor(m)+4,12));steps();after=struct.unpack('<iii',f.read(actor(m)+4,12));expected=(before[0]+(0 if category==102 else delta),before[1],before[2]+(delta if category==102 else 0));assert after==expected,(glyph,before,after,expected)
  conveyors.append(dict(glyph=glyph,cell=cell,before=before,after=after))
 # Finish event stays explicit for G18; no level reset occurs here.
 reset();m=model(0);fc=f.read(world+17472+8192,4096);exit_cell=fc.index(178);pose(m,exit_cell);steps();assert interaction()[3]==5,interaction()
 stack=stack_usage(f);meta=json.loads((build/'build-report.json').read_text());f.core.retro_reset();ct.memset(f.pointer(0x02000000,meta['ewram_static_bytes']),0xa5,meta['ewram_static_bytes']);ready()
 assert f.read(motion,20)==bytes(20) and interaction()[1]==0
 report=dict(task='G09',status='passed',rom_sha256=sha(build/'dok-diagnostic.gba'),scenarios=scenarios,live_start=live,sensors=sensor_results,conveyors=conveyors,active_door_language_preserved=True,finish_event=True,shared_arm_cases=True,poisoned_reset=True,stack=stack,captures=captures,live_frame_min_cycles=min(c for c,mode in cycles if not mode),live_frame_max_cycles=max(c for c,mode in cycles if not mode),test_frame_max_cycles=max(c for c,mode in cycles if mode),physical_gba_tested=False,original_bios_tested=False)
 report['original_affine_visual_oracle_checked']=not a.skip_visual_oracle
 a.report.write_text(json.dumps(report,indent=2)+'\n');f.core.retro_unload_game();f.core.retro_deinit();print('G09 mGBA door/collision, RU/EN, Start, sensors, finish and reset passed'+('; original affine visual oracle intentionally omitted' if a.skip_visual_oracle else ''))
if __name__=='__main__':main()
