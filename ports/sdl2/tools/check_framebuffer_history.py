"""Verify untouched original pixels retain injected sentinel and preceding frame."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[3]
r=json.load(open(ROOT/'research/evidence/source-port/sp07-render/vesa-trap-matrix.json'));out=[]
for x in r:
 if x.get('language')=='en' and x['differences']:
  d=ROOT/Path(f"research/evidence/source-port/sp07-render/en-level1-{x['mode']}-13");frame=(d/'heading-45.idx').read_bytes();prev=(ROOT/Path(x['capture'])/'heading-0.idx').read_bytes();rows=[dict(**p,previous_frame=prev[p['y']*x['width']+p['x']],sentinel_result=frame[p['y']*x['width']+p['x']]) for p in x['differences']];out.append(dict(mode=x['mode'],pixels=rows));print(out[-1]);assert all(p['sentinel_result']==251 and p['previous_frame']==p['expected'] for p in rows)
(ROOT/'research/evidence/source-port/sp07-render/framebuffer-history.json').write_text(json.dumps(out,indent=2))
