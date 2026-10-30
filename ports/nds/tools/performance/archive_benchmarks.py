"""Freeze controlled N23 benchmark reports and recompute paired timing summaries."""
import json,shutil,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from check_items import ROOT,artifact
OUT=ROOT/'research/evidence/nds/n23'

def pair(name,labels,run,keys,timer_key='ticks'):
    directory=OUT/name;directory.mkdir(exist_ok=True)
    reports=[];inputs=[]
    for label in labels:
        source=ROOT/'build/nds/n23'/label/run/'result.json'
        d=json.loads(source.read_text('utf8'));assert d['passed']
        destination=directory/(label+'.json')
        if destination.exists():assert destination.read_bytes()==source.read_bytes()
        else:shutil.copyfile(source,destination)
        reports.append(d);inputs.append(artifact(destination))
    reference=[[r[k]for k in keys]for r in reports[0]['rows']]
    for d in reports:assert reference==[[r[k]for k in keys]for r in d['rows']]
    summary=dict(task='N23',passed=True,scope=reports[0]['scope'],rows=len(reference),equal_fields=keys,inputs=inputs,
        variants=[dict(label=label,total_ms=sum(r[timer_key]for r in d['rows'])*1000/d['timer_hz'],
            max_ms=max(r[timer_key]for r in d['rows'])*1000/d['timer_hz'])for label,d in zip(labels,reports)],
        limitation='FNV equality is not byte-exact proof; independent native differential checks are separate.')
    (directory/'pair.json').write_text(json.dumps(summary,indent=2)+'\n','utf8');print(json.dumps(summary['variants']))

if __name__=='__main__':
    pair('cache-group-benchmark',['baseline-group-bench-ru','batch-group-bench-ru','batch-frames-group-bench-ru'],
        'cache-benchmark-level1',['id','used','evictions','cache_fnv1a'])
    pair('map-benchmark',['vert-mapbench-ru','vert-floorruns-mapbench-ru'],
        'map-benchmark-level2',['scale','view','pixels_fnv1a'],'cpu_ticks')
    labels=['codecbase01-ru','codecsmall01-ru','codecwide01-ru','codecarmwide01-ru']
    pair('codec-benchmark',[s for s in labels if (ROOT/'build/nds/n23'/s/'codec-benchmark-level1/result.json').exists()],
        'codec-benchmark-level1',['id','packed_bytes','decoded_bytes','pixel_fnv1a'])
    pair('cache-replace-benchmark',['replacebase03-ru','replacefast03-ru'],
        'cache-benchmark-level1',['id','used','evictions','logical_fnv1a'])
