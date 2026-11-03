"""Compare the initial VGA HUD against existing original DOS captures."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/sdl2/checks'
PROFILES = (
    ('ru', 'originals/pkr/RUS/PKR.RES', 'ru-03'),
    ('en', 'originals/pkr/ENG/PKE.RES', 'en-02'),
)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = []
    for language, archive, reference in PROFILES:
        output = OUT / f'hud-{language}.idx'
        subprocess.run([str(ROOT / 'build/sdl2/windows/dok_hud_probe.exe'),
                        str(ROOT / archive), str(output)], check=True)
        actual = output.read_bytes()
        expected = (ROOT / f'research/evidence/reference/{reference}/level1-start.idx').read_bytes()
        assert len(actual) == len(expected) == 64000
        matches = sum(a == b for a, b in zip(actual[320*161:], expected[320*161:]))
        report.append(dict(profile=language, matching_pixels=matches, pixels=12480))
    (OUT / 'hud-report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))
    assert all(row['matching_pixels'] == row['pixels'] for row in report)

if __name__ == '__main__':
    main()
