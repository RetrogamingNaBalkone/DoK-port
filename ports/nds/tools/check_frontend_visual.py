"""N21 render-only localization: actual N04 loading/cache vs raw RES oracle."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n21/visual-host'
EVIDENCE=ROOT/'research/evidence/nds/n21'

def artifact(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def run(args,**kwargs):
    result=subprocess.run([str(arg) for arg in args],cwd=ROOT,text=True,encoding='utf8',
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,**kwargs)
    if result.returncode:raise RuntimeError(result.stdout)
    return result.stdout

def main():
    OUT.mkdir(parents=True,exist_ok=True);EVIDENCE.mkdir(parents=True,exist_ok=True)
    source=json.loads((EVIDENCE/'visual-source.json').read_text('utf8'));assert source['passed']
    package=ROOT/'build/nds/assets/dok-assets.ndspak'
    expected=[ROOT/f'build/nds/n21/visual-popal-{language}.bin' for language in ('ru','en')]
    frozen=[artifact(path) for path in [package,*expected,ROOT/'originals/pkr/RUS/PKR.RES',ROOT/'originals/pkr/ENG/PKE.RES']]
    assert frozen[0]==source['package_before']==source['package_after']
    assert frozen[1:3]==source['oracle_files']
    assert frozen[3:]==source['source_before']==source['source_after']
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    compiler=run([zig,'version']).strip();assert compiler=='0.13.0'
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),
             ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    core=[f'ports/nds/core/{name}.c' for name in ('contracts','assets','sha256','world','balance')]
    render=[f'ports/nds/render/{name}.c' for name in ('scene','geometry','materials','door_render','sprites','sprite_assets','texture_cache','frame_store')]
    sources=[*core,*render,'ports/nds/platform/assets/file_reader.c',
             'ports/nds/ui/frontend_visual.c','ports/nds/tests/test_frontend_visual.c']
    common=[zig,'cc','-std=c11','-pedantic-errors','-Wall','-Wextra','-Werror','-O2',
            '-I','ports/nds/include','-I','build/nds/n07/generated',*sources]
    reports=[];binaries=[]
    for language in (1,2):
        exe=OUT/f'visual-{language}.exe'
        run([*common,f'-DVISUAL_BANK_LANGUAGE={language}','-o',exe],env=env)
        result=json.loads(run([exe,package,*expected]));assert result['passed'];reports.append(result)
        binaries.append(artifact(exe))
        # Swap independent RU/EN expected files. A wrong language must be caught
        # by the complete decoded-pixel oracle, despite the tiny three-pixel delta.
        mutation=subprocess.run([str(exe),str(package),str(expected[1]),str(expected[0])],
            cwd=ROOT,capture_output=True,text=True,encoding='utf8')
        assert mutation.returncode!=0 and 'pixels[i]==expected[language-1u][i]' in mutation.stderr
    linux=OUT/'visual-linux'
    run([*common,'-target','x86_64-linux-musl','-static','-o',linux],env=env)
    assert frozen==[artifact(ROOT/entry['path']) for entry in frozen]
    report=dict(task='N21',passed=True,checks=sum(item['checks'] for item in reports),
        banks=reports,wrong_language_mutations_rejected=2,linux_execution=False,compiler=compiler,
        source_oracle=artifact(EVIDENCE/'visual-source.json'),immutable_inputs=frozen,
        artifacts=[*binaries,artifact(linux)],sources=[artifact(ROOT/path) for path in sources+
            ['ports/nds/include/dok/frontend_visual.h','ports/nds/tools/check_frontend_visual.py',
             'build/nds/n07/generated/render_trig.inc']])
    (EVIDENCE/'host-visual.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,checks=report['checks'],banks=reports)))
    return report

if __name__=='__main__':main()
