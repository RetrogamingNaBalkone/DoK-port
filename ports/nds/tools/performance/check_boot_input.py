"""Loading-only input bridge check: title soak, X/Y, calibrated touch; no levels."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();directory=target/'boot-input';directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    f.options['melonds_homebrew_sdcard']='disabled'
    elf=target/'dok_nds_gameplay.elf'
    key_at,_=front.transport.elf_symbol(elf,'keys')
    touch_at,_=front.transport.elf_symbol(elf,'latchedTouchPosition')
    boot,_=front.transport.elf_symbol(target/'arm7/dok_audio_arm7.elf','g_audio_arm7_boot')
    report=dict(passed=False,scope='Boot/title only; no level selected')
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1)
        before=struct.unpack('<32I',front.render.ram(f,boot,128))
        f.tick(240)
        after=struct.unpack('<32I',front.render.ram(f,boot,128))
        assert f.probe[3]==1 and after[2]>before[2] and after[25]>=before[25]+235,(before,after)
        f.joypad=(1<<front.X)|(1<<front.Y);f.touch=(120,80);f.tick(8)
        keys=struct.unpack('<H',front.render.ram(f,key_at,2))[0]
        touch=struct.unpack('<6H',front.render.ram(f,touch_at,12))
        assert keys&0x1c00==0x1c00,hex(keys)
        assert abs(touch[2]-120)<=2 and abs(touch[3]-80)<=2,touch
        f.joypad=0;f.touch=None;f.tick(8)
        released=struct.unpack('<H',front.render.ram(f,key_at,2))[0]
        assert not released&0x1c00,hex(released)
        report.update(passed=True,title_soak_frames=240,input_messages=after[25]-before[25],keys=hex(keys),touch=list(touch),released=hex(released))
    finally:
        f.close();front.transport.flush_native_stdio()
        report['source_sha256']=front.transport.sha(Path(__file__))
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))

if __name__=='__main__':main()
