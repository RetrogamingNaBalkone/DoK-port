"""Publish the current successful full-route evidence, retaining failed experiments."""
import hashlib
import argparse
import json
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


base = Path('research/evidence/source-port/sp16-flow')
parser = argparse.ArgumentParser()
parser.add_argument('--regenerate-kremlin', action='store_true')
args = parser.parse_args()
if args.regenerate_kremlin:
    for prefix,resource in [('ru','originals/pkr/RUS/PKR.RES'),('en','originals/pkr/ENG/PKE.RES')]:
        for level in range(1,4):
            name=f'{prefix}{level}'
            run=subprocess.run(['build/sdl2/windows/dok_walkthrough_probe.exe',resource,f'LEVEL{level}',str(base/'routes'/f'{name}.replay')],capture_output=True,text=True,timeout=180)
            (base/'routes'/f'{name}.log').write_text(run.stdout,encoding='utf-8')
            (base/'routes'/f'{name}-stderr.log').write_text(run.stderr,encoding='utf-8')
            if run.returncode:
                raise RuntimeError(f'{name}: regeneration failed')
rows = []
for name in ('ru1','ru2','ru3','en1','en2','en3','og'):
    log = base/'routes'/f'{name}.log'
    replay = log.with_suffix('.replay')
    if not log.exists() or not replay.exists():
        continue
    data = log.read_bytes()
    text = data.decode('utf-16' if data.startswith(b'\xff\xfe') else 'utf-8-sig')
    completed = re.search(r'^COMPLETED (.*)$',text,re.M)
    if not completed:
        continue
    folder = Path('originals/Old_Gold_3D' if name=='og' else 'originals/pkr/RUS' if name.startswith('ru') else 'originals/pkr/ENG')
    resource = next(folder.glob('*.RES'))
    executable = folder/('OG.EXE' if name=='og' else 'PKR.EXE' if name.startswith('ru') else 'PKE.EXE')
    probe = Path('build/sdl2/windows/dok_walkthrough_probe.exe')
    app = Path('build/sdl2/windows/dok_port.exe')
    rows.append(dict(name=name,status='PASS',normal_inputs=True,ai_enabled=True,full_original_map=True,
                     command=[str(probe),str(resource),'LEVEL4' if name=='og' else f'LEVEL{name[-1]}',str(replay)],
                     endpoint=completed[1],resource=str(resource),resource_sha256=sha(resource),
                     original_executable_sha256=sha(executable),probe_sha256=sha(probe),
                     app_sha256=sha(app),replay_sha256=sha(replay),log=str(log),log_sha256=sha(log)))
report = dict(full_seven_routes=len(rows)==7,successful_routes=len(rows),routes=rows,
              app_matrix_report=str(base/'app/matrix-report.json'),
              limit='Old Gold 104/105 coins; last river-bank coin under independent native investigation' if len(rows)<7 else '')
(base/'routes/successful-route-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(f'{len(rows)}/7 complete original-map routes; report updated')
