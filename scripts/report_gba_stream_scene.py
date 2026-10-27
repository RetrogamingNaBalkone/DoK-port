"""Collect verified evidence for the first native streaming segment-BSP trial."""
import hashlib
import json
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[1]


def main():
    def read(path):return json.loads((ROOT/path).read_text(encoding='utf-8-sig'))
    def sha(path):return hashlib.sha256((ROOT/path).read_bytes()).hexdigest()
    folder=Path('research/experiments/g20a-stream-bsp-v3')
    manifest=read(folder/'prototype-build.json')
    repeat=read(Path('research/experiments/g20a-stream-bsp-repeat/prototype-build.json'))
    trial=manifest['variants']['hw_v128_stream_bsp']['rom_sha256']
    control=manifest['variants']['hw_v128_entity_rect']['rom_sha256']
    assert control=='2775bdd0caaac64b4fd99e4c6d675587fad2da9c795468dc52efacb5d53edaab'
    for name in ('hw_v128_stream_bsp','hw_v128_entity_rect'):
        assert manifest['variants'][name]['rom_sha256']==repeat['variants'][name]['rom_sha256']
        assert sha(folder/name/'dok-diagnostic.gba')==manifest['variants'][name]['rom_sha256']
    paths=[folder/'spatial.json',Path('research/experiments/g20a-hw-entity-rect-v2/spatial.json'),folder/'profile.json',
           folder/'ppu18-live.json',folder/'structure.json',folder/'control-live24.json',
           Path('analysis/g20a-dark-distance-audit.json'),folder/'prototype-build.json',Path('research/experiments/g20a-stream-bsp-repeat/prototype-build.json')]
    spatial,base,profile,ppu,structure,live,dark,*_=map(read,paths)
    assert spatial['rom_sha256']==profile['rom_sha256']==ppu['after']['rom_sha256']==structure['after']['rom_sha256']==trial
    assert base['rom_sha256']==ppu['before']['rom_sha256']==structure['before']['rom_sha256']==live['rom_sha256']==control
    assert structure['status']=='passed' and len(structure['after']['rows'])==48
    key=lambda r:(r['level'],r['cell'],r['heading'])
    reference={key(r):r for r in base['rows']};rows=spatial['rows']
    assert len(rows)==440 and set(map(key,rows))==set(reference)
    levels=[]
    for level in (1,2,3):
        selected=[r for r in rows if r['level']==level]
        levels.append(dict(level=level,cases=len(selected),median_cpu_ratio=statistics.median(r['median']/reference[key(r)]['median'] for r in selected),
                           baseline_minimum_fps=min(reference[key(r)]['presented_fps'] for r in selected),trial_minimum_fps=min(r['presented_fps'] for r in selected)))
    assert len(ppu['after']['rows'])==18 and ppu['languages']==[0,1]
    preserved={}
    for name,expected in [('DoK-port_hardware_cached_prototype.gba',control),
                          ('DoK-port_hardware_rows96_prototype.gba','97f0b3cb1d97c958bb0750128cef72aa94f64fa4107a8edc53d743c38b557f81'),
                          ('DoK-port_hardware_rows72_prototype.gba','17696958e5d1f08672754f788861c31fdc6302d242f24f0b4a5223768688888a')]:
        assert sha(Path('artifacts/gba')/name)==expected;preserved[name]=expected
    result=dict(task='G20A',status='native-architecture-checkpoint-not-selected',goal_complete=False,
        trial_rom_sha256=trial,control_rom_sha256=control,core_sha256=spatial['core_sha256'],byte_exact_repeat=True,
        levels=levels,better_fps=sum(r['presented_fps']>reference[key(r)]['presented_fps']+.001 for r in rows),
        worse_fps=sum(r['presented_fps']<reference[key(r)]['presented_fps']-.001 for r in rows),
        ppu=dict(cases=18,coverage_exact=sum(r['coverage_equal'] for r in ppu['after']['rows']),max_changed_pixels=max(r['different_rgb_pixels'] for r in ppu['after']['rows']),
                 max_overlay_changes=max(r['different_overlay_rgb_pixels'] for r in ppu['after']['rows'])),
        live24=dict(before=[dict(level=r['level'],language=r['language'],fps=r['presented_fps']) for r in live['rows']],
                    after=[dict(level=r['level'],language=r['language'],fps=r['presented_fps']) for r in ppu['live']['rows']]),
        structure_checks=dict(cases=48,scope=structure['scope'],stack=structure['after']['stack']),
        profile=profile,stream_tables=manifest['stream_tables'],preserved_roms=preserved,
        evidence=[dict(path=p.as_posix(),sha256=sha(p)) for p in paths],
        limitations=['V1/V2 had an unaligned XY overlay and are superseded; do not use them to reject an architecture.',
                      'Current prototype streams static geometry without a global face sort but reuses generic coverage samplers and per-cell affine ceilings.',
                      'Full sector/column spans, direct bitmap output, direct hardware world OBJ, moving-camera transition QA and full gameplay regression remain open.',
                      'No draw-distance shortening or AI rebalance implemented; latest darkness and wake-radius evidence is an asset audit only.'],
        next_action='Prototype full visible-column/span representation before local kernel tuning; keep original ceilings and gameplay. Compare far darkness as a separate quality axis.')
    (ROOT/'analysis/g20a-stream-bsp-trial.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(trial=trial,levels=levels,better=result['better_fps'],worse=result['worse_fps'],ppu=result['ppu']),indent=2))


if __name__=='__main__':main()
