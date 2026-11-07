"""Inject a synthetic completed-transfer report; verify real deadline guard.

Explicit ARM9 code/RAM writes replace only nds_map_transfer.part.0 in this
private emulator instance. No DMA is executed by the replacement. The actual
render_one guard and fatal screen run unchanged; the ROM file is untouched.
"""
import ctypes as C
import json
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front


def main():
    target=Path(sys.argv[1]).resolve();directory=target/'map38-fault';directory.mkdir(exist_ok=True)
    elf=target/'dok_nds_gameplay.elf'
    f=front.FrontendObserver(front.CORE,target,directory);f.options['melonds_homebrew_sdcard']='disabled'
    report=dict(passed=False,scope=__doc__)
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8);f.wait_for(8);f.tick(30)
        probe,_=front.transport.elf_symbol(elf,'g_nds_map_probe')
        p=struct.unpack('<64I',front.render.ram(f,probe,256));profile,live=p[60:62]
        assert profile and live==profile+256+8*48*4
        code_at,code_bytes=front.transport.elf_symbol(elf,'nds_map_transfer.part.0')
        assert code_at%4==0 and code_bytes>=48
        bad=0xffff2001
        replacement=struct.pack('<12I',0xe59f1020,0xe59f2020,0xe5812010,0xe5812034,
            0xe3a020c0,0xe5812028,0xe581202c,0xe3a02000,0xe5812030,0xe12fff1e,profile,bad)
        pointer=f.core.retro_get_memory_data(2);size=f.core.retro_get_memory_size(2)
        def put(at,payload):
            offset=at-0x02000000;assert 0<=offset<=size-len(payload)
            C.memmove(pointer+offset,payload,len(payload))
        put(code_at,replacement)
        put(live+48*4,struct.pack('<I',2)) # explicit forced redraw
        for _ in range(60):f.core.retro_run()
        assert not f.callback_error and not f.shutdown
        renderer=front.render.observe(f,f.render_address,f.render_fields,0x37524b44)
        assert renderer['error_bits']==65536 and renderer['status']==3
        diagnostic,n=front.transport.elf_symbol(elf,'crash_diagnostic');assert n==188
        frozen=front.render.ram(f,diagnostic,n)
        count,stopped,valid,operation,status,a,b=struct.unpack_from('<7I',frozen)
        assert (stopped,valid,operation,status,a,b)==(1,1,12,8,bad,0x00c000c0)
        assert struct.unpack('<4I',front.render.ram(f,profile+10*4,16))==(192,192,0,bad)
        for _ in range(60):f.core.retro_run()
        assert front.render.ram(f,diagnostic,n)==frozen
        report.update(passed=True,status=status,elapsed=a,lines=b,events=count,
            captures=f.capture('map-duration'),rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    finally:
        f.close();front.transport.flush_native_stdio()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report))


if __name__=='__main__':main()
