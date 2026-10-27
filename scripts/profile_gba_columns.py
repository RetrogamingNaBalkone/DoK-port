"""Completed held-frame attribution for the full column visibility prototype."""
import argparse
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha
from verify_renderer_gba import stack_usage


def main():
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('--build-dir',type=Path,required=True);a.add_argument('--report',type=Path,required=True)
    a=a.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,a.build_dir);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        p.write(p.symbols['g_render_work'],struct.pack('<I',1))
        previous_level=0
        for level,cell,heading in [(1,520,225),(1,3640,225),(2,1592,135),(2,2871,225),(3,1544,315)]:
            if level!=previous_level:p.camera(level%3,15120,-300,22800,90)
            previous_level=level
            p.camera(level-1,cell%64*480+240,-300,cell//64*480+240,heading)
            for _ in range(6):d=p.present()
            assert d['phase']==2 and d['guard_ok']==1
            ticks=list(p.words('g_column_ticks',4));work=list(p.words('g_column_work',8))
            assert work[0]==1 and work[1]==120 and work[7]==0,(level,cell,work)
            image=a.build_dir/f'columns-L{level}-cell{cell}-{heading}.png';p.frontend.run(2);p.frontend.screenshot(image)
            rows.append(dict(level=level,cell=cell,heading=heading,cycles=d['last_cycles'],work=work,
                attributed_cycles=dict(zip(('prepare','visibility','vertical_output','ceiling_output'),[n*64 for n in ticks])),
                image=str(image),image_sha256=sha(image)))
            print(level,cell,work,rows[-1]['attributed_cycles'],flush=True)
        result=dict(rom_sha256=sha(p.rom),core_sha256=sha(core),script_sha256=sha(__file__),rows=rows,stack=stack_usage(p.frontend),
            forced_pcm=False,cold_reset_after_freeze=True,
            scope='Instrumented completed held draws, frozen simulation, original maps. No forced PCM workload. Timer64 cycles/tick, overhead included; not production FPS or gameplay acceptance.')
        a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    finally:p.close()


if __name__=='__main__':main()
