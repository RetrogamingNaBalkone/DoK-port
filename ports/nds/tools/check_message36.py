"""Check actual HUD C against original RU/EN text and independent pixels."""
import json
import sys
from check_hud_host import build, Driver, ROOT
from hud_pixel_oracle import OriginalHudArt, render_hud, message_pages
from audit_world import originals, parse_level, T
sys.path.insert(0, str(ROOT / 'ports/nds/tools/ui'))
from extract_ui import load_exports


def main():
    build()
    driver, art = Driver(), OriginalHudArt()
    state = dict(language=1, level=1, health=100, charge=150, max_health=100,
                 max_charge=200, lives=3, score=0, keys=7, weapon=6)
    counts = {}
    for language, source in originals().items():
        state['language'] = language
        messages = []
        for level in range(1, 4):
            entry = next(e for e in source['entries']
                         if bytes.fromhex(e['name']).decode('cp866') == f'LEVEL{level}')
            for obj in parse_level(entry['payload'])['objects']:
                for field in obj['fields']:
                    if field['kind'] == T:
                        messages.append(bytes.fromhex(field['value']['hex'])[:39])
        native, _ = load_exports(language)
        messages.extend(bytes.fromhex(e['cp866_hex']) for e in native)
        long = []
        for message in messages:
            clean = message.split(b'\0', 1)[0].split(b'$', 1)[0].strip(b' ')
            if len(clean) > 32:
                long.append(clean.decode('cp866'))
            assert len(message_pages(clean, 32)) == (2 if len(clean) > 32 else 1)
            actual = driver.hud(state, message)
            assert actual == render_hud(art, state, message)[0]
            assert actual == driver.hud(state, clean)
            assert actual == driver.hud(state, b'   ' + clean + b'   $ignored')
        counts[language] = dict(entries=len(messages), over32=len(long), unique=sorted(set(long)))
    assert counts[1]['over32'] == 3 and len(counts[1]['unique']) == 1
    assert counts[2]['over32'] == 0
    for message in (b'', b'    ', b'  $ignored', b'X' * 32, b'X' * 32 + b'   \0ignored'):
        assert driver.hud(state, message) == render_hud(art, state, message)[0]
    report = dict(passed=True, columns=32, rows=2, guarded_rasters=driver.draws, languages=counts)
    path = ROOT / 'analysis/nds-message36-20261107.json'
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
