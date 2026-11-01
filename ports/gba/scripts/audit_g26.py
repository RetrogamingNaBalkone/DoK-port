"""Read existing GBA transport diagnostics at held completed frames."""
import json, os, struct
from pathlib import Path
from check_g26 import ROOT,OUT,CONTROL
from check_g22 import CASES
from research_gba_fps import GameProbe, freeze
from verify_gba import VIDEO,sha

def main():
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1';OUT.mkdir(parents=True,exist_ok=True)
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    result={}
    for name in ('control','ui','low'):
        build=CONTROL if name=='control' else ROOT/f'build/gba/g26/probes/g26_{name}_transport'
        p=GameProbe(core,build);rows=[]
        try:
            p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
            p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
            p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
            for lang in (0,1):
                p.language(lang)
                for case in CASES+['2:3039:90','3:1623:0','3:1940:270']:
                    level,cell,heading=map(int,case.split(':'))
                    p.camera(level%3,15120,-300,22800,90)
                    p.camera(level-1,cell%64*480+240,-300,cell//64*480+240,heading)
                    for _ in range(8):p.present()
                    r=dict(level=level,language=lang,cell=cell,heading=heading)
                    for symbol,count in [('g_hw_weapon',5),('g_hw_obj_diag',6),('g_hw_bg_diag',8),('g_hw_bg_prepare_cycles',1),('g_hw_present_diag',4),('g_hw_dma_guard',8),('g_hw_ui_obj_count',1),('hw_ui_dirty',4),('hw_weapon_dirty',3)]:
                        if symbol in p.symbols:r[symbol]=list(p.words(symbol,count))
                    r['dma_control']=[struct.unpack('<I',p.frontend.read(a,4))[0] for a in (0x040000b8,0x040000d0,0x040000dc)]
                    rows.append(r)
        finally:p.close()
        result[name]=dict(rom_sha256=sha(build/'dok-diagnostic.gba'),rows=rows)
    (OUT/'transport-state.json').write_text(json.dumps(result,indent=2)+'\n')
    print('54 held transport snapshots saved')
if __name__=='__main__':main()
