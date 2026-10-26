"""Audit actual PKR/PKE exit and score-item declarations, independently of flow.

An inherited quota branch is present in EXE, but these six LEVEL resources have
no category99 pickup at all. This script records data, not a DOS playthrough.
"""
import hashlib
import json
from pathlib import Path

from verify_packing import originals


def main():
    rows = []
    for language, original in originals().items():
        for number, model in original['levels'].items():
            maps = [bytes.fromhex(value) for value in model['maps']]
            exits, score_items = [], []
            for index, obj in enumerate(model['objects']):
                fields = {offset: value for kind, offset, value in obj['fields']}
                if obj['class'] == 12 and fields.get(0x66) == 1000:
                    exits.append(dict(object=index, glyph=obj['glyph'],
                                      fcmap_cells=[i for i, value in enumerate(maps[2]) if value == obj['glyph']]))
                if obj['class'] == 3 and fields.get(0x54) == 99:
                    score_items.append(dict(object=index, glyph=obj['glyph'], value=fields.get(0x56),
                                            map_uses=[plane.count(obj['glyph']) for plane in maps]))
            assert not score_items, (language, number, score_items)
            assert len(exits) == 1 and exits[0]['glyph'] == 0xb2
            assert len(exits[0]['fcmap_cells']) == (4 if number == 3 else 2)
            rows.append(dict(language=language, level=number, archive=original['archive_path'].relative_to(Path.cwd()).as_posix(),
                             archive_sha256=original['archive_sha256'], exe_sha256=original['exe_sha256'],
                             score_item_definitions=score_items, score_total=0, exits=exits,
                             address_system='original LEVEL FCMap row-major file cells, not GBA reversed runtime rows'))
    result = dict(task='G18', status='passed', script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  scope='Original PKR/PKE LEVEL data; actual EXE branch is verified separately by G18 native probes',
                  levels=rows, conclusion='No score collection is required for exits in any supplied PKR/PKE level. The inherited equality branch is satisfied by initial score0 and total0.',
                  whole_dos_session_tested=False)
    Path('analysis/g18-exit-audit.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PKR/PKE exit audit: all6 LEVELs have zero score-item definitions, total0, real B2 exits')


if __name__ == '__main__':
    main()
