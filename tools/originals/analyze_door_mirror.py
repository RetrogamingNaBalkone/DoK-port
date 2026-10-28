"""Read-only Ghidra export; hash originals before and after both projects."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[2]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,os,subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    os.chdir(ROOT)
    home=Path(os.environ['LOCALAPPDATA'])/'Programs/Ghidra'
    ghidra=home/'ghidra_12.1.2_PUBLIC'
    java=home/'java/jdk-21.0.12.1+1/bin/java.exe'
    env=dict(os.environ,LOCALAPPDATA=str(ROOT/'research/ghidra/local'))
    for language in ('ru','en'):
        inv=json.loads(Path(f'analysis/originals/inventory-{language}.json').read_text(encoding='utf-8-sig'))
        sample=Path(inv['file']);before=sha(sample);assert before==inv['sha256']
        out=ROOT/'build/gba/g20b-playtest2/original'/language;out.mkdir(parents=True,exist_ok=True)
        args=[java,'-Djava.system.class.loader=ghidra.GhidraClassLoader',
              f'-Duser.home={ROOT}/research/ghidra/home',f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings',
              '-Djava.awt.headless=true','-Xmx2G','-cp',ghidra/'Ghidra/Framework/Utility/lib/Utility.jar',
              'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',ROOT/f'research/ghidra/projects/{language}','Kremlin',
              '-process',sample.name,'-readOnly','-noanalysis','-scriptPath',ROOT/'ports/gba/scripts','-postScript',
              'InspectDoorMirror.java',language,out]
        with (out/'ghidra.log').open('w') as log:subprocess.run(list(map(str,args)),env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        assert 'DOOR_MIRROR_COMPLETE' in (out/'ghidra.log').read_text()
        after=sha(sample);assert before==after
        (out/'manifest.json').write_text(json.dumps(dict(sample=sample.as_posix(),sha256_before=before,sha256_after=after,
            project=f'research/ghidra/projects/{language}/Kremlin.gpr',address_system='Ghidra NE loader',read_only=True,
            sources={p:sha(p) for p in ('tools/ghidra/InspectDoorMirror.java',__file__)},
            outputs={p:sha(out/p) for p in ('center.bin','evidence.asm','ghidra.log')}),indent=2)+'\n')
        print(language,'Ghidra door mirror evidence passed',flush=True)
if __name__=='__main__':main()
