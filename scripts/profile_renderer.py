"""Read the instrumented renderer's Timer0/1 stage timestamps in mGBA."""
import argparse
import ctypes as ct
import json
import os
from pathlib import Path
import struct
from gba_rom import elf_info
from verify_gba import Frontend, sha
from verify_renderer_gba import start_gameplay, freeze_visual

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--level', type=int, choices=(1,2,3), default=1)
p.add_argument('--camera', type=int, nargs=4, metavar=('X','Y','Z','HEADING'))
p.add_argument('--primitives',action='store_true',help='Enable optional per-primitive attribution (adds profiling overhead)')
p.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
a = p.parse_args()
if a.level!=1 and not a.camera:
    p.error('--level requires --camera')
_, symbols, _ = elf_info(a.build_dir/'dok-diagnostic.elf')
f = Frontend(a.core.resolve(), a.build_dir)
f.load(a.build_dir/'dok-diagnostic.gba')
for _ in range(6000):
    f.run(1)
    basic=f.diag(symbols['g_diag'])
    if basic['magic']==0x444f4b31:
        assert not basic['fault'], basic
    d=struct.unpack('<32I',f.read(symbols['g_render_diag'],128))
    freeze_visual(f,symbols,d[0])
    playing=start_gameplay(f,symbols) if d[0]==0x524e4435 and d[4] else False
    if playing and d[4]>=4 and d[30]==2:
        break
else:
    raise AssertionError('Renderer timeout')
if a.primitives:
    ct.memmove(f.pointer(symbols['g_render_work'],4),struct.pack('<I',1),4)
    before=d[4]
    for _ in range(6000):
        f.run(1)
        d=struct.unpack('<32I',f.read(symbols['g_render_diag'],128))
        if d[4]>=before+3 and d[30]==2:break
    else:raise AssertionError('Primitive profiler timeout')
if a.camera:
    ct.memmove(f.pointer(symbols['g_camera_probe']+8,20),struct.pack('<I4i',a.level-1,*a.camera),20)
    ct.memmove(f.pointer(symbols['g_camera_probe'],4),struct.pack('<I',1),4)
    before=d[4]
    for _ in range(6000):
        f.run(1)
        assert not f.diag(symbols['g_diag'])['fault']
        d=struct.unpack('<32I',f.read(symbols['g_render_diag'],128))
        if struct.unpack('<I',f.read(symbols['g_camera_probe']+4,4))[0]==1 and d[4]>=before+3 and d[30]==2:
            break
    else:
        raise AssertionError('Camera timeout')
v=struct.unpack('<7I',f.read(symbols['g_render_stages'],28))
result=dict(rom=sha(a.build_dir/'dok-diagnostic.gba'),core_sha256=sha(a.core),
            total=d[5],timestamps=v,
            input_time_poll=v[0],
            stages=dict(zip(('clear_resources','geometry','sort','raster','hud','copy'),
                            [b-a for a,b in zip(v,v[1:])])),
            note='Full timed frame excludes presentation wait; stages include timestamp overhead; one steady observation')
a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
if a.primitives:
    work=struct.unpack('<5I',f.read(symbols['g_render_work'],20))
    result['primitives']=dict(wall_cycles=work[1]*64,surface_cycles=work[2]*64,walls=work[3],surfaces=work[4])
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
f.core.retro_unload_game()
f.core.retro_deinit()
print(json.dumps(result))
