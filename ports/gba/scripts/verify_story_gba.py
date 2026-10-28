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
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=FrontendProbe(core,args.build_dir)
    screens=[]
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
            before=p.world_sha();resets=p.flow()['resets'];starts=p.audio()['starts']
            p.press(8)
            assert p.flow()['state']==7 and p.flow()['page']==0
            assert p.audio()['active'] and p.audio()['starts']==starts
            # Back from page one returns to the same menu item.
            p.press(0)
            assert p.flow()['state']==0 and p.ui()['focus']==3
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
            assert p.world_sha()==before and p.flow()['resets']==resets
            assert p.audio()['active'] and p.audio()['starts']==starts
            screens[-1]['menu']=menu
        assert p.flow()['errors']==0
        result=dict(status='passed',rom_sha256=sha(p.rom),screens=screens,
                    original_text_pixels=True,world_unchanged=True,music_uninterrupted=True,
                    physical_gba_tested=False)
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print('Story RU/EN: original pixels, paging/back/locale, world and music passed')
    finally:p.close()


if __name__=='__main__':main()
