"""Deterministic exact-quotient reciprocal LUT; no game payloads."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
lines = ['.section .rodata.render_tables,"a",%progbits', '.balign 4',
         '.global dok_reciprocal', 'dok_reciprocal:']
values = [0, 0xffffffff] + [(1 << 32)//d for d in range(2, 65536)]
for i in range(0, len(values), 8):
    lines.append('.word ' + ','.join(str(v) for v in values[i:i+8]))
lines.extend(('.global dok_cell_480','dok_cell_480:'))
for i in range(0,1920,32):
    lines.append('.byte '+','.join(str(n//30) for n in range(i,min(i+32,1920))))
a.output.write_text('\n'.join(lines)+'\n', encoding='ascii')
