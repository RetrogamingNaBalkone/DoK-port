"""N14 actual ARM9 tests, read-only RAM observer. No emulated memory writes."""
import ctypes as C,json,struct,traceback
from pathlib import Path
import run_diag_emulator as transport
import make_assets_fixture as fat
from check_items import ROOT,artifact
CORE=Path.home()/'AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
def main():
    out=ROOT/'build/nds/n14/emulator';out.mkdir(parents=True,exist_ok=True)
    elf=ROOT/'build/nds/n14/arm9/dok_nds_items.elf';rom=elf.with_suffix('.nds')
    assert transport.sha(CORE)=='c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef'
    f=None;report=dict(passed=False,rom=artifact(rom),elf=artifact(elf),core=artifact(CORE) if CORE.is_relative_to(ROOT) else dict(path=str(CORE),sha256=transport.sha(CORE)))
    try:
        f=transport.Frontend(CORE,out,'happy');image=f.save/'melonDS DS/dldi_sd_card.bin'
        image.with_name(image.name+'.idx').unlink(missing_ok=True)
        fat.write_image(image,[('assets.ndp',b'ASSETS  NDP',(ROOT/'build/nds/assets/dok-assets.ndspak').read_bytes())])
        before=transport.sha(image);address,size=transport.elf_symbol(elf,'g_nds_items_probe');assert size==64
        f.load(rom);transport.flush_native_stdio();before=transport.sha(image);observations=[]
        for _ in range(3600):
            if f.frame_time and f.frame_time.callback:f.frame_time.callback(round(1000000/f.av.timing.fps))
            f.core.retro_run();f.frames+=1
            p=f.core.retro_get_memory_data(2);row=struct.unpack('<16I',C.string_at(p+address-0x2000000,64))
            if row[0]==0x14494b44 and row[2]:
                observations.append(row)
                if len(observations)>=3:break
        assert len(observations)>=3,observations
        row=observations[-1];assert row[2]==1 and row[4]==row[5]==row[9]==0,row
        host=json.loads((ROOT/'research/evidence/nds/n14/host-check.json').read_text('utf8'))['host']
        assert (row[3],row[6],row[7])==(host['checks'],host['placements'],host['notifications'])
        report.update(passed=True,checks=row[3],placements=row[6],notifications=row[7],workspace_bytes=row[8],observations=observations,frames=f.frames,core_name=f.info.library_name.decode(),core_version=f.info.library_version.decode(),options={k:v.decode() for k,v in f.variables.items()})
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:
            report['images']=f.capture('final');transport.flush_native_stdio();report['fat_sha256_before_unload']=transport.sha(image)
            f.close();report['fat_sha256_before']=before;report['fat_sha256_after']=transport.sha(image)
            report['passed'] &= before==transport.sha(image)
        (ROOT/'research/evidence/nds/n14/emulator.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
