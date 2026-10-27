"""UI01 second playtest: Select map and both reflected door leaves in mGBA."""
import argparse,hashlib,json,os,struct
from pathlib import Path
from verify_flow_gba import FlowProbe
from verify_gba import sha
from verify_items_gba import MAP_OFFSET,PLAYER_OFFSET
from verify_renderer_gba import stack_usage

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True);ap.add_argument('--report',type=Path,required=True)
    args=ap.parse_args();core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=FlowProbe(core,args.build_dir);cases=[];screens={}
    try:
        p.until(lambda d:d['draws']>=2);p.freeze();p.press(8)
        for language in (0,1):
            for level in range(3):
                p.new_game(level,language)
                screens[f'intro_{language}_{level}']=p.screen(f'playtest-intro-{language}-{level}')
                while p.flow()['state']==1:p.press(8)
                p.wait_flow(2)
                if not language and not level:
                    for phase in range(8):
                        p.frontend.run(phase)
                        p.frontend.run(1,(2,))
                        for _ in range(10):
                            if p.ui()['state']==7:break
                            p.frontend.run(1)
                        assert p.ui()['state']==7,('Lost one-frame Select pulse',phase,p.ui())
                        p.press();p.press(0);assert p.ui()['state']==0
                world=p.symbols['g_render_world'];visited=p.frontend.read(world+MAP_OFFSET+12288,4096)
                assert any(visited),'No exploration at the initial player cell'
                p.until(lambda d:p.ui()['state']==7,buttons=(2,));assert p.ui()['language']==language
                before=p.world_sha();tick=p.words('g_time_diag',3)[2];starts=p.words('g_audio_diag',8)[7]
                for _ in range(12):p.frontend.run(1,(2,))
                assert p.ui()['state']==7 and p.world_sha()==before and p.words('g_time_diag',3)[2]==tick
                screens[f'map_{language}_{level}']=p.screen(f'playtest-map-{language}-{level}')
                assert p.words('g_audio_diag',8)[7]==starts,'Map started menu music'
                p.press();p.press(0);assert p.ui()['state']==0 and p.world_sha()==before
                # Both chord orders: Select-first enters map, Start must still pause.
                p.press(2);p.press(2,3);assert p.ui()['state']==2
                p.press(0);assert p.ui()['state']==0
                p.press(2,3);assert p.ui()['state']==2;p.press(0)
                p.write(world+PLAYER_OFFSET+54,struct.pack('<H',1))
                p.press(2);screens[f'full_map_{language}_{level}']=p.screen(f'playtest-full-map-{language}-{level}')
                p.press(2);assert p.ui()['state']==0
                cases.append(dict(level=level+1,language=language,select_map=True,held_select_stays_open=True,
                    cancel_preserves_world=True,clock_paused=True,no_menu_music=True,chord_orders=True,map_pickup=True))
                p.to_title()
        # LEVEL1 has two adjacent placed panels sharing glyph d. The first has
        # a north wall and must reverse its U endpoints; its neighbour must not.
        p.new_game(0,0)
        while p.flow()['state']==1:p.press(8)
        p.wait_flow(2)
        p.camera(0,13200,-300,22560,90)
        model=p.runtime(0);index=next(i for i,o in enumerate(model['objects']) if o['glyph']==ord('d'))
        door=p.symbols['g_render_world']+4+36*index
        p.write(door+34,struct.pack('<h',30))
        frames={}
        for phase in (0,7,13,20,29):
            p.write(door+2,struct.pack('<h',phase));p.command(1,0)
            frames[phase]=p.frame()[0]
            screens[f'door_{phase}']=p.screen(f'playtest-door-{phase}')
        # The original x86 constructor supplies the reflection oracle. The
        # PPU must expose background at BOTH inner leaf edges at mid-phase,
        # while keeping both outer portions opaque. No C-derived UV oracle.
        def equal_patch(x0,x1):
            offsets=[y*240+x for y in range(50,82) for x in range(x0,x1)]
            return sum(frames[13][i]==frames[29][i] for i in offsets)/len(offsets)
        gaps={name:equal_patch(*bounds) for name,bounds in
              [('left_inner',(111,116)),('right_inner',(125,130)),('left_outer',(85,90)),('right_outer',(151,156))]}
        assert gaps['left_inner']>.95 and gaps['right_inner']>.95,gaps
        assert gaps['left_outer']<.5 and gaps['right_outer']<.5,gaps
        assert p.diag()['guard_ok']==1
        result=dict(status='passed',rom_sha256=sha(args.build_dir/'dok-diagnostic.gba'),script_sha256=sha(__file__),
                    map_cases=cases,door_background_match_fractions=gaps,screens=screens,
                    stack=stack_usage(p.frontend),framebuffer_guards=True,
                    original_reflection_evidence='build/g20b-playtest2/door-original.json',physical_gba_tested=False)
        args.report.write_text(json.dumps(result,indent=2)+'\n')
        print('Select map:6 worlds; held input, both pause chords, source door reflection passed')
    finally:p.close()
if __name__=='__main__':main()
