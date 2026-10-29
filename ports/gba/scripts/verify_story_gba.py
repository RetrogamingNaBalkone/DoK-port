"""Original README story, real menu navigation and pixel checks in mGBA."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import json
import os
from pathlib import Path
from verify_frontend_gba import FrontendProbe
from verify_gba import sha
from verify_ui_gba import glyph_mask
from verify_sprites_gba import original_animations


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
        assert p.ui()['state']==1
        for language in (0,1):
            p.command(2,language)
            # Reach the story through real keys; hold entry to check debouncing.
            while p.ui()['focus']!=3:p.press(5)
            resources,_=original_animations(language)
            label=b'STORY' if language else 'ПРЕДЫСТОРИЯ'.encode('cp866')
            glyph_mask(p.frame()[0],resources[b'FONT8'.hex()],18,88,label,86)
            menu=p.screen(f'story-menu-{language}')
            menu_frame=p.frame()
            before=p.world_sha();resets=p.flow()['resets'];starts=p.audio()['starts']
            p.press(8)
            assert p.flow()['state']==7 and p.flow()['page']==0
            assert p.audio()['active'] and p.audio()['starts']==starts
            # Back from page one returns to the same menu item.
            p.press(0)
            assert p.flow()['state']==0 and p.ui()['focus']==3
            assert p.frame()==menu_frame,'Story exit left stale text instead of the menu'
            p.press(3)
            assert p.flow()['state']==7
            # Switch locale on the text screen, then restore it.
            p.press(2);assert p.ui()['language']==1-language
            p.press(2);assert p.ui()['language']==language
            assert p.flow()['page']==0
            p.press(8);assert p.flow()['page']==1
            p.press(0);assert p.flow()['page']==0
            p.source_screens('README',language,screens)
            assert p.flow()['state']==0 and p.ui()['state']==1 and p.ui()['focus']==3
            assert p.frame()==menu_frame,'Last story page remained visible after exit'
            # Exercise real keypad exits at every drawing phase, including
            # one-video-frame taps and a held button that must not reopen STORY.
            for button,page in ((0,0),(8,1),(3,1)):
                for duration in (1,20):
                    for phase in range(8 if duration==1 else 1):
                        p.press(8)
                        p.frontend.run(20)  # Drain IRQ-latched entry before another A edge.
                        if page:
                            p.press(8)
                            p.frontend.run(20)
                        assert p.flow()['state']==7 and p.flow()['page']==page, (language,button,page,duration,phase,p.flow(),p.ui())
                        p.frontend.run(phase)
                        p.frontend.run(duration,(button,))
                        if duration>1:
                            assert p.flow()['state']==0 and p.ui()['focus']==3,'Held exit reopened story'
                        for lag in range(60):
                            if p.flow()['state']==0 and p.ui()['state']==1:break
                            p.frontend.run(1)
                        p.frontend.run(3)
                        assert p.flow()['state']==0 and p.ui()['state']==1 and p.ui()['focus']==3, (language,button,page,duration,phase,p.flow(),p.ui())
                        assert p.frame()==menu_frame,'Keypad exit did not restore all menu pixels/palette'
                        p.frontend.run(20)
                        exits.append(dict(language=language,button={0:'B',8:'A',3:'Start'}[button],
                                          page=page,held_video_frames=duration,phase=phase,settle_frames=lag))
            assert p.world_sha()==before and p.flow()['resets']==resets
            assert p.audio()['active'] and p.audio()['starts']==starts
            screens[-1]['menu']=menu
        assert p.flow()['errors']==0
        result=dict(status='passed',rom_sha256=sha(p.rom),screens=screens,
                    script_sha256=sha(__file__),exit_cases=exits,menu_pixels_and_palette_restored=True,
                    original_text_pixels=True,world_unchanged=True,music_uninterrupted=True,
                    physical_gba_tested=False)
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print('Story RU/EN: original pixels, paging/back/locale, world and music passed')
    finally:p.close()


if __name__=='__main__':main()
