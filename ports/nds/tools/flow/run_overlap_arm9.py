"""Read-only libretro observer for the separate N22 ARM9 overlap fixture."""
import json
from pathlib import Path
import shutil
import struct
import sys
import traceback
from check_host import ROOT,pin

sys.path.insert(0,str(ROOT/'ports/nds/tools'))
import run_diag_emulator as transport
import run_renderer_emulator as render
import run_frontend_emulator as frontend
from frontend_fat_oracle import volume_files,audit_changes

def main():
    target=ROOT/'build/nds/n22/overlap-arm9'
    evidence=ROOT/'research/evidence/nds/n22/overlap-arm9';evidence.mkdir(parents=True,exist_ok=True)
    directory=target/'emulator';directory.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((target/'build-manifest.json').read_text(encoding='utf-8'))
    for item in manifest['inputs']:
        assert pin(ROOT/item['current']['path'])==item['current']
        assert pin(ROOT/item['snapshot']['path'])==item['snapshot']
    assert pin(ROOT/manifest['arm7']['path'])==manifest['arm7']
    for item in manifest['artifacts']:assert pin(ROOT/item['path'])==item
    assert transport.sha(frontend.CORE)==render.CORE_SHA
    address,size=transport.elf_symbol(target/'dok_nds_overlap.elf','g_nds_overlap_probe');assert size==128
    observer=None
    report=dict(task='N22',passed=False,scope='ARM9 controlled integration fixture, not normal playthrough or physical DS',
                driver=pin(Path(__file__).resolve()),build_manifest=pin(target/'build-manifest.json'),
                core=dict(path=str(frontend.CORE),sha256=transport.sha(frontend.CORE)),
                observation=dict(symbol='g_nds_overlap_probe',address=address,size=size,access='read-only'),
                limitations=['DS mode emulation with built-in BIOS/firmware replacements','No physical hardware performance claim',
                             'No scripted NDS RAM/PC/expected-state writes; controlled world fixture executes within the ARM9 program'])
    try:
        observer=transport.Frontend(frontend.CORE,directory,'happy')
        image=observer.save/'melonDS DS/dldi_sd_card.bin';frontend.write_fixture(image)
        observer.load(target/'dok_nds_overlap.nds');transport.flush_native_stdio()
        before=volume_files(image)
        report['core']['library']=observer.info.library_name.decode();report['core']['version']=observer.info.library_version.decode()
        report['options']=observer.options.copy()
        seen=[]
        for frame in range(12000):
            if observer.frame_time and observer.frame_time.callback:
                observer.frame_time.callback(round(1000000/observer.av.timing.fps))
            observer.core.retro_run();observer.frames+=1;observer.pcm.clear()
            assert not observer.callback_error and not observer.shutdown,observer.callback_error
            raw=render.ram(observer,address,128);values=struct.unpack('<32I',raw)
            if values[0]!=0x224f4b44 or values[23]&1:continue
            again=struct.unpack('<32I',render.ram(observer,address,128))
            if again[23]!=values[23]:continue
            if not seen or seen[-1]['status']!=values[2]:seen.append(dict(frame=observer.frames,status=values[2],probe=list(values)))
            assert values[2]!=3,('Actual ARM9 fixture assertion',values[3],list(values))
            if values[2]==2:
                assert values[4]==160 and values[10]==40,(values[4],values[10])
                assert values[6:9]==(0,0,0),values[6:9]
                assert values[12]==2 and values[13]==2 and values[15]==32,list(values)
                assert values[20]==0,'Fixture must run in DS mode'
                report['actual_probe']=list(values);report['frames']=observer.frames;report['states']=seen
                report['actual']=dict(checks=values[4],total_ai_callbacks=values[10],repeated_retries=values[12],
                     corpse_variants=values[13],collision_state_bytes=values[14],overlap_bitmap_bytes=values[15],
                     faults=list(values[6:9]),DSi=values[20])
                report['screens']=[]
                for capture in observer.capture('passed'):
                    source=Path(capture['path']);destination=evidence/source.name
                    shutil.copyfile(source,destination);assert transport.sha(destination)==capture['sha256']
                    report['screens'].append(pin(destination))
                (evidence/'probe.bin').write_bytes(raw)
                report['probe']=pin(evidence/'probe.bin')
                break
        else:raise AssertionError('ARM9 overlap fixture deadline expired')
        report['passed']=True
    except Exception:
        report['error']=traceback.format_exc()
        if observer and observer.last_video:report['failure_screens']=observer.capture('failed')
    finally:
        if observer:
            observer.close();transport.flush_native_stdio()
            if 'before' in locals():report['disk']=audit_changes(before,volume_files(image),[])
        report['observer_inputs']=[]
        loaded={Path(module.__file__).resolve() for module in tuple(sys.modules.values())
                if getattr(module,'__file__',None) and str(module.__file__).endswith('.py')}
        for source in sorted(loaded):
            if not source.is_relative_to(ROOT):continue
            destination=evidence/'observer-inputs'/source.relative_to(ROOT)
            destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
            current,snapshot=pin(source),pin(destination)
            assert current['sha256']==snapshot['sha256']
            report['observer_inputs'].append(dict(current=current,snapshot=snapshot))
        (evidence/'emulator.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        (directory/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        shutil.copyfile(Path(__file__),evidence/'observer.py')
    print(json.dumps({k:v for k,v in report.items() if k in ('passed','actual','frames','error')}))
    return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
