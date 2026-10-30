"""N20 focused original automap second-pass state and wrapper extension check."""
from pathlib import Path
import hashlib,json,random,sys
from check_items import ROOT,artifact
from check_map_raster import from_model
from map.original_map import original_map_result,original_map

def main():
    rows=[]
    for lang in ('ru','en'):
        for level in (1,2,3):
            path=ROOT/f'research/evidence/runtime/{lang}/LEVEL{level}-runtime.json'
            for mode in ('zero','pattern'):
                state=from_model(json.loads(path.read_text()))
                state['visited']=bytes(4096) if mode=='zero' else bytes(i%7==0 for i in range(4096))
                pixel,post=original_map_result(lang,state,3,-32,16)
                assert pixel==original_map(lang,state,3,-32,16)
                classes={o['glyph']:o['class_id'] for o in state['objects']}
                expected=bytes(int(bool(v) or classes[glyph]==1) for v,glyph in zip(state['visited'],state['maps'][0],strict=True))
                assert post==expected
                rows.append(dict(language=lang,level=level,mode=mode,
                    pixels_sha256=hashlib.sha256(pixel).hexdigest(),visited_sha256=hashlib.sha256(post).hexdigest(),
                    hero_writes=sum(a!=b for a,b in zip(state['visited'],post,strict=True))))
    old=ROOT/'research/evidence/nds/n20/raster-tested-helper.py'
    new=ROOT/'ports/nds/tools/map/original_map.py'
    assert new.read_bytes().startswith(old.read_bytes())
    report=dict(task='N20',passed=True,checks=len(rows),scope='Actual native visited state after map second pass, existing raster helper prefix unchanged; separate focused extension check, not a repeated954-case raster gate',
        historical_helper=artifact(old),current_helper=artifact(new),checker=artifact(Path(__file__)),rows=rows)
    out=ROOT/'research/evidence/nds/n20/postvisited-check.json';out.write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,checks=len(rows))))
if __name__=='__main__':main()
