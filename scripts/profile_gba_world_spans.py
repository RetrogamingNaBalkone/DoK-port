"""Attribute horizontal-plane cost in frozen G20A cameras (instrumented)."""
import argparse
import json
import os
from pathlib import Path
import struct

from research_gba_fps import GameProbe, freeze, ROOT
from verify_gba import sha


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    a=parser.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    probe=GameProbe(core,a.build_dir);rows=[]
    try:
        probe.until(lambda d:d['draws']>=4 and d['phase']==2)
        freeze(probe,True)
        probe.write(probe.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        probe.write(probe.symbols['g_world_span_work'],struct.pack('<I',1))
        probe.write(probe.symbols['g_render_work'],struct.pack('<I',1))
        if 'g_ray_work' in probe.symbols:probe.write(probe.symbols['g_ray_work'],struct.pack('<I',1))
        cameras=json.loads((ROOT/'analysis/g20a-fps-audit.json').read_text(encoding='utf-8'))['results']['baseline']['rows']
        for camera in cameras:
            probe.language(camera['language']);probe.camera(camera['level'],*camera['camera'])
            probe.present();d=probe.present()
            world=probe.words('g_world_span_work',9);raster=probe.words('g_render_work',5)
            row=dict(name=camera['name'],camera=camera['camera'],cycles=d['last_cycles'],
                     setup_cycles=world[1]*64,walk_cycles=world[2]*64,sample_cycles=world[3]*64,
                     rows=world[4],spans=world[5],pixels=world[6],covered=world[7],
                     walls_cycles=raster[1]*64,sides_cycles=raster[2]*64,
                     walls=raster[3],sides=raster[4])
            if 'g_ray_work' in probe.symbols:
                ray=probe.words('g_ray_work',4)
                row['ray_pass']=dict(cycles=ray[1]*64,hits=ray[2],map_steps=ray[3])
            rows.append(row);print(row,flush=True)
    finally:probe.close()
    a.report.write_text(json.dumps(dict(rom_sha256=sha(a.build_dir/'dok-diagnostic.gba'),
        core_sha256=sha(core),instrumented=True,scope='Frozen attribution; timestamp overhead included. Not live FPS.',
        rows=rows),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
