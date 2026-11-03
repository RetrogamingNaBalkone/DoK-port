"""Read-only LEVEL2 water/FPS observations and LCD capture on the exact ELF."""
import argparse,json,os,struct,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'flow'))
import run_frontend_emulator as front
from inspect_ai_layout import parse,member

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();elf=target/'dok_nds_gameplay.elf'
    tool=Path.home()/'.local/msys64/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-readelf.exe'
    env=os.environ.copy();env['PATH']=str(Path.home()/'.local/msys64/opt/wonderful/bin')+';'+env['PATH']
    nodes=parse(subprocess.check_output([tool,'--debug-dump=info',elf],env=env,text=True,encoding='utf8'))
    world=next(n for n in nodes.values()if n['tag']=='DW_TAG_structure_type' and n['attrs'].get('name')=='SpriteWorld')
    offset,effects=member(nodes,world,'effects')
    offsets={name:member(nodes,effects,name)[0] for name in ('water_ticks','water_phase','water_fraction')}
    storage,_=front.transport.elf_symbol(elf,'storage');fps,_=front.transport.elf_symbol(elf,'fps_counter')
    directory=target/'animation-observer';directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    f.options['melonds_homebrew_sdcard']='disabled'
    rows=[];report=dict(passed=False,scope='Read-only stationary LEVEL2, ordinary buttons, no RAM writes')
    def snapshot():
        pointer=struct.unpack('<I',front.render.ram(f,storage,4))[0]+offset
        return {k:struct.unpack('<I',front.render.ram(f,pointer+v,4))[0]for k,v in offsets.items()}
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        f.tap(front.A);f.wait_for(3);f.tap(front.DOWN);f.tap(front.A);f.text_to(7,8);f.wait_for(8)
        for _ in range(240):
            f.tick();row=snapshot()
            assert row['water_phase']==row['water_ticks']*7140//1193182%18,row
            assert row['water_fraction']==row['water_ticks']*7140%1193182,row
            rows.append(row)
        counter=struct.unpack('<5I',front.render.ram(f,fps,20));assert 100<=counter[4]<=605,counter
        report['fps_counter']=counter;report['captures']=f.capture('level2-fps-water')
        # Actual composited LCD, not just a correct counter in ARM9 RAM.
        width,height,pitch,fmt,video=f.last_video;assert width==256 and height==384 and fmt==1
        colors={video[y*pitch+x*4:y*pitch+x*4+4]for y in range(2,12)for x in range(188,252)}
        assert len(colors)==2,('FPS tile missing from LCD',len(colors))
        report['lcd_fps_tile_colors']=len(colors)
        f.tap(front.START);f.wait_for(9);frozen=snapshot();f.tick(45);assert snapshot()==frozen
        f.tap(front.A);f.wait_for(8);f.tick(60)
        report.update(passed=True,water_samples=len(rows),distinct_phases=len({r['water_phase']for r in rows}),pause_frozen=True)
        assert report['distinct_phases']==18
    finally:
        f.close();front.transport.flush_native_stdio()
        report.update(rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'),water_rows=rows)
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items()if k!='water_rows'}))
if __name__=='__main__':main()
