"""Real one-video-frame keypad pulses; never wait with a button held down."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import json
import os
from pathlib import Path
import struct
from verify_flow_gba import FlowProbe
from verify_gba import sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    rows=[];boot=[]
    for button in (8,3):  # libretro A, Start
        for phase in range(8):
            p=FlowProbe(core,args.build_dir)
            try:
                p.until(lambda d:d['draws']>=1)
                boot.append(p.frontend.video_frames)
                assert boot[-1]<=30,boot[-1]
                assert p.frontend.read(p.symbols['g_render_world'],4)==b'\0'*4,'World was eagerly loaded at boot'
                p.frontend.run(phase)
                def pulse(button,expected):
                    before=p.frontend.video_frames
                    focus=(p.ui()['focus']+1)%5 if button==5 else None
                    def ready():return p.ui()['state']==expected and (focus is None or p.ui()['focus']==focus)
                    p.frontend.run(1,(button,))
                    for _ in range(6):
                        if ready():break
                        p.frontend.run(1)
                    lag=p.frontend.video_frames-before
                    assert ready() and lag<=6,(button,phase,lag,p.ui())
                    p.frontend.run(3)
                    return lag
                splash=pulse(button,1)
                level=pulse(button,6)
                back=pulse(0,1)
                down=pulse(5,1)
                assert p.ui()['focus']==1
                pulse(5,1);assert p.ui()['focus']==2
                help_lag=pulse(button,3)
                help_back=pulse(0,1)
                pulse(5,1);assert p.ui()['focus']==3
                pulse(5,1);assert p.ui()['focus']==4
                pulse(5,1);assert p.ui()['focus']==0,'Removed statistics row is still reachable'
                rows.append(dict(button='A' if button==8 else 'Start',phase=phase,splash=splash,levels=level,
                                 cancel=back,down=down,help=help_lag,help_back=help_back))
            finally:p.close()
    result=dict(task='UI01 feedback',status='passed',rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),
                script_sha256=sha(__file__),boot_video_frames=boot,max_boot_seconds=max(boot)*280896/16777216,
                pulse_video_frames=1,max_input_to_ui_update_frames=max(max(r[k] for k in ('splash','levels','cancel','down','help','help_back')) for r in rows),
                rows=rows,menu_statistics_removed=True,lazy_world_creation=True,physical_gba_tested=False)
    args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('One-frame taps:',len(rows),'cases; boot',max(boot),'frames; max UI update latency',result['max_input_to_ui_update_frames'])


if __name__=='__main__':main()
