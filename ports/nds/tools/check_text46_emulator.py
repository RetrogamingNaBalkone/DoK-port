"""Original word preservation, all story/intro pages and physical B-to-menu."""
import json
import sys
from pathlib import Path
import run_frontend_emulator as front
from frontend_pixel_oracle import prepared_text, text_pages


def main():
    target=Path(sys.argv[1]).resolve();directory=target/'text46';directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    front.write_fixture(f.save/'melonDS DS/dldi_sd_card.bin',None,embedded=True)
    report=dict(passed=False,texts=[],cancels=[])
    names=('README','BINTRO1','EINTRO1','BINTRO2','EINTRO2','BINTRO3','EINTRO3')
    for language in (1,2):
        for index,name in enumerate(names):
            raw=f.art.original_text(language,name);prepared=prepared_text(raw,index)
            assert raw.split()==prepared.split(),(language,name,'Changed original words')
            assert b'\n\n\n' not in prepared
            pages=text_pages(prepared,30,11,prose=True)
            assert all(not line.startswith(b' ') for page in pages for line in page),(language,name,'Indented wrap')
            if index==0:assert len(pages)==2
            report['texts'].append(dict(language=language,name=name,pages=len(pages),
                                       paragraphs=prepared.decode('cp866').split('\n\n')))
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1)
        f.tap(front.B);f.wait_for(2)
        for language in (1,2):
            f.choose(2,1);f.tap(front.A);f.wait_for(4);f.choose(4,language-1)
            f.tap(front.A);f.wait_for(2,language=language)
            f.choose(2,3);f.tap(front.A);f.wait_for(6,page=0)
            assert f.probe[8]==2
            f.tap(front.A);f.wait_for(6,page=1)
            f.tap(front.B);f.wait_for(6,page=0)
            f.tap(front.B);f.wait_for(2,selection=3)
            for level in (1,2,3):
                f.choose(2,0);f.tap(front.A);f.wait_for(3);f.choose(3,level-1)
                f.tap(front.A);f.wait_for(7,page=0)
                count=f.probe[8]
                for page in range(1,count):f.tap(front.A);f.wait_for(7,page=page)
                for page in reversed(range(count-1)):f.tap(front.B);f.wait_for(7,page=page)
                f.tap(front.B);f.wait_for(2,selection=0)
                # Re-enter to check cancellation on page zero independently.
                f.tap(front.A);f.wait_for(3);f.choose(3,level-1);f.tap(front.A);f.wait_for(7,page=0)
                f.tap(front.B);f.wait_for(2,selection=0)
                report['cancels'].append(dict(language=language,level=level,pages=count))
        report.update(passed=True,index8_checks=f.report['index8_checks'],rgb_checks=f.report['rgb_checks'],
                      screens=f.report['rows'],rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    finally:f.close();front.transport.flush_native_stdio()
    (directory/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('texts','screens')}))


if __name__=='__main__':main()
