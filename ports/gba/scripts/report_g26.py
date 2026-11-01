"""Collect G26 measured transport model and bounded rejection."""
import hashlib,json,statistics
from pathlib import Path
from check_g26 import ROOT,OUT,CONTROL
from verify_gba import sha

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def main():
    rows=[];builds={}
    for name in ('control','ui','low'):
        build=CONTROL if name=='control' else ROOT/f'build/gba/g26/probes/g26_{name}_transport'
        b=read(build/'build-report.json');i=next(s['size'] for s in b['sections'] if s['name']=='.iwram')
        builds[name]=dict(rom_sha256=sha(build/'dok-diagnostic.gba'),rom_bytes=b['rom_bytes'],ewram=b['ewram_static_bytes'],ewram_free=262144-b['ewram_static_bytes'],iwram=i,iwram_free=32768-8192-i,stack_reserve=8192)
    assert builds['control']['rom_sha256']=='53fdf1b1b868effecfd68c8feb87ffb60bcc72190a4d01f38d8684a781860247'
    for language in (0,1):
        c=read(OUT/f'control-warm-{language}.json')['rows']
        for name in ('ui','low'):
            t=read(OUT/f'{name}-warm-{language}.json')['rows']
            for b,r in zip(c,t):
                assert (b['level'],b['cell'],b['heading'])==(r['level'],r['cell'],r['heading'])
                assert b['coverage_sha256']==r['coverage_sha256']
                if name=='ui':assert b['completed_ppu_sha256']==r['completed_ppu_sha256']
                rows.append(dict(probe=name,language=language,level=r['level'],cell=r['cell'],heading=r['heading'],control_cpu=b['median'],probe_cpu=r['median'],delta_cycles=r['median']-b['median'],delta_percent=(r['median']/b['median']-1)*100,control_p95=b['p95'],probe_p95=r['p95'],control_fps=b['presented_fps'],probe_fps=r['presented_fps'],control_stages=b['stage_medians'],probe_stages=r['stage_medians'],static_cache_work=r['static_cache_work']))
    ppu={}
    for name in ('ui','low'):
        p=read(OUT/f'{name}-ppu.json');rs=p['after']['rows'];before={r['name']:r for r in p['before']['rows']}
        assert all(r['coverage_equal'] and r['lease_sha256']==before[r['name']]['lease_sha256'] and r['different_foreground_rgb_pixels']==0 and r['different_overlay_rgb_pixels']==0 for r in rs)
        if name=='ui':assert all(r['different_rgb_pixels']==0 for r in rs)
        ppu[name]=dict(pairs=len(rs),maximum_background_rgb_difference=max(r['different_rgb_pixels'] for r in rs),coverage_leases_foreground_overlay_exact=True)
    summary={n:dict(minimum_percent=min(r['delta_percent'] for r in rows if r['probe']==n),maximum_percent=max(r['delta_percent'] for r in rows if r['probe']==n),median_percent=statistics.median(r['delta_percent'] for r in rows if r['probe']==n)) for n in ('ui','low')}
    sources=['ports/gba/platform/hardware_bg.inc','ports/gba/platform/hardware_obj.inc','ports/gba/platform/hardware_ui_obj.inc','ports/gba/platform/hardware_direct.inc','ports/gba/include/dok/gba_hw_scratch.h','ports/gba/src/renderer_static_cache.inc','ports/gba/src/renderer_surface_raster.inc']
    result=dict(task='G26',status='passed',decision='Do not open a new H10 native backend for the current release; dirty OAM/weapon transfer does not remove fixed affine/OAM transport cost',builds=builds,summary=summary,rows=rows,ppu=ppu,
        model=dict(units='bytes unless cycles explicitly named',mode1=dict(hblank_bytes_per_line=18,hblank_bytes_per_video_frame=2880,affine_frame_bytes=2900,affine_double_buffer_bytes=5800),ui_probe=dict(hblank_bytes_per_line=50,hblank_bytes_per_video_frame=8000),mode2=dict(hblank_bytes_per_line=68,hblank_bytes_per_video_frame=10880,affine_frame_bytes=5796,affine_double_buffer_bytes=11592,mode4_hold_bytes=5796,geometry_payload_bytes=17041),common=dict(foreground_bytes_per_draw=16384,ui_max_bytes=6784,weapon_stage_max_bytes=5120,weapon_obj_atlas_bytes=8192,map_bytes=24576,map_cell_writes_bytes=96,video_frame_cycles=280896,vblank_cycles=83776,hblank_cycles=272),mode1_geometry_payload_bytes=28637),
        timer_units='g_hw_* timer_sample diagnostics already report CPU cycles; no multiplication by 64. Existing primitive render_ticks profile uses different units and is not used here.',
        evidence={str(p.relative_to(ROOT)):sha(p) for p in OUT.glob('*.json') if p.name!='model.json'},source_sha256={p:sha(ROOT/p) for p in sources},
        limitations=['18 warm frozen poses per ROM; no full spatial/live or release regression on diagnostic transport probes','New hardware ceiling changes background sampling; not exact image optimization','No new adaptive/grid/feedback tuning, no candidate promotion, no hardware/original BIOS check','Byte traffic is not CPU cycles; measured stage deltas include DMA stalls, IRQ and layout/code differences','G21/BUG02 and other ports unchanged'])
    target=ROOT/'analysis/gba/g26-transport-model-20261101.json';target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(summary=summary,builds=builds,ppu=ppu),indent=2))
if __name__=='__main__':main()
