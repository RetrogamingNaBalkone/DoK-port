"""Regenerate compact identification tables from tracked original research."""
import csv
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
lines=['// Generated from analysis/originals resource catalogs. No resource payload.']
for lang in ('og','ru','en'):
    if lang=='og':
        with (ROOT/'analysis/originals/old-gold/resource-catalog.tsv').open(encoding='utf-8',newline='') as f:
            entries=list(csv.DictReader(f,delimiter='\t'))
    else:
        entries=json.loads((ROOT/f'analysis/originals/resources-{lang}.json').read_text(encoding='utf-8'))['entries']
    lines.append(f'static constexpr CatalogEntry catalog_{lang}[] = {{')
    for e in entries:
        raw = e['name'].encode('cp866')
        literal = '"' + ''.join('\\%03o' % byte for byte in raw) + '"'
        lines.append('    {'+literal+', '+str(e['size'])+'},')
    lines.append('};')
(ROOT/'ports/sdl2/src/catalogs.inc').write_text('\n'.join(lines)+'\n',encoding='utf-8')
