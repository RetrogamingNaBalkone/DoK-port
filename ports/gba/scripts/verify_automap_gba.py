"""Actual mGBA PPU: map item gate, scroll/recenter, frozen world and clock."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse,json,os,struct
from pathlib import Path
from verify_flow_gba import FlowProbe
from verify_items_gba import MAP_OFFSET,PLAYER_OFFSET
from verify_gba import sha
from verify_renderer_gba import stack_usage

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True);ap.add_argument('--report',type=Path,required=True)
 args=ap.parse_args();p=FlowProbe(Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll',args.build_dir)
 cases=[];screens={}
 try:
  p.until(lambda d:d['draws']>=2);p.freeze()
  if 'g_camera_capture_hold' in p.symbols:p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
  p.press(8)
  for language in (0,1):
   for level in range(3):
    p.new_game(level,language)
    while p.flow()['state']==1:p.press(8)
    p.wait_flow(2);world=p.symbols['g_render_world']
    # Explicitly explored plan isolates item-gated symbols from discovery.
    p.write(world+MAP_OFFSET+12288,bytes([1]*4096))
    p.press(2);assert p.ui()['state']==7
    # Remove test freezes while the map is open: production pause alone must
    # stop actors, combat and animation, as well as the independent clock.
    for name in ('g_motion_probe','g_visual_probe','g_combat_probe','g_ai_probe'):p.write(p.symbols[name],struct.pack('<I',0))
    before=p.world_sha();ticks=p.words('g_time_diag',3)[2];starts=p.words('g_audio_diag',8)[7]
    initial,palette=p.frame();screens[f'without_{language}_{level}']=p.screen(f'dos-map-without-{language}-{level}')
    p.press(4);p.press(4);p.press(6);p.press(6)
    shifted,_=p.frame();assert shifted[:144*240]!=initial[:144*240]
    p.frontend.run(30,(5,7));p.press()
    assert p.world_sha()==before and p.words('g_time_diag',3)[2]==ticks
    assert p.words('g_audio_diag',8)[7]==starts
    p.press(8);centered,_=p.frame();assert centered==initial,'A did not recenter exactly'
    p.write(world+PLAYER_OFFSET+54,struct.pack('<H',1));before=p.world_sha()
    revealed,_=p.frame();assert revealed[:144*240]!=initial[:144*240],('Missing item symbols',language,level)
    screens[f'with_{language}_{level}']=p.screen(f'dos-map-with-{language}-{level}')
    assert p.world_sha()==before and p.words('g_time_diag',3)[2]==ticks
    # Select closes; B closes; Start+Select keeps both chord orders.
    p.freeze()
    p.press(2);assert p.ui()['state']==0
    p.press(2);p.press(0);assert p.ui()['state']==0
    p.press(2);p.press(2,3);assert p.ui()['state']==2;p.press(0)
    p.press(2,3);assert p.ui()['state']==2;p.press(0)
    assert p.diag()['guard_ok']==1
    cases.append(dict(language=language,level=level+1,item_gate=True,scroll=True,recenter=True,world_paused=True,clock_paused=True,exits=True,pause_chords=True))
    p.to_title()
  result=dict(status='passed',rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),script_sha256=sha(__file__),cases=cases,screens=screens,stack=stack_usage(p.frontend),physical_gba_tested=False)
  args.report.write_text(json.dumps(result,indent=2)+'\n');print('DOS map: six worlds, item gate, scrolling, recenter, frozen simulation and clock passed')
 finally:p.close()
if __name__=='__main__':main()
