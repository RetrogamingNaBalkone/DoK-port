"""Measure actual resident RLE streams on ARM9; no gameplay FPS claim."""
import argparse,json,struct,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
from run_routes import RouteObserver,ROOT,front,transport,render,pin

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--level',type=int,choices=(1,2,3),default=1)
    args=parser.parse_args();target=args.target.resolve()
    assert target.is_relative_to(ROOT/'build/nds/n23')
    directory=target/f'codec-benchmark-level{args.level}';assert not directory.exists();directory.mkdir()
    report=dict(task='N23',passed=False,scope='Actual resident RLE streams, isolated unpack timing; no gameplay FPS claim');f=None
    try:
        f=RouteObserver(front.CORE,target,directory)
        address,size=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_n23_codec_bench');assert size==10272
        front.write_fixture(f.save/'melonDS DS/dldi_sd_card.bin',front.settings_bytes(1))
        f.load(target/'dok_nds_gameplay.nds');f.start_level(args.level)
        values=struct.unpack('<2568I',render.ram(f,address,size))
        assert values[:2]==(0x23444b44,1) and 0<values[2]<=512
        rows=[dict(zip(('id','ticks','packed_bytes','decoded_bytes','pixel_fnv1a'),values[8+i*5:13+i*5]))for i in range(values[2])]
        report.update(passed=True,rom=pin(target/'dok_nds_gameplay.nds'),elf=pin(target/'dok_nds_gameplay.elf'),runner=pin(Path(__file__)),
            packed_bytes=values[3],frame_count=values[4],timer_hz=values[5],rows=rows,total_ticks=sum(r['ticks']for r in rows),max_ticks=max(r['ticks']for r in rows))
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items()if k!='rows'}));return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
