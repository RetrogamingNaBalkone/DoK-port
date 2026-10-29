"""Read actual ARM9 shared-suite outputs for the N15+ logic gates."""
import ctypes as C,json,struct,sys,traceback
import run_diag_emulator as transport
import make_assets_fixture as fat
from run_items_emulator import CORE
from check_items import ROOT,artifact
def main(task,name,magic):
    out=ROOT/f'build/nds/{task}/emulator';out.mkdir(parents=True,exist_ok=True)
    elf=ROOT/f'build/nds/{task}/arm9/dok_nds_{name}.elf';rom=elf.with_suffix('.nds');f=None
    report=dict(task=task,passed=False,rom=artifact(rom),elf=artifact(elf),core_sha256=transport.sha(CORE))
    assert report['core_sha256']=='c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef'
    try:
        f=transport.Frontend(CORE,out,'happy');image=f.save/'melonDS DS/dldi_sd_card.bin';image.with_name(image.name+'.idx').unlink(missing_ok=True)
        fat.write_image(image,[('assets.ndp',b'ASSETS  NDP',(ROOT/'build/nds/assets/dok-assets.ndspak').read_bytes())])
        f.load(rom);transport.flush_native_stdio();before=transport.sha(image)
        address,size=transport.elf_symbol(elf,'g_nds_'+name+'_probe');assert size==64;observations=[]
        for _ in range(9000):
            if f.frame_time and f.frame_time.callback:f.frame_time.callback(round(1000000/f.av.timing.fps))
            f.core.retro_run();f.frames+=1
            pointer=f.core.retro_get_memory_data(2);row=struct.unpack('<16I',C.string_at(pointer+address-0x2000000,64))
            if row[0]==magic and row[2]:
                observations.append(row)
                if len(observations)>=3:break
        assert len(observations)==3;row=observations[-1]
        assert row[2]==1 and row[4]==row[5]==row[9]==0,row
        host=json.loads((ROOT/f'research/evidence/nds/{task}/host-check.json').read_text())['tests']['test_'+name]
        assert (row[3],row[6],row[7],row[8])==tuple(host[k] for k in ('checks','scenarios','callbacks','trace_hash')),(row,host)
        report.update(passed=True,observations=observations,frames=f.frames,core_name=f.info.library_name.decode(),core_version=f.info.library_version.decode(),options={k:v.decode() for k,v in f.variables.items()})
        if task=='n16':
            pa,ps=transport.elf_symbol(elf,'g_nds_ai_profile');assert ps==32
            profile=list(struct.unpack('<8I',C.string_at(pointer+pa-0x2000000,32)))
            assert profile[2]==host['callbacks'] and profile[5]==host['stress_callbacks']
            combat=json.loads((ROOT/f'research/evidence/nds/{task}/host-check.json').read_text())['tests']['test_combat']
            assert (profile[6],profile[7])==(combat['checks'],combat['trace_hash']) and not row[14]
            report['cpu_profile']=dict(normal_ticks=profile[0],normal_max_ticks=profile[1],normal_callbacks=profile[2],stress_ticks=profile[3],stress_max_ticks=profile[4],stress_callbacks=profile[5],clock_hz=33513982,scope='emulated ARM9 callback CPU only; diagnostics included; no renderer/audio, no physical FPS')
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:
            report['images']=f.capture('final');transport.flush_native_stdio();report['fat_sha256_before']=before;report['fat_sha256_before_unload']=transport.sha(image)
            f.close();report['fat_sha256_after_unload']=transport.sha(image);report['passed'] &= before==transport.sha(image)
        (ROOT/f'research/evidence/nds/{task}/emulator.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k in ('passed','error','observations','frames')}));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main(sys.argv[1],sys.argv[2],int(sys.argv[3],0)))
