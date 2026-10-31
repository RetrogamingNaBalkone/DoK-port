"""Production-ROM title waveform check, without frontend fixture hooks."""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();directory=target/'title-audio';directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    f.options['melonds_homebrew_sdcard']='disabled'
    report=f.report
    try:
        f.load(target/'dok_nds_gameplay.nds')
        f.wait_for(1);f.wait_audio_complete()
        a=report['audio'][0];w=a['waveform']
        assert a['complete'] and w['complete_source_covered'] and w['whole_coefficient']>.99
        report['passed']=True
    finally:
        f.close();front.transport.flush_native_stdio()
        report['check_source_sha256']=front.transport.sha(Path(__file__))
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=report['passed'],whole_coefficient=w['whole_coefficient'])))

if __name__=='__main__':main()
