"""Native sector-fed OBJ admission, completed timing and actual PPU captures."""
import argparse
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha
from verify_renderer_gba import stack_usage


def main():
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--build-dir',type=Path,required=True)
    a.add_argument('--report',type=Path,required=True);a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        p.camera(1,15120,-300,22800,90);p.write(p.symbols['g_render_work'],struct.pack('<I',1))
        for level,cell,heading in [(1,520,225),(1,3640,225),(2,1592,135),(2,2871,225),(3,1544,315)]:
            p.camera(level-1,cell%64*480+240,-300,cell//64*480+240,heading)
            for _ in range(4):d=p.present()
            work=list(p.words('g_sector_gpu_work',12));ticks=p.words('g_sector_ticks',5)
            assert d['guard_ok']==1
            state=p.words('g_sector_gpu',1)[0];meta=struct.unpack('<6I',p.frontend.read(state,24))
            gpu_cycles=[n*64 for n in p.words('g_sector_gpu_ticks',3)]
            shear_work=list(p.words('g_sector_gpu_shear_work',8)) if 'g_sector_gpu_shear_work' in p.symbols else None
            image=a.build_dir/f'gpu-L{level}-{cell}-{heading}.png';p.frontend.run(2);p.frontend.screenshot(image)
            rows.append(dict(level=level,cell=cell,heading=heading,cycles=d['last_cycles'],work=work,state=meta,
                shear_work=shear_work,
                sector_cycles=[n*64 for n in ticks],gpu_cycles=gpu_cycles,
                image=str(image),image_sha256=sha(image)))
            print(level,cell,'state',meta,'work',work,'cycles',rows[-1]['gpu_cycles'],flush=True)
        a.report.write_text(json.dumps(dict(rom_sha256=sha(p.rom),core_sha256=sha(core),script_sha256=sha(__file__),rows=rows,
            stack=stack_usage(p.frontend),forced_pcm=False,cold_reset_after_freeze=True,
            scope='Instrumented held native frames; not unprofiled FPS, full gameplay or physical GBA acceptance.'),indent=2)+'\n',encoding='utf-8')
    finally:p.close()


if __name__=='__main__':main()
