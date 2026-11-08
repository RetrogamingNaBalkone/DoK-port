"""Physical weapon/FPS keys and LCD restoration; explicit inventory fixture."""
import json
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'flow'))
from check_death39_emulator import layout, put
from run_routes import RouteObserver, front, render, transport


def main():
    target=Path(sys.argv[1]).resolve();elf=target/'dok_nds_gameplay.elf'
    off=layout(elf);directory=target/'controls41';directory.mkdir(exist_ok=True)
    report=dict(passed=False,scope=__doc__,schemes=[])
    for scheme in (0,1):
        f=RouteObserver(front.CORE,target,directory/str(scheme))
        image=f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image,front.settings_bytes(1,scheme),embedded=True)
        try:
            f.load(target/'dok_nds_gameplay.nds');f.start_level(1);f.hold([],60)
            storage=struct.unpack('<I',render.ram(f,transport.elf_symbol(elf,'storage')[0],4))[0]
            # Two real source weapon definitions, exposed in inventory only.
            # No target code calls, clock edits or gameplay saves.
            indices=struct.unpack('<256H',render.ram(f,storage+off('geometry','initial','glyph_index'),512))
            epoch=f.probe[45]
            for slot,glyph in ((1,86),(2,87)):
                assert indices[glyph]
                put(f,storage+off('game','items','inventory','weapons')+slot*12,
                    struct.pack('<III',epoch,indices[glyph],1))
            for field in ('selected','pending'):
                put(f,storage+off('game','items','inventory',field),struct.pack('<H',1))
            f.hold([],90)
            assert f.probe[51]==1
            def tile():
                width,height,rgb=f.rgb();assert width==256 and height==384
                return b''.join(rgb[(y*width+188)*3:(y*width+212)*3]for y in range(2,12))
            shown=tile();assert f.probe[39]==1
            f.joypad=1<<front.SELECT;f.tick(90)
            assert f.probe[39]==0 and f.probe[51]==1
            hidden=tile();assert hidden!=shown,'FPS pixels remained after hiding'
            captures=f.capture('fps-hidden-weapon1')
            f.joypad=0;f.tick(8);f.tap(front.SELECT);f.tick(12)
            assert f.probe[39]==1 and f.probe[51]==1 and tile()==shown
            captures+=f.capture('fps-shown-weapon1')
            # Hide while retaining the weapon overlay, then change its bitmap.
            f.tap(front.SELECT);f.tick(8);assert f.probe[39]==0
            weapon_button=front.A if scheme else front.B
            f.tap(weapon_button);f.tick(180)
            assert f.probe[51]==2 and f.probe[39]==0
            captures+=f.capture('fps-hidden-weapon2')
            f.tap(front.SELECT);f.tick(12);assert f.probe[39]==1 and tile()==shown
            f.tap(weapon_button);f.tick(180);assert f.probe[51]==1
            f.tap(front.START);f.wait_for(9);f.tap(front.SELECT);assert f.probe[39]==1
            f.tap(front.START);f.wait_for(8);f.tick(8);assert f.probe[39]==1
            report['schemes'].append(dict(scheme=scheme,weapon_button='A'if scheme else'B',
                fps_button='Select',captures=captures,weapon_cycle=[1,2,1],hold_frames=90,
                errors=dict(frontend=f.probe[30],game=list(f.game[10:13]))))
        finally:f.close();transport.flush_native_stdio()
    report.update(passed=True,rom_sha256=transport.sha(target/'dok_nds_gameplay.nds'))
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,schemes=len(report['schemes']))))


if __name__=='__main__':main()
