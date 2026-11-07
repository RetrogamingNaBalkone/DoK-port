"""Short real-input reload/map smoke; no RAM writes or long stability claim."""
import json
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front


class Reloads(front.FrontendObserver):
    def check_pixels(self,p):pass # Full pixel/input/FAT oracle is a separate gate.


def main():
    target=Path(sys.argv[1]).resolve();directory=target/'map38-restarts';directory.mkdir(exist_ok=True)
    f=Reloads(front.CORE,target,directory);f.options['melonds_homebrew_sdcard']='disabled'
    elf=target/'dok_nds_gameplay.elf';started=time.monotonic()
    report=dict(passed=False,scope=__doc__,loads=[])
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        retry,_=front.transport.elf_symbol(elf,'retry_count')
        map_at,_=front.transport.elf_symbol(elf,'g_nds_map_probe')
        for level in (1,1,2,3,1,1):
            f.choose(2,0);f.tap(front.A);f.wait_for(3);f.choose(3,level-1);f.tap(front.A);f.text_to(7,8)
            f.tick(120)
            # Normal controls, release, and fresh map staging after every load.
            f.joypad=1<<8;f.tick(12);f.joypad=0;f.tick(12)
            p=struct.unpack('<64I',front.render.ram(f,map_at,256))
            assert p[31]==0 and p[33]>0 and not p[2]&1
            profile=struct.unpack('<14I',front.render.ram(f,p[60],56))
            report['loads'].append(dict(level=level,map_updates=p[33],map_fault=p[31],profile=list(profile)))
            f.tap(front.START);f.wait_for(9);f.choose(9,3);f.tap(front.A);f.wait_for(2)
        report.update(passed=True,frames=f.frames,clock_retries=struct.unpack('<I',front.render.ram(f,retry,4))[0],
                      last_renderer=f.report['last_renderer'],rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    finally:
        f.close();front.transport.flush_native_stdio();report['wall_seconds']=time.monotonic()-started
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(passed=report['passed'],loads=len(report['loads']),wall_seconds=report['wall_seconds'])))


if __name__=='__main__':main()
