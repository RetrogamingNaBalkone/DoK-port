"""Small actual-ROM read-only AI sampling smoke; not a level playthrough."""
import json,argparse
from pathlib import Path
import traceback
from check_host import ROOT,pin
from read_live_ai import LiveAiReader
from run_routes import RouteObserver,front,render,transport,UP
from plan_routes import Level
from frontend_fat_oracle import volume_files,audit_changes

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--language',type=int,choices=(1,2),default=1);args=parser.parse_args()
    suffix='ru'if args.language==1 else'en'
    target=ROOT/f'build/nds/n22/candidate-partial01-{suffix}'
    directory=ROOT/f'build/nds/n22/candidate01-ai-sampler-smoke-{suffix}';directory.mkdir(parents=True,exist_ok=True)
    evidence=ROOT/'research/evidence/nds/n22/ai-readonly';evidence.mkdir(parents=True,exist_ok=True)
    f=None;report=dict(task='N22',passed=False,scope='Actual candidate01 real-input boot plus read-only model-RAM consistency checks, not playthrough')
    try:
        f=RouteObserver(front.CORE,target,directory);report['rom']=pin(target/'dok_nds_gameplay.nds')
        reader=LiveAiReader(target/'dok_nds_gameplay.elf')
        image=f.save/'melonDS DS/dldi_sd_card.bin';front.write_fixture(image,front.settings_bytes(args.language))
        f.load(target/'dok_nds_gameplay.nds');transport.flush_native_stdio();before=volume_files(image)
        f.start_level(1);data=Level(1);samples=[];reasons={}
        for step in range(80):
            maps=f.map_snapshot(data)
            sample=reader.sample(lambda address,size:render.ram(f,address,size),mmap=maps[0]if maps else None)
            if sample['accepted']:
                samples.append(dict(video_frame=f.frames,**sample))
            else:reasons[sample['reason']]=reasons.get(sample['reason'],0)+1
            f.hold([UP]if step<40 else [],1)
        assert samples,'No consistent read-only sample'
        report.update(passed=True,accepted=len(samples),refused=reasons,samples=samples,
                      language=args.language,elf=pin(target/'dok_nds_gameplay.elf'),layout=pin(ROOT/'research/evidence/nds/n22'/('candidate01-ai-layout.json'if args.language==1 else'candidate01-en-ai-layout.json')),
                      inputs=[pin(Path(__file__).resolve()),pin(ROOT/'ports/nds/tools/flow/read_live_ai.py'),pin(ROOT/'ports/nds/tools/flow/inspect_ai_layout.py')])
    except Exception:report['error']=traceback.format_exc()
    finally:
        if f:
            f.close();transport.flush_native_stdio()
            if 'before'in locals():report['disk']=audit_changes(before,volume_files(image),['/DOK/SETTINGS.DAT'])
        (evidence/('smoke.json'if args.language==1 else'smoke-en.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items()if k in('passed','accepted','refused','error')}))
    return 0 if report['passed']else 1
if __name__=='__main__':raise SystemExit(main())
