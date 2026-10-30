"""Verify the frozen N18 inputs and archive final N19 sources/evidence."""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/nds/n19'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def artifact(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p))
def main():
    accepted=json.loads((ROOT/'build/nds/n18/final-release-ru/build-manifest.json').read_text('utf8'))
    pins={r['path']:r for r in accepted['sources']}
    supersession=json.loads((E/'supersession.json').read_text('utf8'))
    for r in supersession['changes']:
        old=ROOT/r['archive'];current=ROOT/r['path']
        assert sha(old)==pins[r['path']]['sha256']==r['original_sha256']
        r['current_sha256']=sha(current)
    (E/'supersession.json').write_text(json.dumps(supersession,indent=2)+'\n','utf8')
    sources=[]
    for path in sorted((ROOT/'ports/nds').rglob('*')):
        if not path.is_file() or not(path.suffix in ('.c','.h','.py','.ps1','.inc') or path.name.startswith('Makefile')):continue
        dest=E/'final-sources'/path.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest)
        assert sha(dest)==sha(path);sources.append(artifact(path))
    generated=[artifact(ROOT/'build/nds/n19'/p) for p in ('ui.dat','generated/ui_native.inc','scene.dat')]
    reports=[]
    for label in ('final-release-ru','final-release-en','smoke-ru','smoke-en'):
        target=ROOT/f'build/nds/n19/{label}'
        for folder in ('hud','emulator','audio'):
            for path in sorted((target/folder).rglob('*')):
                if not path.is_file() or path.suffix not in ('.json','.png','.log'):continue
                dest=E/'emulator'/label/folder/path.relative_to(target/folder);dest.parent.mkdir(parents=True,exist_ok=True)
                shutil.copyfile(path,dest);assert sha(path)==sha(dest);reports.append(dict(source=artifact(path),archive=artifact(dest)))
    out=dict(task='N19',passed=True,sources=sources,generated=generated,reports=reports,supersession=artifact(E/'supersession.json'))
    (E/'final-archive.json').write_text(json.dumps(out,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,sources=len(sources),reports=len(reports))))
if __name__=='__main__':main()
