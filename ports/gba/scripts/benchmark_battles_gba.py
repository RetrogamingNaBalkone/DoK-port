"""G20/G21 original-map mass battles: real ARM/PPU cadence, no AI limits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT / 'ports/gba/scripts'))
from profile_game_gba import GameProbe, collect, freeze
from verify_combat_gba import actor_pose
from verify_gba import VIDEO, sha
from verify_renderer_gba import stack_usage
from verify_audio_gba import irq_stack

# All three LEVEL3 keys are covered so order of collection is unambiguous.
CASES = ((2,31,47,90,'last-key'), (3,23,25,0,'west-key'),
         (3,53,52,0,'southeast-key'), (3,51,10,0,'northeast-key'),
         (3,20,30,270,'shotgun-witches'))

def measure(build, core, samples, languages, fire=False):
    p=GameProbe(core,build); rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2)
        freeze(p,True)
        for lang in languages:
            p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));freeze(p,True);p.present()
            p.language(lang)
            for lv,x,z,heading,name in CASES:
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));freeze(p,True);p.present()
                p.camera(lv%3,15120,-300,22800,90)
                p.camera(lv-1,x*480+240,-300,z*480+240,heading)
                model=json.loads((ROOT/f'research/evidence/runtime/ru/LEVEL{lv}-runtime.json').read_text())
                actor_pose(p,model,model['player_index']-1,x*480+240,z*480+240,heading)
                player=p.player();player[0]=player[2]=32767;p.set_player(player)
                p.write(p.symbols['test_camera'],b'\0')
                frozen=collect(p,6)
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present();p.frontend.run(2)
                frozen['ppu_sha256']=hashlib.sha256(p.frontend.last_video_raw).hexdigest()
                frozen['geometry_fnv1a']=p.diag()['geometry_fnv1a']
                if fire:
                    p.control(1,1,32767)
                work=p.words('g_ai_work',4)
                freeze(p,False)
                timing=collect(p,samples,buttons=(8,) if fire else ())
                assert all(s['flow_state']==2 and s['ui_state']==0 for s in timing['samples'])
                assert timing['max_motion_debt']<416,timing['max_motion_debt']
                assert timing['time_conservation_error_ticks']==0,timing['time_conservation_error_ticks']
                after=p.words('g_ai_work',4)
                def stages(t):
                    return {k:statistics.median(s['stages'][k] for s in t['samples']) for k in t['samples'][0]['stages']}
                row=dict(language=lang,level=lv,cell=[x,z],heading=heading,name=name,
                         frozen=frozen,live=timing,frozen_stages=stages(frozen),live_stages=stages(timing),
                         ai_work=dict(lookups=after[0]-work[0],sights=after[1]-work[1],longest_ray=after[2],capped=after[3]-work[3]),
                         ai=p.ai_diag(),stack=stack_usage(p.frontend),irq_stack=irq_stack(p))
                rows.append(row)
                print(lang,lv,name,'FPS',round(timing['presented_fps'],2),'stages',row['live_stages'],'AI',row['ai_work'],flush=True)
        return dict(rom_sha256=sha(p.rom),rows=rows)
    finally:p.close()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    ap.add_argument('--samples',type=int,default=120)
    ap.add_argument('--languages',type=int,nargs='+',default=[0,1])
    ap.add_argument('--fire',action='store_true',help='Equip shotgun/ammo32767 and hold real A')
    a=ap.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    assert a.samples>=2
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    result=measure(a.build_dir,core,a.samples,a.languages,a.fire)
    result.update(script_sha256=sha(__file__),core_sha256=sha(core),samples=a.samples,fire=a.fire,
                  scope='Stationary hero relocated into original rooms; original maps/enemies, HP/armor32767. '
                        'Natural renderer consideration/AI wake, normal clock and audio. '
                        'Different frame times change live AI poses; frozen PPU pairs are exact comparisons. '
                        'mGBA, not physical GBA or a full playthrough.')
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
