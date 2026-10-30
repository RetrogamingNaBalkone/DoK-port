"""Freeze N18 software acceptance only after independent gates and both captures."""
import argparse,hashlib,json,shutil
from pathlib import Path
from check_items import ROOT,artifact
OUT=ROOT/'build/nds/n18';EVIDENCE=ROOT/'research/evidence/nds/n18'
def read(p):return json.loads(p.read_text('utf8'))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--accept-listening-deferred',action='store_true',help='Use only after explicit user acceptance of deferring subjective listening to N24')
    args=parser.parse_args()
    host=read(EVIDENCE/'host-audio-runtime.json');codec=read(EVIDENCE/'codec-check.json')
    source=read(ROOT/'analysis/nds-n18-audio-sources.json');regression=read(EVIDENCE/'gameplay-regression.json')
    live=read(EVIDENCE/'live-title-waveforms.json');assert live['passed']
    assert all(r['passed'] for r in (host,codec,regression)) and source['status']=='passed'
    report=dict(task='N18',status='ready-emulator' if args.accept_listening_deferred else 'software-checks-complete-awaiting-listening',passed=args.accept_listening_deferred,software_checks_passed=True,source=source,
        host=host['tests'],codec=dict(checks=codec['checks'],bytes_per_language=codec['bytes_per_language']),
        gameplay_regression=regression['gameplay'],source_callbacks=regression['source_callbacks'],
        variants={},artifacts=[],hardware_deferred='N24',
        listening='Actual emulator PCM captured and compared numerically; subjective user listening and physical DS click/noise quality are not claimed.',
        scope='Independent native x86 semantic slices, production lossless loader host checks, actual ARM7/melonDS PCM and accumulated gameplay stress. Not a complete DOS/DPMI session or Sound Blaster equivalence.')
    helpers=[p for p in (ROOT/'ports/nds/platform/audio').iterdir() if p.is_file()]
    helpers += [ROOT/('ports/nds/'+name) for name in (
        'Makefile.n18','Makefile.n18arm7','core/audio_codec.c','core/audio_rice.inc','include/dok/audio_codec.h',
        'tests/test_audio_runtime.c','tests/audio_host/nds.h','platform/gameplay_probe/main.c','platform/gameplay_probe/gameplay.c')]
    helpers += [p for p in (ROOT/'ports/nds/tools').glob('*audio*.py')]
    helper_rows=[]
    for p in sorted(set(helpers)):
        q=EVIDENCE/'sources/final-tools'/p.relative_to(ROOT);q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q)
        assert p.read_bytes()==q.read_bytes();helper_rows.append(dict(current=artifact(p),snapshot=artifact(q)))
    tools_manifest=dict(task='N18',scope='Current N18 runtime/helpers, after documented build-only prerequisite correction; captured build source snapshots remain unchanged.',files=helper_rows)
    (EVIDENCE/'current-tools-manifest.json').write_text(json.dumps(tools_manifest,indent=2)+'\n','utf8')
    for label in ('final-release-ru','final-release-en'):
        target=OUT/label;r=read(target/'emulator/result.json');a=read(target/'audio/result.json');manifest=read(target/'build-manifest.json')
        assert r['passed'] and r['fat_unchanged'] and a['passed'] and len(r['cases'])==59
        assert r['last_observation']['total_frames']==8620 and len(a['catalog'])==24
        assert r['history']['budget_passed'],r['history']
        for row in manifest['artifacts']:assert artifact(ROOT/row['path'])['sha256']==row['sha256']
        assert r['rom_sha256']==artifact(target/'dok_nds_gameplay.nds')['sha256']
        for row in manifest['sources']:
            assert artifact(EVIDENCE/'sources'/label/row['path'])['sha256']==row['sha256'],row['path']
        waveform=[]
        for row in a['catalog']:
            m=row['waveform'];assert m['complete_source_covered'] and m['whole_coefficient']>=.95
            waveform.append(dict(resource_index=row['resource_index'],loop=row['phase']==3,
                whole_coefficient=m['whole_coefficient'],complete_source_covered=m['complete_source_covered']))
        history={k:v for k,v in r['history'].items() if k not in ('cases','cold_loading')}
        probe=a['last_probe'];assert not probe[8] and not probe[9] and not probe[22]
        report['variants'][label]=dict(history=history,memory=r['memory'],audio_probe=probe,
            audio_service_max_ms=probe[21]*1000/33513982,title_cold_prefill_max_ms=probe[32]*1000/33513982,
            resident_packed_bytes=probe[33],waveforms=waveform,live_title=a.get('live_title'),live_title_waveform=live['variants']['en' if label.endswith('en') else 'ru'],
            core_sha256=r['core_sha256'],frontend_frames=r.get('frontend_frames',a.get('output_sample_frames')))
        archive=EVIDENCE/'release'/label;archive.mkdir(parents=True,exist_ok=True)
        files=[target/'build-manifest.json',target/'dok_nds_gameplay.nds',target/'dok_nds_gameplay.elf',target/'dok_nds_gameplay.map']
        for p in files:
            q=archive/p.name;shutil.copy2(p,q);report['artifacts'].append(artifact(q))
        for directory in ('emulator','audio'):
            dest=archive/directory;dest.mkdir(exist_ok=True)
            for p in (target/directory).iterdir():
                if p.name=='result.json' or (directory=='audio' and p.suffix=='.wav') or (directory=='emulator' and p.name in ('case-49-top.png','case-50-top.png','case-59-top.png')):
                    q=dest/p.name;shutil.copy2(p,q);assert q.read_bytes()==p.read_bytes();report['artifacts'].append(artifact(q))
    for name in ('host-audio-runtime.json','codec-check.json','source/report.json','gameplay-regression.json',
                 'predecessor-integrity.json','before-resident-summary.json','before-arm7-producer-summary.json','capture-checker-controls.json','inputs.json','supersession.json','build-repro.json','live-title-waveforms.json','current-tools-manifest.json','final-review.json'):
        report['artifacts'].append(artifact(EVIDENCE/name))
    arm7=EVIDENCE/'release/arm7';arm7.mkdir(parents=True,exist_ok=True)
    for p in (OUT/'arm7').iterdir():
        if p.suffix in ('.elf','.map'):
            q=arm7/p.name;shutil.copy2(p,q);report['artifacts'].append(artifact(q))
    report['artifacts'].append(artifact(OUT/'scene.dat'))
    report['deferred']='Physical DS/DS Lite, flashcart, acoustic EOS/IRQ latency, stack high-water and subjective listening remain explicit; final shared UI/audio/map cadence gate N21P.'
    (ROOT/'analysis/nds-n18-audio.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:dict(fps=v['history']['virtual_fps'],service_max_ms=v['audio_service_max_ms'],packed=v['resident_packed_bytes']) for k,v in report['variants'].items()},indent=2))
if __name__=='__main__':main()
