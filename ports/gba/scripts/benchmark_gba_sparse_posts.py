"""G20C paired native-clock sparse-post trial; resumes completed reports."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from research_gba_fps import ROOT,live_measure
from verify_gba import sha

CONTROL='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
CASES=['1:520:225','1:3640:225','2:1592:135','2:2871:225','3:1544:315']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,default=Path('research/experiments/g20c-sparse-posts'))
    p.add_argument('--mode',choices=('heavy','spatial','live'),required=True)
    a=p.parse_args();os.chdir(ROOT);os.environ['DOK_PROBE_FAST_VIDEO']='1'
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    assert sha(a.out/'hw_v128_entity_rect/dok-diagnostic.gba')==CONTROL
    variants=['hw_v128_entity_rect','hw_v128_sparse_posts']
    if a.mode=='heavy':variants.insert(1,'hw_v128_sparse_control')
    for variant in variants:
        build=a.out/variant
        if a.mode in ('heavy','spatial'):
            for language in (0,1):
                report=a.out/f'{a.mode}-{variant}-{language}.json'
                if report.exists():
                    old=json.loads(report.read_text())
                    assert old['rom_sha256']==sha(build/'dok-diagnostic.gba')
                    assert old['script_sha256']==sha('ports/gba/scripts/benchmark_gba_spatial_sweep.py')
                    continue
                cmd=[sys.executable,'ports/gba/scripts/benchmark_gba_spatial_sweep.py','--build-dir',str(build),
                     '--report',str(report),'--language',str(language),'--verify-output']
                if a.mode=='heavy':cmd+=['--cases',*CASES]
                subprocess.run(cmd,check=True)
        else:
            for label,buttons in (('turn120',(8,7)),('walking120',(4,8,7))):
                report=a.out/f'{label}-{variant}.json'
                if report.exists():
                    old=json.loads(report.read_text())
                    assert old['rom_sha256']==sha(build/'dok-diagnostic.gba')
                    continue
                result=live_measure(build,core,follow_camera=True,samples=120,buttons=buttons)
                result.update(core_sha256=sha(core),script_sha256=sha(__file__),
                              scope='Native-clock PCM22050/AI/movement/fire;120 presentations per language and level; no whole-level playthrough.')
                for row in result['rows']:
                    assert row['audio']['underruns']==row['time_conservation_error_ticks']==0
                    assert row['hardware_bg_new_overflows']==0
                    assert all(s['flow_state']==2 and s['ui_state']==0 for s in row['samples'])
                report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
                print(label,variant,[(r['level'],r['language'],round(r['presented_fps'],3)) for r in result['rows']],flush=True)


if __name__=='__main__':main()
