"""Archive N21P reproducible inputs/results without rewriting prior evidence."""
import hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
E=ROOT/'research/evidence/nds/n21p'
def read(path):return json.loads(path.read_text('utf8'))
def artifact(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def copy(path,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(path,destination)
    a,b=artifact(path),artifact(destination)
    assert (a['bytes'],a['sha256'])==(b['bytes'],b['sha256'])
    return dict(source=a,archive=b)
def supersession():
    predecessor=read(ROOT/'research/evidence/nds/n21/final-archive.json')
    rows=[]
    reasons={
      'ports/nds/platform/gameplay_probe/main.c':'Bounded CPU phase timing in existing scene tail; map CPU preparation before SwapBuffers and DMA after VBlank; explicit raw-cache mutation observation.',
      'ports/nds/platform/gameplay_probe/effects_runtime.h':'Configurable diagnostic history only; default and SCN512 preserved, frontend384 preserves SDK minimum without reserve use.',
      'ports/nds/platform/frontend/loop.inc':'Invalidate cached lower-map pixels and blank-overlay validity on return after frontend overwrote VRAM; clear stale input latency.',
      'ports/nds/platform/frontend/runtime.c':'Flush static payload at mutation, texture inspector data after exact cache mutation/identity change; expensive VRAM comparison only in audit with honest counters.',
      'ports/nds/platform/frontend/runtime.h':'Sixteen bytes of inspector mutation metadata in existing frontend I/O gap; no new allocation.',
      'ports/nds/platform/ui/runtime.c':'Idle buffer flush suppression; VRAM readback belongs to explicit audit, real product DMA/pixels unchanged.',
      'ports/nds/platform/ui/runtime.h':'Hidden-overlay blank validity uses existing final2B padding; sizeof unchanged on ARM.',
      'ports/nds/platform/map/runtime.c':'Dirty/12Hz dynamic map, immediate gestures, independent live/last-shown ABI, exact per-frame class1 writes and PRE dirty debt; defer observer snapshot cache clean and diagnostic PRE visited count until after map/GPU DMA, before even descriptor publication, with composite CPU accounting.',
      'ports/nds/platform/map/runtime.h':'Scene-tail metadata for dirty lifetimes and bounded phase timings; no heap or reserve allocation.',
      'ports/nds/ui/map_discovery.c':'Exact transient sprite lookup, factor cache and source signed32 IDIV; per-call object-index field54 metadata; native order/bounds/320-column semantics retained.',
      'ports/nds/render/geometry.c':'Shared private in-place kernel avoids unused-tail clear and candidate copy for the scene caller; public full-zero/status/candidate/error/out contract remains byte-exact. Redundant append clear removed.',
      'ports/nds/render/scene.c':'Exact64-bit frustum predicates, range-proven merge intermediates, one transient cell via private builder with old cache1 path preserved; optional fine timers are disabled in normal builds.',
      'ports/nds/render/door_render.c':'Eliminate unchanged self-copies while preserving phase validation, exact crop/compaction order, counts, errors and merge refusal; optional direct door CPU timing.',
      'ports/nds/platform/render_probe/render_backend.c':'Typed equality of five padding-free int32 vertex fields replaces20B libc comparisons; bounded strip metadata packs four2-bit order indices and one reuse bit into uint16, preserving capacity2048 and exact admission/emission order; SDK ITCM placement only for hot code.',
      'ports/nds/platform/render_probe/render_backend.h':'Optional explicit prepare-upload/commit-prepared lease; original public commit keeps its full cache-cleaning contract and error order.',
      'ports/nds/ui/map_raster.c':'Compute contiguous inclusive visible cell range once per call; skip wholly offscreen rows, preserve full linear source index and analytical seen/PRE ordering, keep fixed hero arrow separate; SDK ITCM_CODE places raster/helpers in existing ITCM.',
      'ports/nds/include/dok/render_scene.h':'Optional sixteen-byte fine-profile counters in existing scene slab, removed from normal layout when N21P_GEOMETRY_PROFILE=0; no heap/BSS/reserve.',
      'ports/nds/include/dok/map_discovery.h':'Transient workspace47556 including per-call field54 table within shared scratch131072, persistent0.',
      'ports/nds/tools/map/audit_discovery.py':'Optional output report argument for fresh N21P native execution; frozen N20 report is retained.',
    }
    for pair in predecessor['sources']:
        old=pair['source'];saved=pair['archive'];path=ROOT/old['path']
        assert artifact(ROOT/saved['path'])==saved
        assert old['sha256']==saved['sha256'] and old['bytes']==saved['bytes']
        current=artifact(path)
        if current['sha256']!=old['sha256']:
            assert old['path'] in reasons,('Unexplained modification of N21 input',old['path'])
            rows.append(dict(old=old,accepted_archive=saved,current=current,reason=reasons[old['path']]))
    result=dict(task='N21P',passed=True,predecessor=artifact(ROOT/'analysis/nds-n21-frontend.json'),
        unchanged_sources=len(predecessor['sources'])-len(rows),changes=rows,
        policy='Only explicit changed files are superseded. Every predecessor archive pin is checked; unchanged inputs still match current source.')
    (E/'supersession.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    return result
def main():
    acceptance=read(E/'root-acceptance.json');assert acceptance['passed'] is True
    allowed={'ports/nds/tools/archive_ui_performance.py','ports/nds/tools/summarize_ui_performance.py'}
    labels={'candidate16-ru','candidate16-en','candidate16-audit-ru','candidate16-frontend-ru','candidate16-frontend-en','candidate16-fixture-ru','candidate16-fixture-en'}
    bridges={}
    use_companion=any(Path(target).name in labels for target in acceptance['targets'])
    if use_companion:
        companion=read(E/'final-tool-companion.json');assert companion['passed'] is True
        assert {Path(row['target']).name for row in companion['targets']}==labels and len(companion['targets'])==7
        for item in companion['targets']:
            assert item['target']=='build/nds/n21p/'+Path(item['target']).name
            assert artifact(ROOT/item['manifest']['path'])==item['manifest']
            assert {row['build_source']['path'] for row in item['tool_supersessions']}==allowed
            for row in item['artifacts_unchanged']:assert artifact(ROOT/row['path'])==row
            for row in item['tool_supersessions']:bridges[(Path(item['target']).name,row['build_source']['path'])]=row
    report=dict(task='N21P',passed=True,acceptance=artifact(E/'root-acceptance.json'),sources=[],builds=[],generated=[],reports=[],artifacts=[],supersession=artifact(E/'supersession.json'))
    seen=set()
    for relative in acceptance['targets']:
        target=ROOT/relative;manifest=read(target/'build-manifest.json')
        for row in manifest['sources']:
            current=artifact(ROOT/row['path'])
            if current!=row:
                assert (target.name,row['path']) in bridges,('Stale final source',row['path'])
                bridge=bridges[(target.name,row['path'])];assert bridge['build_source']==row
                assert artifact(ROOT/bridge['frozen_snapshot']['path'])==bridge['frozen_snapshot']
                assert (row['sha256'],row['bytes'])==(bridge['frozen_snapshot']['sha256'],bridge['frozen_snapshot']['bytes'])
                assert current==bridge['current_source']
            if row['path'] not in seen:
                seen.add(row['path']);report['sources'].append(copy(ROOT/row['path'],E/'final-sources'/row['path']))
        for path in [target/'build-manifest.json',*[ROOT/x['path'] for x in manifest['artifacts']]]:
            report['builds'].append(copy(path,E/'release'/target.name/path.name))
        for row in manifest['generated_inputs']+[manifest['arm7_dependency']]+manifest.get('build_drivers',[]):
            assert artifact(ROOT/row['path'])==row
            if row['path'] not in seen:
                seen.add(row['path']);report['generated'].append(copy(ROOT/row['path'],E/'final-inputs'/row['path']))
    for relative in acceptance['reports']:
        path=ROOT/relative
        # Evidence gates share E with archived sources and earlier checkpoints.
        # Archive the requested file alone there; never recurse into our output.
        candidates=[path] if path.is_relative_to(E) else sorted(path.parent.rglob('*'))
        for item in candidates:
            if not item.is_file() or item.suffix not in ('.json','.jsonl','.png','.bin','.wav'):continue
            if 'save' in item.relative_to(ROOT).parts or item.name=='dldi_sd_card.bin':continue
            # One per evidence path even when several reports share a folder.
            key=item.relative_to(ROOT).as_posix()
            if key in seen:continue
            seen.add(key);report['reports'].append(copy(item,E/'final-results'/key))
    # Tools added after a build are final reproducibility inputs, separately
    # from that build's immutable provenance manifest.
    for item in sorted((ROOT/'ports/nds').rglob('*')):
        if not item.is_file() or not(item.suffix in ('.c','.h','.inc','.py','.S','.ld','.lds') or item.name.startswith('Makefile')):continue
        key=item.relative_to(ROOT).as_posix()
        if key not in seen:
            seen.add(key);report['sources'].append(copy(item,E/'final-sources'/key))
    # Preserve the build-time tool versions as well as current final sources.
    for item in ([E/'final-tool-companion.json',*[ROOT/row['frozen_snapshot']['path'] for row in bridges.values()]] if use_companion else []):
        key=item.relative_to(ROOT).as_posix()
        if key not in seen:
            seen.add(key);report['artifacts'].append(copy(item,E/'final-results'/key))
    # Read-only audits and explicit proof binaries are evidence, without an
    # invented functional `passed` field. Only files named by root are copied.
    for relative in acceptance.get('artifacts',[]):
        item=ROOT/relative;assert item.is_file(),relative
        assert item.is_relative_to(ROOT) and not item.is_relative_to(E/'final-results')
        key=item.relative_to(ROOT).as_posix()
        if key not in seen:
            seen.add(key);report['artifacts'].append(copy(item,E/'final-results'/key))
    (E/'final-archive.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,counts={k:len(report[k]) for k in ('sources','builds','generated','reports','artifacts')})))
if __name__=='__main__':
    import sys
    if '--supersession' in sys.argv:print(json.dumps(supersession()))
    else:main()
