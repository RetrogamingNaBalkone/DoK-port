"""Both LCDs, RU/EN control previews, confirmation and cancellation via real keys."""
import json
import sys
from pathlib import Path
import run_frontend_emulator as front


def main():
    target=Path(sys.argv[1]).resolve();directory=target/'controls43';directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    front.write_fixture(f.save/'melonDS DS/dldi_sd_card.bin',None,embedded=True)
    report=dict(passed=False,cases=[])
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        current=0
        for language in (1,2):
            f.choose(2,1);f.tap(front.A);f.wait_for(4);f.choose(4,language-1)
            f.tap(front.A);f.wait_for(2,language=language)
            for scheme in (0,1):
                f.choose(2,2);f.tap(front.A);f.wait_for(5,selection=current)
                f.choose(5,scheme);f.wait_for(5,selection=scheme)
                captures=f.capture(f'controls-{language}-{scheme}')
                writes=f.probe[28]
                f.tap(front.B);f.wait_for(2,selection=2)
                assert f.probe[28]==writes,'Preview/cancel wrote settings'
                f.tap(front.A);f.wait_for(5,selection=current)
                f.choose(5,scheme);f.tap(front.A);f.wait_for(2,selection=2)
                assert f.probe[28]==writes+(scheme!=current)
                current=scheme
                f.tap(front.A);f.wait_for(5,selection=current)
                f.tap(front.B);f.wait_for(2)
                report['cases'].append(dict(language=language,scheme=scheme,captures=captures))
        report.update(passed=True,index8_checks=f.report['index8_checks'],rgb_checks=f.report['rgb_checks'],
                      rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    finally:f.close();front.transport.flush_native_stdio()
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k!='cases'}))


if __name__=='__main__':main()
