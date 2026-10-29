"""Two credits screens: real keypad, original font pixels, menu/music restore."""

import argparse
import json
import os
from pathlib import Path

from verify_frontend_gba import FrontendProbe
from verify_gba import sha
from verify_sprites_gba import original_animations
from verify_ui_gba import glyph_mask


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args()
    os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=FrontendProbe(core,args.build_dir)
    screens=[];exits=[]
    try:
        p.until(lambda d:d['draws']>=2)
        p.press(8)
        for language in (0,1):
            p.command(2,language)
            p.menu_focus(4)
            resources,_=original_animations(language)
            label=('CREDITS' if language else 'ОБ АВТОРАХ').encode('cp866')
            glyph_mask(p.frame()[0],resources[b'FONT8'.hex()],18,102,label,86)
            menu=p.screen(f'credits-menu-{language}');menu_frame=p.frame()
            before=p.world_sha();flow=p.flow();audio=p.audio()
            p.press(8)
            assert p.flow()['state']==5 and p.flow()['page']==0 and p.flow()['pages']==2
            # Changing locale must retain the selected original/port screen.
            for page in (0,1):
                if page:p.press(8)
                p.press(2);assert p.ui()['language']==1-language and p.flow()['page']==page
                p.press(2);assert p.ui()['language']==language and p.flow()['page']==page
            p.press(0);assert p.flow()['page']==0
            p.source_screens('ENDTXT',language,screens)
            assert p.flow()['state']==0 and p.ui()['focus']==4 and p.frame()==menu_frame
            # First-page B and final-page A/Start, short taps in each drawing
            # phase and held exits. Check all pixels/palette, not just state.
            for button,page in ((0,0),(8,1),(3,1)):
                for duration in (1,20):
                    for phase in range(8 if duration==1 else 1):
                        p.press(8);p.frontend.run(20)
                        assert p.flow()['state']==5 and p.flow()['page']==0
                        if page:p.press(3);p.frontend.run(20)
                        assert p.flow()['page']==page
                        p.frontend.run(phase)
                        p.frontend.run(duration,(button,))
                        if duration>1:
                            assert p.flow()['state']==0 and p.ui()['focus']==4
                        for lag in range(60):
                            if p.flow()['state']==0 and p.ui()['state']==1:break
                            p.frontend.run(1)
                        p.frontend.run(3)
                        assert p.flow()['state']==0 and p.ui()['focus']==4
                        assert p.frame()==menu_frame,'Credits exit left stale text or palette'
                        p.frontend.run(20)
                        exits.append(dict(language=language,button={0:'B',8:'A',3:'Start'}[button],
                                          page=page,held_video_frames=duration,phase=phase))
            assert p.world_sha()==before and p.flow()['resets']==flow['resets']
            assert p.flow()['session']==flow['session']
            current=p.audio()
            assert current['active'] and current['starts']==audio['starts']
            assert current['stops']==audio['stops'] and current['underruns']==audio['underruns']
            screens[-1]['menu']=menu
        result=dict(status='passed',rom_sha256=sha(p.rom),script_sha256=sha(__file__),
                    screens=screens,exit_cases=exits,menu_pixels_and_palette_restored=True,
                    original_text_and_port_attribution_pixels=True,world_unchanged=True,
                    music_uninterrupted=True,physical_gba_tested=False)
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print('Credits RU/EN: two screens, original/port pixels, paging/locale/back, 54 exits, music passed')
    finally:p.close()


if __name__=='__main__':main()
