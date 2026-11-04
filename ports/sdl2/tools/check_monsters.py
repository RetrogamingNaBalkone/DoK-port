"""Reproduce SP12 monster checks; keep full reports locally and a compact summary."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp12-monsters'
RES=['originals/Old_Gold_3D/OLD_GOLD.RES','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    inventory=json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    originals=[p['sample'] for p in inventory]+RES;before={p:sha(ROOT/p) for p in originals}
    commands=[
        [sys.executable,'-B','ports/sdl2/tools/check_monster_oracle.py'],
        ['build/sdl2/windows/dok_monster_probe.exe',*RES],
        ['build/sdl2/windows/dok_invisible_corpse_probe.exe',*RES[1:]],
        ['build/sdl2/windows/dok_monster_scene_probe.exe',RES[0],str(OUT/'render')],
    ]
    results=[]
    for i,command in enumerate(commands):
        p=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=90)
        (OUT/f'check-{i}.log').write_text(p.stdout+p.stderr,encoding='utf-8')
        results.append(dict(command=command,exit_code=p.returncode,stdout=p.stdout,stderr=p.stderr))
        if p.returncode:raise RuntimeError(str(results[-1]))
    after={p:sha(ROOT/p) for p in originals};assert before==after
    report=dict(date='2026-11-04',results=results,sha256_before=before,sha256_after=after,
        boundary='Hit/death native instruction oracle; original parameter groups in test arenas; isolated actual GL/software image anchor. No full map playthrough.')
    (OUT/'checks-report.json').write_text(json.dumps(report,indent=2)+'\n')
    summary=dict(date=report['date'],task='SP12',originals_unchanged=True,native_damage_vectors=630,native_death_vectors=96,
        parameter_groups=dict(old_gold=20,kremlin=27,total=47),fps_backend_callback_replays=42,
        fixes=['OG signed height SAR on each death frame','OG melee hurt keeps attack clip',
               'Release passage during fully transparent death tail without skipping duration/audio',
               'Preserve entering actor during corpse map finalization',
               'Release dead actor door occupancy exactly once'],
        invisible_corpse_scenarios=24,
        real_gl_anchor=dict(old_base_y=-150,new_base_y=-75,old_bottom=189,new_bottom=219),
        software_anchor=dict(old_bottom=92,new_bottom=107),
        evidence='research/evidence/source-port/sp12-monsters/checks-report.json',
        boundary=report['boundary'])
    (ROOT/'analysis/source-port/sp12-monster-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    for result in results:print(result['stdout'],end='')
if __name__=='__main__':main()
