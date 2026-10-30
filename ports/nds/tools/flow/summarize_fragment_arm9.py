"""Integrity-check frozen public-SDK fixtures and publish their compact result."""
import json
from pathlib import Path
from check_host import ROOT,pin

def main():
    count=0;cases={}
    for variant in ('baseline','partial'):
        evidence=ROOT/f'research/evidence/nds/n22/fragment-{variant}-01'
        report_path=evidence/'emulator.json';report=json.loads(report_path.read_text(encoding='utf-8'))
        manifest_path=evidence/'build-manifest.json';manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
        assert report['passed'] and report['disk']['passed'] and not report['disk']['changed_files']
        for row in manifest['inputs']+report['observer_inputs']:
            for name in ('current','snapshot'):
                assert pin(ROOT/row[name]['path'])==row[name];count+=1
        for row in manifest['artifacts']+report['snapshots']+report['screens']+[manifest['arm7']]:
            assert pin(ROOT/row['path'])==row;count+=1
        cases[variant]=dict(report=pin(report_path),manifest=pin(manifest_path),artifacts=manifest['artifacts'],
                            actual=report['actual'],frames=report['frames'],
                            build_input_snapshots=len(manifest['inputs']),observer_input_snapshots=len(report['observer_inputs']))
        if variant=='partial':
            cases[variant]['relocated']=report['relocated'];cases[variant]['pixel_sample_count']=len(report['pixels'])
            cases[variant]['maximum_RGB8_sample_error']=max(abs(a-b)for row in report['pixels']for a,b in zip(row['expected'],row['actual']))
    matches=[]
    candidate=ROOT/'research/evidence/nds/n22/candidates/candidate-partial01-ru/sources'
    for name in ('ports/nds/platform/render_probe/render_backend.c','ports/nds/render/vram_layout.c','ports/nds/include/dok/vram_layout.h'):
        current=pin(ROOT/name);saved=pin(candidate/name);assert current['sha256']==saved['sha256']
        matches.append(dict(current=current,candidate=saved));count+=2
    summary=dict(task='N22',passed=True,scope='Exact V14 fragmentation via public BlocksDS/backend API; generated texels; emulator only',
       normal_playthrough_claim=False,physical_DS_claim=False,cases=cases,candidate_implementation_match=matches,
       integrity_pins_checked=count,commands=dict(build='py -3.14 -B -X utf8 ports/nds/tools/flow/build_fragment_arm9.py {baseline|partial}',
       execute='py -3.14 -B -X utf8 ports/nds/tools/flow/run_fragment_arm9.py {baseline|partial}',
       summarize='py -3.14 -B -X utf8 ports/nds/tools/flow/summarize_fragment_arm9.py'),
       conclusion='One 16384-byte current HIT moves; pending payload67840 and no palette reset fit map-first119-line epoch. Baseline367360 reproduces HARDWARE6. No private allocator writes.')
    path=ROOT/'analysis/nds-n22-sdk-fragmentation.json';path.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(passed=True,integrity_pins_checked=count,uploads={key:value['actual']['upload_bytes']for key,value in cases.items()})))

if __name__=='__main__':main()
