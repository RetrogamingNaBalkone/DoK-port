"""Check N23 UI from original RES/EXE texts, including every key/weapon mask."""
import ctypes as C
import json
from pathlib import Path
import sys
from check_hud_host import build, Driver, U8, ROOT
from hud_pixel_oracle import OriginalHudArt, render_hud, message_pages
sys.path.insert(0,str(ROOT/'ports/nds/tools/ui'))
from prepare_playtest_ui import prepare

def main():
    revised=prepare()
    build()
    driver,art=Driver(),OriginalHudArt()
    blob=(revised/'ui.dat').read_bytes()
    driver.banks=[(U8*8192).from_buffer_copy(blob[32+i*8192:32+(i+1)*8192]) for i in range(2)]
    catalog=json.loads((ROOT/'research/evidence/nds/n19/ui-catalog.json').read_text('utf8'))
    base=dict(language=1,level=1,health=100,charge=150,max_health=100,max_charge=200,lives=3,score=0,keys=7,weapon=6)
    for entry in catalog['messages']:
        message=bytes.fromhex(entry['cp866_hex'])
        assert len(message_pages(message,30))<=2
        state=dict(base,language=entry['language'])
        assert driver.hud(state,message)==render_hud(art,state,message)[0]
    for language in (1,2):
        for keys in range(8):
            for weapon in (0,2,4,6):
                for value in (-32768,-1,0,9,100,32767):
                    state=dict(base,language=language,keys=keys,weapon=weapon,health=value,charge=value)
                    expected=render_hud(art,state)[0]
                    assert driver.hud(state)==expected
                    assert driver.hud(dict(state,lives=-32768,score=32767,max_health=-1,max_charge=0))==expected
    from run_diag_emulator import png
    for language in (1,2):
        longest=max((e for e in catalog['messages'] if e['language']==language),key=lambda e:e['bytes'])
        for message in (b'TEXT',bytes.fromhex(longest['cp866_hex'])):
            centered=driver.hud(dict(base,language=language),message)
            ys=[y for y in range(1,20) for x in range(8,248) if centered[y*256+x]==2]
            assert ys and abs(min(ys)+max(ys)-20)<=1,'Message ink is not vertically centered'
            for x0 in (23,97):
                ys=[y for y in range(21,38) for x in range(x0,x0+48) if centered[y*256+x]==2]
                assert ys and abs(min(ys)+max(ys)-58)<=1,'Numeric ink is not vertically centered'
        pixels=driver.hud(dict(base,language=language),bytes.fromhex(longest['cp866_hex']))
        palette=art.palette(language,1)
        rgb=bytes(((palette[index]>>shift)&31)*255//31 for index in pixels for shift in (0,5,10))
        png(ROOT/f'research/evidence/nds/n23/playtest-hud-{language}.png',256,38,rgb)
    report=dict(passed=True,guarded_rasters=driver.draws,original_messages=len(catalog['messages']),
                max_message_bytes=max(e['bytes'] for e in catalog['messages']),message_columns=30,message_rows=2)
    out=ROOT/'research/evidence/nds/n23/playtest-ui-check.json'
    out.write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))

if __name__=='__main__':main()
