"""Preserve reviewed original PLAY/list evidence with exact producer scripts."""
from pathlib import Path
import hashlib,json,subprocess
from audit_effects import ROOT,OUT,originals
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
    samples=originals();old=OUT/'InspectRendering-original.java'
    expected=json.loads((ROOT/'research/evidence/rendering/ru/analysis-manifest.json').read_text('utf-8-sig'))['script_sha256']
    if not old.exists() or sha(old)!=expected:
        raw=subprocess.check_output(['git','show','dca5c551fe892abae57b1131c7f832c464e0aed9:scripts/InspectRendering.java'],cwd=ROOT)
        raw=raw.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n');assert hashlib.sha256(raw).hexdigest()==expected;old.write_bytes(raw)
    report=dict(passed=False,languages={},scope='Reviewed normal PLAY forward build/draw and its image-only water getter; not an assertion about every unreachable EXE branch')
    for language in ('ru','en'):
        blocks=[];inputs=[]
        for folder,script,role in [('flow',ROOT/'tools/ghidra/InspectFlow.java','world_draw_render_list'),('rendering',old,'render_draw_sorted_list')]:
            directory=ROOT/'research/evidence'/folder/language;m=json.loads((directory/'analysis-manifest.json').read_text('utf-8-sig'))
            assert m['sha256_before']==m['sha256_after']==samples[language] and sha(script)==m['script_sha256']
            active=False
            with (directory/'evidence.asm').open(encoding='utf8') as stream:
                for line in stream:
                    if line.startswith('FUNCTION '):active=role in line
                    if active:blocks.append(line.rstrip('\n'))
            inputs.append(dict(path=(directory/'evidence.asm').relative_to(ROOT).as_posix(),sha256=sha(directory/'evidence.asm'),
                manifest=m,producer_script=script.relative_to(ROOT).as_posix()))
        text='\n'.join(blocks)+'\n';needle='CALLF 0x1100:0521' if language=='ru' else 'CALLF 0x1060:14af'
        assert text.count(needle)==1
        target=OUT/f'render-list-{language}.asm';target.write_text(text,'utf8')
        report['languages'][language]=dict(inputs=inputs,extract=target.relative_to(ROOT).as_posix(),sha256=sha(target),sorted_list_calls=1)
    report['passed']=True;report['originals_after']=originals()
    (OUT/'render-list-evidence.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print('N13 original PLAY/list effect registry evidence: matched samples and producer SHA PASS')
if __name__=='__main__':main()
