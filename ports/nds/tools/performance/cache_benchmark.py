"""ARM9 paired synthetic resident-frame workload, entered by physical menu input."""
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
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--level',type=int,choices=(1,2,3),default=1)
    args=parser.parse_args();target=args.target.resolve()
    assert target.is_relative_to(ROOT/'build/nds/n23')
    directory=target/f'cache-benchmark-level{args.level}'
    assert not directory.exists();directory.mkdir()
    report=dict(task='N23',passed=False,scope='Synthetic ordered real resident payload requests; no gameplay/FPS claim')
    f=None
    try:
        f=RouteObserver(front.CORE,target,directory)
        address,size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_n23_cache_bench')
        assert size==10272
        image=f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image,front.settings_bytes(1))
        f.load(target/'dok_nds_gameplay.nds');f.start_level(args.level)
        values=struct.unpack('<2568I',render.ram(f,address,size))
        assert values[0:2]==(0x23424b44,1) and 0<values[2]<=512
        rows=[dict(zip(('id','ticks','used','evictions','cache_fnv1a'),values[8+i*5:13+i*5]))for i in range(values[2])]
        try:logical_address,logical_size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_n23_cache_logical')
        except Exception:logical_address=None
        if logical_address:
            assert logical_size==2048
            logical=struct.unpack('<512I',render.ram(f,logical_address,logical_size))
            for row,digest in zip(rows,logical):row['logical_fnv1a']=digest
        report.update(passed=True,rom=pin(target/'dok_nds_gameplay.nds'),elf=pin(target/'dok_nds_gameplay.elf'),
            runner=pin(Path(__file__)),capacity=values[3],packed_bytes=values[4],image_count=values[5],timer_hz=values[6],group_size=values[7],rows=rows,
            total_ticks=sum(row['ticks']for row in rows),max_ticks=max(row['ticks']for row in rows),
            max_evictions_per_request=max(row['evictions']for row in rows))
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({key:value for key,value in report.items()if key not in ('rows',)}))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
