"""Dual-display credits, physical menu input, LCD pixels and independent QR decode."""
import json
import shutil
import subprocess
import sys
from pathlib import Path
import run_frontend_emulator as front


def main():
    target=Path(sys.argv[1]).resolve();directory=target/'credits42';directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    image=f.save/'melonDS DS/dldi_sd_card.bin'
    front.write_fixture(image,None,embedded=True)
    report=dict(passed=False,languages=[])
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2)
        for language in (1,2):
            f.choose(2,1);f.tap(front.A);f.wait_for(4);f.choose(4,language-1)
            f.tap(front.A);f.wait_for(2,language=language)
            f.choose(2,4);f.tap(front.A);f.wait_for(11,page=0)
            assert f.probe[8]==1
            f.tap(front.B);f.wait_for(2,selection=4)
            f.tap(front.A);f.wait_for(11,page=0)
            # Decode the real lower LCD callback, including its white quiet zone.
            width,height,rgb=f.rgb();assert (width,height)==(256,384)
            rgba=bytearray()
            for at in range(49152*3,len(rgb),3):rgba.extend((*rgb[at:at+3],255))
            frame=directory/f'channel-{language}.rgba';frame.write_bytes(rgba)
            decoder=Path(__file__).with_name('decode_channel_qr.cjs')
            decoded=json.loads(subprocess.check_output([shutil.which('node'),str(decoder),str(frame)],text=True))
            assert decoded==dict(data='https://t.me/RetroGamingNaBalkone',version=3)
            f.tap(front.A);f.wait_for(2,selection=4)
            report['languages'].append(dict(language=language,pages=1,displays=2,decoded=decoded))
        # Starting a level after closing credits proves that they leave the UI usable.
        f.choose(2,0);f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8)
        report.update(passed=True,index8_checks=f.report['index8_checks'],rgb_checks=f.report['rgb_checks'],
                      screens=f.report['rows'],rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    finally:f.close();front.transport.flush_native_stdio()
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:v for k,v in report.items() if k!='screens'}))


if __name__=='__main__':main()
