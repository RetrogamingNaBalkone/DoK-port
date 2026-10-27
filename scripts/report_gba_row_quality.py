"""Collect the row-resolution experiments without replacing the cached control."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics

ROOT=Path(__file__).resolve().parents[1]
FOLDER=Path('research/experiments/g20a-hw-row-quality-v3')
OLD=Path('research/experiments/g20a-hw-row-quality-v2')


def read(path):
    return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--copy-rom',action='store_true')
    args=parser.parse_args()
    manifest=read(FOLDER/'prototype-build.json')
    repeat=read(Path('research/experiments/g20a-hw-row-quality-repeat/prototype-build.json'))
    accepted=read(Path('analysis/g20a-hardware-cache.json'))
    control=manifest['variants']['hw_v128_entity_rect']['rom_sha256']
    assert control==accepted['rom_sha256']=='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    preserved={**accepted['preserved_artifacts'],accepted['artifact']:control}
    for path,digest in preserved.items():assert sha(Path(path))==digest,path
    originals=accepted['originals_unchanged']
    for original in originals:assert sha(Path(original['path']))==original['sha256'],original
    reference=read(Path('research/experiments/g20a-hw-entity-rect-v2/spatial.json'))
    assert reference['rom_sha256']==control
    rows={};inputs=[];copies=[]
    for n in ('96','72'):
        variant='hw_rows'+n;build=FOLDER/variant
        digest=manifest['variants'][variant]['rom_sha256']
        assert repeat['variants'][variant]['rom_sha256']==sha(build/'dok-diagnostic.gba')==digest
        reports={name:FOLDER/(name+n+'.json') for name in
                 ('ui','sprites','flow','ai','ai-combat','items','combat','interactions','time','turn120','walking120')}
        for name in ('turn120','walking120'):reports[name]=FOLDER/(name+'-'+n+'.json')
        for name in ('water','transitions','quality','spatial'):
            reports[name]=(FOLDER if n=='96' else OLD)/(name+n+'.json')
        reports['arm']=FOLDER/'arm96.json' if n=='96' else OLD/'hw_rows72/arm.json'
        reports['row_probe']=FOLDER/'rows96.json' if n=='96' else Path('research/experiments/g20a-hw-row-probes/rows72.json')
        if n=='96':reports['profile']=FOLDER/'profile96.json'
        data={name:read(path) for name,path in reports.items()}
        for name,d in data.items():
            if name in ('quality','row_probe'):continue
            assert d['rom_sha256']==digest,(n,name)
            if 'status' in d:assert d['status']=='passed',(n,name)
        assert data['time']['edge_vectors_on_arm']=='passed' and data['time']['exact_rational_samples']
        assert not data['interactions']['original_affine_visual_oracle_checked']
        probe=data['row_probe']
        assert probe['status']=='passed' and probe['source_rows']==int(n)
        assert probe['rom_sha256']==manifest['variants'][variant+'_probe']['rom_sha256']
        quality=data['quality'];assert quality['after']['rom_sha256']==digest and quality['before']['rom_sha256']==control
        assert len(quality['after']['rows'])==396
        assert all(r['different_overlay_rgb_pixels']==0 for r in quality['after']['rows'])
        spatial=data['spatial'];assert len(spatial['rows'])==440 and not spatial['instrumented']
        paired=list(zip(reference['rows'],spatial['rows']))
        assert all((a['level'],a['cell'],a['heading'])==(b['level'],b['cell'],b['heading']) for a,b in paired)
        assert len(data['transitions']['rows'])==282
        assert all(not r['mixed_frames'] and not r['overflows'] for r in data['transitions']['rows'])
        symbols=manifest['variants'][variant]['symbols']
        live={name:[dict(level=r['level'],language=r['language'],fps=r['presented_fps']) for r in data[name]['rows']]
              for name in ('turn120','walking120')}
        for name in ('turn120','walking120'):
            for r in data[name]['rows']:
                assert r['audio']['underruns']==0 and r['hardware_bg_new_overflows']==0
                assert r['camera_follows_runtime'] and not r['ai_disabled']
        target=Path('artifacts/gba')/f'DoK-port_hardware_rows{n}_prototype.gba'
        copies.append((build/'dok-diagnostic.gba',target))
        rows[n]=dict(rom_sha256=digest,rom_bytes=manifest['variants'][variant]['rom_bytes'],artifact=target.as_posix(),
            ewram_bytes=symbols['__bss_end']-0x02000000,iwram_bytes=symbols['__iwram_end']-0x03000000,stack_reserve_bytes=8192,
            clean_repeat_byte_exact=True,live=live,
            spatial_minima={str(l):min(r['presented_fps'] for r in spatial['rows'] if r['level']==l) for l in (1,2,3)},
            median_cpu_ratio={str(l):statistics.median(b['median']/a['median'] for a,b in paired if b['level']==l) for l in (1,2,3)},
            spatial_fps_improved=sum(b['presented_fps']>a['presented_fps']+.001 for a,b in paired),
            spatial_fps_regressed=sum(b['presented_fps']<a['presented_fps']-.001 for a,b in paired),
            native_overlay_exact_frames=396,transition_callbacks=sum(r['video_frames'] for r in data['transitions']['rows']),
            observed_stack=data['turn120']['stack'],observed_irq_stack=data['turn120']['irq_stack'],
            input_reports={name:path.as_posix() for name,path in reports.items()})
        inputs.extend(reports.values())
    for n,variant in [('128','hw_v128_entity_rect'),('96','hw_rows96'),('72','hw_rows72')]:
        for image in (ROOT/FOLDER/variant).glob('L*-cell*-ppu.png'):
            copies.append((FOLDER/variant/image.name,Path('artifacts/gba/row-quality')/(n+'-'+image.name)))
    if args.copy_rom:
        for source,destination in copies:
            if (ROOT/destination).exists():assert sha(source)==sha(destination),destination
        for source,destination in copies:
            (ROOT/destination).parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/source,ROOT/destination)
    stress=read(FOLDER/'stress96.json')
    assert stress['rom_sha256']==rows['96']['rom_sha256'] and stress['status']=='passed' and not stress['target_met']
    report=dict(task='G20A',date='2026-10-27',status='comparison-prototypes-verified-goal-open',targets_met=False,
        default_control_unchanged=control,copied=args.copy_rom,variants=rows,
        decision='96 rows is the moderate quality candidate;72 rows remains a comparison, no universal15+ demonstrated.',
        physical_gba_tested=False,original_bios_tested=False,
        stress96=dict(guards_logic_audio_passed=True,frame_budget_met=False,
            all_awake_fps=[r['presented_fps'] for r in stress['rows'] if r.get('artificial_all_enemies_awake')]),
        originals_unchanged=originals,preserved_artifacts=preserved,
        inputs={path.as_posix():sha(path) for path in [*inputs,FOLDER/'stress96.json',FOLDER/'prototype-build.json']},
        limitations=['Spatial440 freezes simulation; live120 routes do not prove whole-level minima.',
                     'The new282-transition checks do not cover the known baseline294 Pause/Resume transient issue.',
                     'G09 checks production interaction logic; its original affine pixel oracle is explicitly disabled for these quality variants.',
                     '96-row affine rounding repeats one preceding-strip row at band boundaries; WIN0 clips scene OBJ below144.',
                     '72-row artificial all-awake stress not rerun;96-row stress still misses the frame budget.'])
    (ROOT/'analysis/g20a-row-quality-trial.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Verified',[(n,d['rom_sha256'],d['spatial_minima']) for n,d in rows.items()],'copied',args.copy_rom)


if __name__=='__main__':main()
