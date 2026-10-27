"""Measure exact-camera reuse eligibility on the selected renderer, not a cache FPS."""
import argparse
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze,collect
from verify_audio_gba import sounds
from verify_sprites_gba import load_json
from verify_gba import sha


def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--build-dir',type=Path,required=True)
    a.add_argument('--report',type=Path,required=True);a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.until(lambda d:d['draws']>=4);rate=max(sounds()[0],key=lambda r:r['rate'])
        for scene,buttons in (('turn-fire',(8,7)),('walk-turn-fire',(8,4,7)),('stand-fire',(8,))):
            for level in range(3):
                freeze(p,True);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.language(0)
                model=load_json(f'research/evidence/runtime/ru/LEVEL{level+1}-runtime.json');hero=model['objects'][model['player_index']-1]['actor']
                p.camera((level+1)%3,15120,-300,22800,90);p.camera(level,hero['x'],hero['y'],hero['z'],hero['heading'])
                p.command(6);p.command(2,rate['id']);freeze(p,False);p.write(p.symbols['test_camera'],b'\0')
                collect(p,6,buttons=buttons);before=list(p.words('g_hw_static_work',4));d0=p.diag()['draws']
                result=collect(p,120,buttons=buttons);after=list(p.words('g_hw_static_work',4));draws=p.diag()['draws']-d0
                delta=[b-a for a,b in zip(before,after)]
                # Snapshots may see one in-flight next builder. Give the
                # eligibility bound one complete frame of conservative slack.
                upper=draws-max(0,min(draws,delta[2]-1))
                rows.append(dict(scene=scene,level=level+1,presentations=120,draw_delta=draws,
                    counters_before=before,counters_after=after,counter_delta=delta,
                    exact_camera_reuse_opportunities_upper_bound=upper,
                    baseline_fps=result['presented_fps'],audio_underruns=result['audio']['underruns'],
                    time_conservation_error_ticks=result['time_conservation_error_ticks']))
        a.report.write_text(json.dumps(dict(rom_sha256=sha(p.rom),script_sha256=sha(__file__),core_sha256=sha(core),rows=rows,
            cold_reset_after_freeze=True,scope='RU original start routes,120 live presentations per route with PCM, real moving camera. Existing static-cache camera miss counters give a conservative exact-camera reuse bound; this is not an implemented image cache, a reprojection experiment, or a cache FPS claim.'),indent=2)+'\n',encoding='utf-8')
        print([(r['scene'],r['level'],r['exact_camera_reuse_opportunities_upper_bound']) for r in rows])
    finally:p.close()


if __name__=='__main__':main()
