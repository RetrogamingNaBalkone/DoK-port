"""PERF05 native class attribution and diagnostic removal of ammo/witches."""
import argparse
import json
import os
from pathlib import Path
import statistics
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'ports/gba/scripts'))
from profile_game_gba import GameProbe,freeze,collect
from verify_gba import VIDEO,sha


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--out',type=Path,default=Path('research/evidence/gba-perf05'))
    a=ap.parse_args();os.chdir(ROOT);a.out.mkdir(parents=True,exist_ok=True)
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    model=json.loads(Path('research/evidence/runtime/ru/LEVEL2-runtime.json').read_text())
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        for x,z,h in [(50,41,270),(51,41,270),(51,41,315),(55,41,270),(55,41,90),(14,48,135)]:
            for removed in [(),(41,),(174,),(41,174)]:
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
                p.camera(0,15120,-300,22800,90);p.camera(1,x*480+240,-300,z*480+240,h)
                for glyph in removed:
                    index=next(i for i,o in enumerate(model['objects']) if o['glyph']==glyph)
                    addr=p.symbols['g_render_world']+4+index*36
                    flags=struct.unpack('<H',p.frontend.read(addr,2))[0]
                    p.write(addr,struct.pack('<H',flags|0x400))
                p.write(p.symbols['hw_static_active'],struct.pack('<I',0))
                p.write(p.symbols['g_render_work'],struct.pack('<I',0))
                collect(p,6);t=collect(p,18)
                p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1));p.present()
                if 'g_perf05_work' in p.symbols:
                    p.write(p.symbols['g_render_work'],struct.pack('<I',1));p.present()
                    work=list(p.words('g_perf05_work',32));kernel=list(p.words('g_kernel_work',4))
                else:work=kernel=None
                p.frontend.run(2)
                label=f'{x}-{z}-{h}-'+('-'.join(map(str,removed)) or 'native')
                if not removed:p.frontend.screenshot(a.out/(label+'.png'))
                row=dict(cell=[x,z],heading=h,removed_glyphs=removed,median=t['median'],fps=t['presented_fps'],
                         stages={k:statistics.median(s['stages'][k] for s in t['samples']) for k in t['samples'][0]['stages']},
                         class_ticks_calls=work,kernel_ticks_calls=kernel)
                rows.append(row);print(label,row['median'],row['fps'],work,kernel,flush=True)
        result=dict(rom_sha256=sha(p.rom),core_sha256=sha(core),rows=rows,
                    scope='Frozen diagnostic removals, not an optimization or altered shipping map. Class/kernel timers instrumented; no per-primitive timers during median CPU collection. Tick=64 cycles.')
        (a.out/'attribution.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    finally:p.close()


if __name__=='__main__':main()
