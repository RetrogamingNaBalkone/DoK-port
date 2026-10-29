"""Compare actual PPU for every original animation frame near/far, RU/EN."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.append(str(ROOT/'ports/gba/scripts'))
from research_gba_fps import GameProbe, freeze
from verify_gba import VIDEO, sha
from verify_sprites_gba import original_animations, load_json
from probe_render_layout import frames


def capture(build,core):
    p=GameProbe(core,build);rows=[]
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        p.camera(1,15120,-300,22800,90);p.camera(0,15120,-300,22800,90)
        manifest=load_json('build/gba/assets/manifest.json')
        for language in (0,1):
            p.language(language)
            originals,source=original_animations(language)
            for resource in manifest['resources']:
                raw=originals.get(resource['name_hex'])
                if raw is None:continue
                try:sequence=frames(raw)
                except (ValueError,AssertionError,struct.error):continue
                for index,(metadata,pixels) in enumerate(sequence):
                    for depth in (600,2600):
                        p.present()
                        # A fixture billboard reuses the player definition's
                        # width scale; original maps and source images remain.
                        p.write(p.symbols['g_render_world']+4+2,struct.pack('<H',index))
                        p.write(p.symbols['g_render_world']+4+14,struct.pack('<H',resource['id']))
                        p.write(p.symbols['g_sprite_probe'],struct.pack('<5i',1,0,15120-depth,0,22800))
                        try:
                            p.present();d=p.present()
                        except AssertionError as error:
                            raise AssertionError((language,resource['id'],resource['name_hex'],index,depth,metadata)) from error
                        coverage=p.frontend.read(p.symbols['coverage_bits'],120*16)
                        p.frontend.run(2)
                        rows.append(dict(language=language,resource=resource['id'],frame=index,depth=depth,
                            original_frame_sha256=hashlib.sha256(pixels).hexdigest(),
                            video_sha256=hashlib.sha256(p.frontend.last_video_raw).hexdigest(),
                            coverage_sha256=hashlib.sha256(coverage).hexdigest(),geometry=d['geometry_fnv1a']))
            assert sha(source['path'])==source['sha256']
        return dict(rom_sha256=sha(p.rom),rows=rows)
    finally:p.close()


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--reference-build',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True);args=ap.parse_args()
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    before=capture(args.reference_build,core);after=capture(args.build_dir,core)
    assert before['rows']==after['rows'],'Animation PPU/coverage/geometry differs'
    assert len(after['rows'])==616,len(after['rows'])
    result=dict(status='passed',rom_sha256=after['rom_sha256'],script_sha256=sha(__file__),
                core_sha256=sha(core),before=before,after=after,frame_cases=len(after['rows']),
                scope='All 308 original localized animation frames at depths600/2600, '
                      'actual PPU/coverage/geometry equality in a frozen original-map fixture. '
                      'Not a full game or physical GBA test.')
    args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('Animation PPU equality:',len(after['rows']),'near/far frame cases')


if __name__=='__main__':main()
