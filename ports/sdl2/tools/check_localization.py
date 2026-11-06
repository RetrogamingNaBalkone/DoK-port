"""SP14 catalogue, single-RES rendering and real paused-language-switch checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import struct
import zlib

from check_assets import read_archive
from level_reference import parse

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp14-localization'
EXE = ROOT / 'build/sdl2/windows/dok_port.exe'


def normalize(value):
    return re.sub(r'\s+', ' ', value.split('\0')[0].split('$')[0]).strip()


def readback_colors(path):
    # The port's own save_png emits RGB8 scanlines with filter zero.
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    at = 8
    compressed = b''
    while at < len(data):
        count = struct.unpack_from('>I', data, at)[0]
        kind = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + count]
        if kind == b'IHDR':
            width, height, depth, mode, _, _, _ = struct.unpack('>IIBBBBB', payload)
            assert depth == 8 and mode == 2
        if kind == b'IDAT':
            compressed += payload
        at += count + 12
    raw = zlib.decompress(compressed)
    stride = width * 3 + 1
    assert len(raw) == stride * height
    colors = set()
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        assert row[0] == 0
        colors.update(row[x:x + 3] for x in range(1, len(row), 3))
    assert len(colors) > 8, f'Blank readback: {path}'
    return len(colors)


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    if args.out:
        OUT = args.out.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    data = json.loads((ROOT / 'ports/sdl2/data/localization.json').read_text(encoding='utf-8'))
    originals = {p['path']: hashlib.sha256((ROOT / p['path']).read_bytes()).hexdigest()
                 for p in data['sources'].values()}
    for p in data['sources'].values():
        assert originals[p['path']] == p['sha256']
    bindings = []
    counts = {}
    font_checks = []
    characters = {b for pair in data['texts'].values() for text in pair.values() for b in text.encode('cp866') if b > 32}
    for profile, source in data['sources'].items():
        count = 0
        entries = dict(read_archive((ROOT / source['path']).read_bytes()))
        for font in (b'FONT8', b'FONT16'):
            payload = entries[font]
            height = len(payload) // 256
            missing = [c for c in characters if not any(payload[c * height:(c + 1) * height])]
            assert not missing, (profile, font, missing)
            font_checks.append(dict(profile=profile, font=font.decode(), glyphs=len(characters), missing=0))
        for name, payload in entries.items():
            if not name.startswith(b'LEVEL'):
                continue
            level = parse(payload, name.decode(), profile == 'og')
            for obj in level['objects']:
                for kind, offset, value in obj['fields']:
                    if kind != 6:
                        continue
                    original = bytes.fromhex(value).decode('cp866')
                    key = data['aliases'][normalize(original)]
                    assert key in data['texts']
                    bindings.append(dict(profile=profile, level=name.decode(), glyph=obj['glyph'],
                                         offset=offset, key=key, original=original))
                    count += 1
        counts[profile] = count
    (OUT / 'source-bindings.json').write_text(json.dumps(bindings, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    for key, pair in data['texts'].items():
        assert set(pair) == {'ru', 'en'}
        for text in pair.values():
            encoded = text.encode('cp866')
            assert encoded and '\ufffd' not in text
            if not any(part in key for part in ('.story.', 'dataset.', 'level.')):
                assert len(encoded) <= 38, (key, len(encoded))
    assert data['texts']['cheat.idkfa']['en'] == 'You sneaky Doomer!'
    commands = [
        [sys.executable, '-B', 'ports/sdl2/tools/generate_localization.py', '--check'],
        ['build/sdl2/windows/dok_localization_tests.exe'],
        ['build/sdl2/windows/dok_localization_probe.exe', str(ROOT), str(OUT / 'render')],
    ]
    results = []
    for index, command in enumerate(commands):
        p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', timeout=180,
                           env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
        (OUT / f'check-{index}.log').write_text(p.stdout + p.stderr, encoding='utf-8')
        results.append(dict(command=command, exit_code=p.returncode, stdout=p.stdout, stderr=p.stderr))
        assert p.returncode == 0, results[-1]
    readbacks = []
    for path in sorted((OUT / 'render').glob('*-story.png')):
        readbacks.append(dict(path=str(path.relative_to(ROOT)), distinct_colors=readback_colors(path)))
    processes = []
    for profile, source in data['sources'].items():
        level = 'LEVEL4' if profile == 'og' else 'LEVEL1'
        for backend in ('gl', 'software'):
            for locale in ('ru', 'en'):
                name = f'{profile}-{backend}-{locale}'
                user = OUT / 'process' / name
                user.mkdir(parents=True, exist_ok=True)
                script = user / 'script.txt'
                screenshot = user / 'translated.png'
                script.write_text('0 key I\n1 key D\n2 key K\n3 key F\n4 key A\n5 key Escape\n6 state before\n'
                                  '7 key Down\n8 key Return\n9 key Return\n'
                                  + '10 key Down\n' * (7 if backend == 'gl' else 6)
                                  + '11 key Right\n12 state after\n13 key Escape\n14 key Escape\n15 key Escape\n'
                                  + f'17 shot {screenshot}\n18 state resumed\n', encoding='utf-8')
                command = [str(EXE), '--hidden', '--res', source['path'], '--level', level, '--skip-intro', '--renderer', backend,
                           '--locale', locale, '--hud', 'classic', '--size', '800x600', '--frames', '24',
                           '--user-dir', str(user), '--ui-script', str(script)]
                p = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=45,
                                   env=dict(os.environ, SDL_AUDIODRIVER='dummy'))
                assert p.returncode == 0, (name, p.stdout, p.stderr)
                log = (user / 'port.log').read_text(encoding='utf-8')
                states = {m[1]: dict(re.findall(r'(\w+)=([^\s]*)', m[2]))
                          for m in re.finditer(r'^STATE (\w+) (.*)$', log, re.M)}
                before, after = states['before'], states['after']
                for field in ('world', 'ticks', 'pose', 'hp', 'charge', 'maps'):
                    assert before[field] == after[field], (name, field, before, after)
                target = 'en' if locale == 'ru' else 'ru'
                assert before['message_key'] == after['message_key'] == 'cheat.idkfa', (name, states)
                assert after['locale'] == ('1' if target == 'en' else '0'), (name, states)
                assert after['message_hex'] == data['texts']['cheat.idkfa'][target].encode('cp866').hex(), (name, states)
                assert states['resumed']['world'] == before['world']
                processes.append(dict(profile=profile, backend=backend, initial_locale=locale,
                                      target_locale=target, paused_world_preserved=True, message_key=after['message_key'],
                                      message_hex=after['message_hex'], screenshot=str(screenshot.relative_to(ROOT))))
    after = {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in originals}
    assert originals == after
    report = dict(task='SP14', semantic_keys=len(data['texts']), source_aliases=len(data['aliases']),
                  original_object_fields=len(bindings), fields_per_profile=counts, checks=results,
                  real_process_locale_switches=processes, originals_sha256_before=originals,
                  originals_sha256_after=after, fonts=font_checks, render_readbacks=readbacks)
    (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    summary = {key: report[key] for key in ('task', 'semantic_keys', 'source_aliases', 'original_object_fields', 'fields_per_profile')}
    summary.update(real_process_locale_switches=len(processes), originals_unchanged=True,
                   fonts_checked=len(font_checks), glyphs_per_font=len(characters), story_readbacks=len(readbacks),
                   evidence=str((OUT / 'report.json').relative_to(ROOT)))
    (OUT / 'summary.json' if args.out else ROOT / 'analysis/source-port/sp14-localization-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
