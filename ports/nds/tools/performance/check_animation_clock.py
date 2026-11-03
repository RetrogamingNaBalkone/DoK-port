"""Production door wrapper + rational water/FPS clock; actual core regression."""
import json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools/map'))
from compiler_cache import compiler_environment
def main():
    out=ROOT/'build/nds/n23/animation-clock';out.mkdir(parents=True,exist_ok=True)
    sources=['ports/nds/platform/gameplay_probe/gameplay.c','ports/nds/tools/performance/test_animation_clock.c']
    sources+=['ports/nds/core/'+n+'.c' for n in ('contracts','events','input','controls','motion','camera','time','collision','interactions','interactions_move','assets','memory','sha256','world','balance','items','combat','ai')]
    sources+=['ports/nds/render/'+n+'.c' for n in ('scene','geometry','materials','door_render','sprites','sprite_assets','weapon_overlay')]
    cmd=[Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe','cc','-std=c11','-Wall','-Wextra','-Werror','-O2',
        '-DGAMEPLAY_FRONTEND=1','-DGAMEPLAY_UI=0','-DGAMEPLAY_AUDIO=0','-Iports/nds/include',
        '-Iports/nds/platform/gameplay_probe','-Ibuild/nds/n07/generated',*sources,'-o',out/'test.exe']
    r=subprocess.run(list(map(str,cmd)),cwd=ROOT,env=compiler_environment(ROOT),capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    r=subprocess.run([out/'test.exe'],cwd=ROOT,capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    report=json.loads(r.stdout);report['command']=list(map(str,cmd))
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(r.stdout)
if __name__=='__main__':main()
