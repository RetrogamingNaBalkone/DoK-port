"""Explicit two-tool companion for immutable first C16 RU build provenance."""
import json
from pathlib import Path
from archive_ui_performance import ROOT,E,artifact,read
ALLOWED={'ports/nds/tools/archive_ui_performance.py','ports/nds/tools/summarize_ui_performance.py'}

def main():
    targets=[]
    for label in ('candidate16-ru','candidate16-en','candidate16-audit-ru','candidate16-frontend-ru','candidate16-frontend-en','candidate16-fixture-ru','candidate16-fixture-en'):
        target=ROOT/'build/nds/n21p'/label;manifest=read(target/'build-manifest.json');rows=[]
        for source in manifest['sources']:
            current=artifact(ROOT/source['path'])
            if current==source:continue
            assert source['path'] in ALLOWED,('Unexpected frozen source delta',label,source['path'])
            old=artifact(E/'sources'/label/source['path'])
            assert (old['sha256'],old['bytes'])==(source['sha256'],source['bytes'])
            rows.append(dict(build_source=source,frozen_snapshot=old,current_source=current))
        assert {row['build_source']['path'] for row in rows}==ALLOWED
        for source in manifest['artifacts']:assert artifact(ROOT/source['path'])==source
        targets.append(dict(target=target.relative_to(ROOT).as_posix(),manifest=artifact(target/'build-manifest.json'),artifacts_unchanged=manifest['artifacts'],tool_supersessions=rows))
    result=dict(task='N21P immutable seven C16 targets tool companion',passed=True,targets=targets,scope='Exactly two Python report/archive tools evolved after frozen C16 builds. Each original snapshot and current final version is pinned; no product or arbitrary Python override is permitted.',tool=artifact(Path(__file__)))
    (E/'final-tool-companion.json').write_text(json.dumps(result,indent=2)+'\n','utf8');print(json.dumps(dict(passed=True,targets=len(targets),tools_per_target=2)))
if __name__=='__main__':main()
