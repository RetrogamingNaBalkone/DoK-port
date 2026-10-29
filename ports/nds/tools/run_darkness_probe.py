"""Read-only DS GPU artistic-depth test: near visibility, darkening, far black."""
from pathlib import Path
import json,struct,traceback,hashlib
import run_diag_emulator as transport
import run_renderer_emulator as render
ROOT=Path(__file__).resolve().parents[3]
TARGET=ROOT/'build/nds/n13p/quality'
def main():
    out=TARGET/'emulator';out.mkdir(exist_ok=True);f=None
    report=dict(task='N13P',passed=False,scope='Actual DS-mode emulator raster; no physical hardware FPS claim')
    try:
        core=Path.home()/'AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll'
        assert transport.sha(core)==render.CORE_SHA
        rom=TARGET/'dok_nds_darkness.nds';elf=TARGET/'dok_nds_darkness.elf'
        report.update(rom_sha256=transport.sha(rom),elf_sha256=transport.sha(elf),core_sha256=transport.sha(core))
        address,_=transport.elf_symbol(elf,'g_nds_darkness_probe');f=transport.Frontend(core,out,'happy');f.load(rom)
        for _ in range(2000):
            if f.frame_time and f.frame_time.callback:f.frame_time.callback(round(1000000/f.av.timing.fps))
            f.core.retro_run();f.frames+=1
            data=struct.unpack('<16I',render.ram(f,address,64))
            if data[0]==0x50464b44:
                assert data[1]==1,'GPU darkness probe failed'
                break
        else:raise AssertionError('GPU darkness probe timeout')
        w,h,rgb=f.rgb();assert (w,h)==(256,384)
        samples=[]
        for i,depth in enumerate(data[2:10]):
            colors=[tuple(rgb[(y*256+x)*3:(y*256+x)*3+3]) for y in range(72,81) for x in range(i*32+13,i*32+20)]
            assert len(set(colors))==1 and len(set(colors[0]))==1
            samples.append(dict(depth=depth,rgb=list(colors[0])))
        levels=[s['rgb'][0] for s in samples]
        assert all(n>=245 for n in levels[:3]) and levels[-2:]==[0,0],levels
        assert all(a>=b for a,b in zip(levels,levels[1:])) and 0<levels[5]<levels[4]<levels[3]<levels[2],levels
        report.update(samples=samples,near_readable=True,monotonic_darkness=True,far_objects_fully_hidden=True,
            images=f.capture('darkness-depth'),passed=True)
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:f.close()
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report,indent=2));return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
