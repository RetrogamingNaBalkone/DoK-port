"""Collect G27 evidence; no new emulation and no inferred FPS gains."""
import json,zlib
from check_g27 import ROOT,OUT,PIN
from verify_gba import sha

def main():
    packing=json.loads((OUT/'packing.json').read_text());admission=json.loads((OUT/'admission.json').read_text());ppu=json.loads((OUT/'ppu.json').read_text())
    captures=json.loads(zlib.decompress((OUT/'captures.json.zlib').read_bytes()))
    rows=packing['rows'];assert len(rows)==len(captures['rows'])==len(admission['rows'])==54
    assert packing['rom_sha256']==captures['rom_sha256']==PIN
    assert all(r['stats_exact'] for r in captures['rows'])
    assert len(ppu['rows'])==6 and all(r['differences']==0 for r in ppu['rows'])
    assert sum(r['early_false_accept'] for r in rows)==2 and sum(r['early_false_reject'] for r in rows)==0
    assert sum(r['separate']['capacity'] for r in rows)==2
    assert all(not r['separate']['capacity'] or r['visible_samples']==0 for r in rows)
    summary=dict(task='G27',status='passed',date='2026-11-02',decision='Do not open H11 native renderer for current release',
        production_rom_sha256=PIN,captures=len(rows),verified_original_writes=sum(r['verified_writes'] for r in captures['rows']),
        nonempty_scenes=sum(r['visible_samples']>0 for r in rows),
        separate_capacity_passes=sum(r['separate']['capacity'] for r in rows),
        merged_capacity_passes=sum(r['merged']['capacity'] for r in rows),
        direct_source_bands=sum(r['source_bands'] for r in rows),direct_source_affine_rejects=sum(r['source_affine_rejects'] for r in rows),
        direct_source_alpha_conflicts=sum(r['source_alpha_conflicts'] for r in rows),
        early_false_accepts=sum(r['early_false_accept'] for r in rows),early_false_rejects=sum(r['early_false_reject'] for r in rows),
        admission_instructions=[min(r['arm_instructions'] for r in admission['rows']),max(r['arm_instructions'] for r in admission['rows'])],
        admission_code_bytes=admission['binary_bytes'],actual_ppu_pairs=len(ppu['rows']),actual_ppu_pixels=sum(r['pixels'] for r in ppu['rows']),
        prior_hypotheses={'H04':'not reopened: far work unchanged','H08':'not reopened: atlas is per-frame and already sampled, no persistent cache',
            'H10':'not reopened: no viable heavy GPU layout removing transport costs','sector_clip_bitmap':'not reopened: BSP/static/CPU plane cost unchanged',
            'sector_shear':'not reopened: scan/OAM pressure remains; only empty final accepts; exact direct source mapping not established',
            'H02_H03_H06':'not reopened: no changed diagnostics/UV/sparse kernel cost','H05_H09':'G25 still pending; no new quality acceptance'},
        evidence={name:dict(path=str((OUT/name).relative_to(ROOT)),sha256=sha(OUT/name)) for name in ('captures.json.zlib','packing.json','admission.json','ppu.json')},
        source_sha256={str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'ports/gba/scripts'/name for name in ('check_g27.py','audit_g27_admission.py','g27_admission.c','verify_g27_ppu.py','report_g27.py')]},
        capture_manifest={k:v for k,v in captures.items() if k!='rows'},rows=rows,admission=admission,
        limitations=packing['limitations']+['No new native renderer or FPS improvement. PPU test is isolated packet oracle.',
        'Near fixtures use synthetic camera positions, not a collision-valid gameplay route. Physical GBA/original BIOS/full playthrough not tested.'])
    target=ROOT/'analysis/gba/g27-packing-20261102.json';target.write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({k:v for k,v in summary.items() if k not in ('rows','admission','evidence','source_sha256','capture_manifest','limitations','prior_hypotheses')},indent=2))
if __name__=='__main__':main()
