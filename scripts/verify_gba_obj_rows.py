"""Use real PPU row colors to verify the hardware foreground's vertical mapping."""
import argparse
import json
import os
from pathlib import Path
import struct
from research_gba_fps import GameProbe,ROOT,freeze
from verify_gba import VIDEO,sha,scene_source_y

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True);args=parser.parse_args()
    os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=GameProbe(core,args.build_dir)
    try:
        p.video_callback=VIDEO(p.frontend.video);p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws']>=1);p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.until(lambda d:d['draws']>=4 and d['phase']==2);freeze(p,True)
        p.camera(1,20880,-300,3120,195);p.present();p.frontend.run(2)
        assert 'g_hw_obj_rows_probe' in p.symbols,'Build a dedicated HW_OBJ_ROWS_PROBE variant'
        count=p.words('g_hw_obj_diag',6)[5];assert count in (36,48,64,72);height=count//4*8
        address=p.symbols['g_hw_obj_rows_probe'];request=p.words('g_hw_obj_rows_probe',2)[1]+1
        p.write(address+8,struct.pack('<I',1));p.write(address,struct.pack('<I',request))
        p.until(lambda d:p.words('g_hw_obj_rows_probe',2)[1]==request);p.frontend.run(2)
        assert p.words('g_hw_obj_rows_probe',4)[3]==height
        raw=p.frontend.deferred_video;fmt=p.frontend.pixel_format;actual=[]
        for y in range(160):
            offset=y*240+100
            if fmt==1:
                v=struct.unpack_from('<I',raw,offset*4)[0];r=(v>>19)&31;g=(v>>11)&31;b=(v>>3)&31
            else:
                v=struct.unpack_from('<H',raw,offset*2)[0]
                r=(v>>(11 if fmt==2 else 10))&31;g=(v>>(6 if fmt==2 else 5))&31;b=v&31
            actual.append(r|(g<<5)|(b<<10))
        backdrop=struct.unpack('<H',p.frontend.read(0x05000000,2))[0]
        expected=[scene_source_y(y,count)+1 if y<144 else backdrop for y in range(160)]
        result=dict(status='passed' if actual==expected else 'failed',rom_sha256=sha(p.rom),core_sha256=sha(core),
                    script_sha256=sha(__file__),source_rows=height,physical_rows=144,obj_count=count,
                    pixel_format=fmt,actual_row_palette_values=actual,expected_row_palette_values=expected,
                    scope='Injected OBJ row colors in real PPU; BG0/BG2 disabled only in this fixture. No renderer oracle implied.')
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(result['status'],height,'source rows')
        assert actual==expected,[(y,a,b) for y,(a,b) in enumerate(zip(actual,expected)) if a!=b][:10]
    finally:p.close()

if __name__=='__main__':main()
