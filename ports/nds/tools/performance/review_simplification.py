"""Review the frozen ARM pairs, matching geometry/cameras and captured images."""
import argparse
import json
from pathlib import Path
from audit_pipeline import ROOT,pin

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.resolve()
    assert output.is_relative_to(ROOT/'research/evidence/nds/n23') and not output.exists()
    pairs=[]
    for left,right,field in [('static16-control-scn-ru','static16-prepared-scn-ru','static_geometry'),
        ('static17-control-scn-ru','static17-shared-scn-ru','static_geometry'),
        ('static18-strips-scn-ru','static18-quads-scn-ru','simple_quads'),
        ('relative20-control-scn-ru','relative20-shift-scn-ru','relative_matrices')]:
        targets=[ROOT/'build/nds/n23'/s for s in (left,right)];inputs=[];reports=[]
        for target in targets:
            manifest=json.loads((target/'build-manifest.json').read_text());inputs.append(pin(target/'build-manifest.json'))
            assert manifest['compiled_inputs_unchanged']
            for row in manifest['artifacts']:
                actual=pin(ROOT/row['path']);assert actual==row
            cost=json.loads((target/'cost-result.json').read_text());pipeline=json.loads((target/'pipeline-result.json').read_text())
            assert cost['diagnostic_completed'] and pipeline['passed']
            inputs.extend(pin(target/name)for name in ('cost-result.json','pipeline-result.json'))
            reports.append((manifest,cost,pipeline))
        a,b=reports
        assert a[0]['sources']==b[0]['sources']
        for key in set(a[0])|set(b[0]):
            if key=='generated_inputs':
                normalize=lambda rows:[(Path(r['path']).name,r['bytes'],r['sha256'])for r in rows]
                assert normalize(a[0][key])==normalize(b[0][key])
                continue
            if key in ('label','feature_selection','artifacts','sources',field):continue
            assert a[0][key]==b[0][key],(key,left,right)
        assert len(a[2]['rows'])==len(b[2]['rows'])==154
        ar={r['ordinal']:r for r in a[2]['rows']};br={r['ordinal']:r for r in b[2]['rows']}
        for ordinal,row in ar.items():
            for key in ('cells','quads','camera_x','camera_z','worst_case_fits'):
                assert row[key]==br[ordinal][key],(left,right,ordinal,key)
        images=[]
        for source in sorted(targets[0].rglob('*.png')):
            other=targets[1]/source.relative_to(targets[0]);assert other.exists()
            assert source.read_bytes()==other.read_bytes(),('Different capture',source,other)
            images.append(dict(control=pin(source),candidate=pin(other)))
        assert images
        cases={}
        for case,base in a[1]['cases'].items():
            new=b[1]['cases'][case];assert base['frames']==new['frames']
            cases[case]=dict(frames=base['frames'],control_ms=base['mean_ms'],candidate_ms=new['mean_ms'],
                saved_cpu_ms=base['mean_ms']['cpu_excluding_wait']-new['mean_ms']['cpu_excluding_wait'])
        pairs.append(dict(control=left,candidate=right,changed_feature=field,inputs=inputs,cases=cases,exact_png_pairs=images))
    audit_target=ROOT/'build/nds/n23/relative20-audit-scn-ru'
    gpu=json.loads((audit_target/'pipeline-result.json').read_text());assert gpu['passed']
    report=dict(task='N23',passed=True,acceptance_eligible=False,reviewer=pin(Path(__file__)),pairs=pairs,
        gpu_matrix_audit=dict(report=pin(audit_target/'pipeline-result.json'),result=gpu['relative_matrix_audit']),
        limitations=['Fine per-cell/callback instrumentation adds overhead; none of these FPS values accepts production.',
            'Exact captures cover this three-case packet only; final RU/EN and natural-AI routes remain separate gates.'])
    output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(dict(passed=True,report=pin(output),pairs=len(pairs),
        exact_image_pairs=sum(len(p['exact_png_pairs'])for p in pairs),gpu=gpu['relative_matrix_audit'])))
if __name__=='__main__':main()
