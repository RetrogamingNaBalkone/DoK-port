"""SP06 reproducible lifecycle report; native ASM provenance is exported separately."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp06-world/checks'
RES = ['originals/Old_Gold_3D/OLD_GOLD.RES', 'originals/pkr/RUS/PKR.RES', 'originals/pkr/ENG/PKE.RES']

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in RES}
    commands = [['build/sdl2/windows/dok_lifecycle_tests.exe', *RES]]
    for path in RES:
        for level in (['LEVEL4'] if 'Old_Gold' in path else ['LEVEL1', 'LEVEL2', 'LEVEL3']):
            user = ROOT / 'build/sdl2/checks/sp06' / (Path(path).stem + '-' + level)
            commands.append(['build/sdl2/windows/dok_port.exe', '--renderer', 'software', '--size', '320x200',
                             '--res', path, '--level', level, '--skip-intro', '--frames', '30', '--user-dir', str(user)])
    results = []
    for index, command in enumerate(commands):
        p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        (OUT / f'{index}.log').write_text(p.stdout + p.stderr, encoding='utf-8')
        results.append(dict(command=command, exit=p.returncode, stdout=p.stdout, stderr=p.stderr))
        if p.returncode:
            raise RuntimeError(f'Failed command: {command}\n{p.stdout}\n{p.stderr}')
    after = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in RES}
    assert before == after
    (OUT / 'report.json').write_text(json.dumps(dict(results=results, sha256_before=before, sha256_after=after,
        scope='C++ lifecycle plus 30-frame process startup; no playthrough or renderer equivalence'), indent=2) + '\n', encoding='utf-8')
    print(results[0]['stdout'], end='')
    print('7 actual-process startups PASS; original RES hashes unchanged')

if __name__ == '__main__':
    main()
