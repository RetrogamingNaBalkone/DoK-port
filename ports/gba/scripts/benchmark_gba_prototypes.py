"""Repeat G20A frozen-view and live PCM comparisons of isolated prototypes."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import json
import os
from pathlib import Path
import statistics

from prototype_gba_bsp import ROOT, VARIANTS
from research_gba_fps import measure, live_measure
from verify_gba import sha


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-root',type=Path,default=Path('research/experiments/g20a-prototypes'))
    p.add_argument('--report',type=Path,default=Path('analysis/gba/g20a-prototypes.json'))
    p.add_argument('--variants',nargs='+',choices=VARIANTS,
                   default=['baseline','no_scales','no_scales_runtime','planes','light','bsp_setup','setup',
                            'textured','exact_planes','world_planes','low_vertical','procedural'])
    p.add_argument('--frozen-only',action='store_true')
    a=p.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    previous=json.loads(Path('analysis/gba/g20a-fps-audit.json').read_text(encoding='utf-8'))
    cameras=[{k:r[k] for k in ('name','level','language','camera')}
             for r in previous['results']['baseline']['rows']]
    result=dict(task='G20A',status='prototype-comparison',core_sha256=sha(core),
                script_sha256=sha(__file__),targets_fps=[30,24.1,15],cpu_hz=16777216,
                selection='Nine RU start cameras ranked by the older G20 sweep; not a new extrema search.',
                frozen={},live={},limitations=[
                    'mGBA replacement BIOS; physical GBA not tested.',
                    'Frozen samples include full frame CPU and exclude presentation wait.',
                    'Live FPS includes VBlank; fixtures turn/fire with PCM22050, not a whole-level playthrough.',
                    'Dynamic states differ with elapsed emulated time; only frozen frames form a pixel oracle.',
                    'Procedural and low-resolution experiments are quality comparisons, not defaults.'])
    for variant in a.variants:
        build=a.build_root/variant
        result['frozen'][variant]=measure(build,core,cameras,a.build_root/'baseline')
        result['frozen'][variant]['defines']=list(VARIANTS[variant])
        if not a.frozen_only:
            result['live'][variant]=live_measure(build,core)
        a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        summary=[(r['name'],r['median'],r['changed_scene_pixels']) for r in result['frozen'][variant]['rows']]
        print(variant+' frozen '+str(summary),flush=True)
        if not a.frozen_only:
            print(variant+' live '+str([(r['level'],r['language'],round(r['presented_fps'],3))
                                       for r in result['live'][variant]['rows']]),flush=True)
    baseline=result['frozen'].get('baseline')
    if baseline:
        for variant,record in result['frozen'].items():
            ratios=[b['median']/r['median'] for b,r in zip(baseline['rows'],record['rows'])]
            record['cpu_speedup_median']=statistics.median(ratios)
            record['cpu_speedup_range']=[min(ratios),max(ratios)]
    result['zero_difference_frozen_candidates']=[v for v,r in result['frozen'].items()
                                                if all(row['changed_scene_pixels']==0 for row in r['rows'])]
    result['targets_met']=False
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
