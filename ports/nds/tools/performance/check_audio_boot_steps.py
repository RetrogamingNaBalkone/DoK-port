"""Read-only ARM9 boot checkpoint observer for N23 diagnostics."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();directory=target/'boot-steps';directory.mkdir(exist_ok=True)
    stage,_=front.transport.elf_symbol(target/'dok_nds_gameplay.elf','loading_stage')
    f=front.FrontendObserver(front.CORE,target,directory)
    f.options['melonds_homebrew_sdcard']='disabled'
    report=dict(passed=False,stages=[],images=[])
    try:
        f.load(target/'dok_nds_gameplay.nds')
        for _ in range(1000):
            f.tick()
            value=struct.unpack('<I',front.render.ram(f,stage,4))[0]
            if value not in report['stages']:report['stages'].append(value)
            if value==57 and not report['images']:report['images']=f.capture('audio-step56')
            if f.probe and f.probe[3]==1:break
        assert set(range(51,60))<=set(report['stages']),report['stages']
        assert report['images']
        arm7=target/'arm7/dok_audio_arm7.elf'
        if arm7.exists():
            try:boot,boot_bytes=front.transport.elf_symbol(arm7,'g_audio_arm7_boot')
            except ValueError:boot=None
            if boot is not None:
                assert boot_bytes in (64,96,128) and boot%32==0
                words=struct.unpack('<16I',front.render.ram(f,boot,64))
                assert words[0]==0x37424f41 and words[1]==9 and words[2]>0 and words[3]>0 and words[5]==6,words
                report['arm7_boot']=dict(address=boot,words=list(words))
                try:front.transport.elf_symbol(arm7,'audio_irq_probe');has_irq_probe=True
                except ValueError:has_irq_probe=False
                if has_irq_probe:
                    pc=words[13]-4
                    assert 0x03800000<=pc<0x03810000,(hex(words[13]),hex(words[15]))
                    assert words[15]&31 in (0x1f,0x12,0x13),hex(words[15])
                    report['irq_context']=dict(saved_lr=hex(words[13]),interrupted_pc=hex(pc),r3=hex(words[14]),psr=hex(words[15]))
                if boot_bytes>=96:
                    control=struct.unpack('<8I',front.render.ram(f,boot+64,32))
                    assert control[0]==0x43424d41 and 0x02000000<=control[1]<0x02380000 and control[1]%32==0 and control[2]>0,control
                    report['ram_mailbox_control']=list(control)
                if boot_bytes==128:
                    data=struct.unpack('<8I',front.render.ram(f,boot+96,32))
                    assert data[0]>0 and not data[0]&1 and data[1]>0,data
                    report['ram_input']=list(data)
                if words[9]:
                    f.tick(3)
                    later=struct.unpack('<16I',front.render.ram(f,boot,64))
                    assert later[2]>words[2] and later[11]>words[11],(words,later)
                    report['arm7_progress']=dict(loop_delta=later[2]-words[2],vblank_delta=later[11]-words[11],words=list(later))
        report['passed']=True
    finally:
        f.close();front.transport.flush_native_stdio()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))

if __name__=='__main__':main()
