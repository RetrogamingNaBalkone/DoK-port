"""Read local ARM9 rational-clock arithmetic benchmark entered by real menu input."""
import argparse
import json
from pathlib import Path
import struct
import sys
import traceback
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from run_routes import RouteObserver,ROOT,front,transport,render,pin

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--target',type=Path,required=True)
    args=parser.parse_args();target=args.target.resolve();assert target.is_relative_to(ROOT/'build/nds/n23')
    directory=target/'clock-benchmark';assert not directory.exists();directory.mkdir()
    f=None;report=dict(task='N23',passed=False,scope='Deterministic synthetic local clock arithmetic; no gameplay or hardware FPS claim')
    try:
        f=RouteObserver(front.CORE,target,directory)
        address,size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_n23_clock_bench');assert size==64
        image=f.save/'melonDS DS/dldi_sd_card.bin';front.write_fixture(image,front.settings_bytes(1))
        f.load(target/'dok_nds_gameplay.nds');f.start_level(1)
        values=struct.unpack('<16I',render.ram(f,address,size));assert values[:3]==(0x23434b44,1,10000)
        report.update(passed=True,rom=pin(target/'dok_nds_gameplay.nds'),elf=pin(target/'dok_nds_gameplay.elf'),
            runner=pin(Path(__file__)),raw=list(values),steps=values[2],cpu_ticks=values[3],trace_fnv1a=values[4],
            final_ticks=values[5],fraction=values[6]+(values[7]<<32),counter=values[8]+(values[9]<<32),timer_hz=values[10],
            milliseconds=values[3]*1000/values[10],memory=f.renderer)
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({key:value for key,value in report.items()if key not in('raw','memory')}))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
