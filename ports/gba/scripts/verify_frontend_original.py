"""Bind UI01 DOS captures and read-only Ghidra evidence to unchanged samples.

Run analyze-ui.ps1 and dos_reference.py for both languages first. This script
reads exported segments/ASM only; it never disassembles original EXE files.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import hashlib
import json
from pathlib import Path

from verify_gba import png
from verify_sprites_gba import original_animations


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,default=Path('build/gba/g20b'))
    args=parser.parse_args()
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    results=[]
    for language,lang in enumerate(('ru','en')):
        directory=args.out/('dos-'+lang)
        report=json.loads((directory/'report.json').read_text(encoding='utf-8'))
        manifest=json.loads((Path('research/evidence/ui')/lang/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert report['originals_unchanged'] and report['runtime_copies_unchanged']
        assert sha(Path(manifest['sample']))==manifest['sha256_before']==manifest['sha256_after']
        resources,origin=original_animations(language)
        sprite=resources['ЗАСТАВКА_СПР'.encode('cp866').hex()][18:]
        matches=[p.name for p in directory.glob('*.idx') if p.read_bytes()==sprite]
        assert matches,('Original DOS splash not observed',lang)
        palette=resources['ЗАСТАВКА_ПАЛ'.encode('cp866').hex()]
        rgb=b''.join(bytes(v*255//63 for v in palette[x*3:x*3+3]) for x in sprite)
        png(directory/'splash-source-palette.png',320,200,rgb)
        segment=(Path('research/evidence/ui')/lang/('1190.bin' if language==0 else '10d0.bin')).read_bytes()
        names=(['ХОРОМЫ НЕЧИСТИ','ПОДЗЕМНАЯ РЕКА','ПОДВАЛЫ КРЕМЛЯ'] if language==0 else
               ['PALACE OF EVIL','SACRED RIVER','UNDERGROUND KREMLIN'])
        offsets=[segment.index(n.encode('cp866')+b'\0') for n in names]
        for offset,name in ((0x132,'ЗАСТАВКА_ПАЛ'),(0x13f,'ЗАСТАВКА_СПР'),(0x159,'ЗАСТАВКА_ЗВУК'),(0x175,'МЕНЮ_ПАЛ')):
            assert segment[offset:].startswith(name.encode('cp866')+b'\0')
        asm=(Path('research/evidence/ui')/lang/'evidence.asm').read_text(encoding='utf-8')
        entry='1008:01ba' if language==0 else '1008:023e'
        end='1008:02f7' if language==0 else '1008:044b'
        body=asm[asm.index('FUNCTION '+entry):asm.index('FUNCTION '+end)]
        once='CALLF 0x1108:01f7' if language==0 else 'CALLF 0x1060:173d'
        stop='CALLF 0x1108:026d' if language==0 else 'CALLF 0x1060:17b3'
        assert once in body and stop in body and 'PUSH DX' in body and 'RETF' in body
        results.append(dict(language=lang,manifest=manifest,dos_report_sha256=sha(directory/'report.json'),
            splash_dos_indices_equal_resource=matches,res=origin,original_names=names,
            name_addresses=[('1190' if language==0 else '10d0')+f':{n:04x}' for n in offsets],
            address_system='Ghidra NE loader, not file offset or DPMI selector',
            splash_function=entry,play_once_and_stop_asm=True,cdecl16far_caller_cleanup=True,
            source_palette_used_for_preview=True,dos_dac_capture=False))
    result=dict(task='UI01/G20B',status='passed',script_sha256=sha(Path(__file__)),samples=results,
        adaptations=['320x200 resized to240x160 without letterboxing','GBA music loops and continues through menus; DOS splash calls once then stop',
                     'GBA main menu, language switch and start-to-level-selector hierarchy'],
        limits=['DOS input is the researched game keyboard state, not IRQ1','No new Sound Blaster waveform capture or full DOS playthrough'])
    (args.out/'original-evidence.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('UI01 DOS splash indices and Ghidra names/once-stop: RU/EN passed')


if __name__=='__main__':main()
