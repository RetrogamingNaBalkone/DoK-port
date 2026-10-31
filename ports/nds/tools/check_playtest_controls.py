"""Real joypad input and read-only ARM9 pose checks for release drift."""
import json
import struct
from pathlib import Path
import sys
import run_frontend_emulator as front

def main():
    target=Path(sys.argv[1]).resolve()
    directory=target/'playtest-controls'
    f=front.FrontendObserver(front.CORE,target,directory)
    rows=[]
    try:
        image=f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image,None,embedded=True)
        f.load(target/'dok_nds_gameplay.nds')
        front.smoke_flow(f,'world-smoke',1)
        address,_=front.transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_map_probe')
        def pose():
            p=struct.unpack('<64I',front.render.ram(f,address,256))
            assert not p[2]&1
            # Raster snapshots are intentionally throttled to 12 Hz. Read the
            # per-presentation live pose rather than a delayed map redraw.
            live=struct.unpack('<48I',front.render.ram(f,p[61],192))
            return list(live[6:9])
        for name,button in (('forward',front.UP),('backward',front.DOWN),('strafe-right',7),('strafe-left',6),
                            ('turn-right',front.A),('turn-left',front.Y)):
            before=pose()
            f.joypad=1<<button;f.tick(20)
            during=pose()
            f.joypad=0;f.tick(6)
            stopped=pose()
            for _ in range(20):
                f.tick(2)
                assert pose()==stopped,(name,'release drift',stopped,pose())
            assert during!=before,(name,'input produced no movement')
            rows.append(dict(input=name,before=before,during=during,stopped=stopped,no_drift_video_frames=40))
        f.capture('final')
    finally:f.close()
    report=dict(passed=True,rows=rows,rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))

if __name__=='__main__':main()
