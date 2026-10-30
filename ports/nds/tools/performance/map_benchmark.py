"""Paired ARM9 raster-only fixture: actual maps/glyphs, separate all-visited input."""
import argparse
import json
from pathlib import Path
import struct
import sys
import traceback
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from run_routes import RouteObserver,ROOT,front,transport,render,pin

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',type=Path,required=True);parser.add_argument('--level',type=int,choices=(1,2,3),required=True)
    args=parser.parse_args();target=args.target.resolve();assert target.is_relative_to(ROOT/'build/nds/n23')
    directory=target/f'map-benchmark-level{args.level}';assert not directory.exists();directory.mkdir()
    f=None;report=dict(task='N23',passed=False,scope='Synthetic all-visited raster workload with original world maps/glyphs; never writes game visited')
    try:
        f=RouteObserver(front.CORE,target,directory)
        address,size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_n23_map_bench');assert size==672
        image=f.save/'melonDS DS/dldi_sd_card.bin';front.write_fixture(image,front.settings_bytes(1))
        f.load(target/'dok_nds_gameplay.nds');f.start_level(args.level)
        values=struct.unpack('<168I',render.ram(f,address,size));assert values[:4]==(0x234d4b44,1,40,args.level)
        rows=[dict(zip(('scale','view','cpu_ticks','pixels_fnv1a'),values[8+n*4:12+n*4]))for n in range(40)]
        try:phase_address,phase_bytes=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_n23_map_phases')
        except Exception:phase_address=None
        if phase_address:
            assert phase_bytes==640
            phases=struct.unpack('<160I',render.ram(f,phase_address,phase_bytes))
            for n,row in enumerate(rows):row['phases']=dict(zip(('background_bounds','floor','objects','arrow'),phases[n*4:n*4+4]))
        report.update(passed=True,rom=pin(target/'dok_nds_gameplay.nds'),elf=pin(target/'dok_nds_gameplay.elf'),
            runner=pin(Path(__file__)),rows=rows,level=args.level,width=values[5],length=values[6],timer_hz=values[4],
            total_ticks=sum(row['cpu_ticks']for row in rows),max_ticks=max(row['cpu_ticks']for row in rows),memory=f.renderer)
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({key:value for key,value in report.items()if key not in('rows','memory')}))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
