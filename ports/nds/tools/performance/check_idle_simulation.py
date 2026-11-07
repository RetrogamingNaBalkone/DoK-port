"""Read-only no-input soak on a pinned production ROM; no emulated RAM writes."""
import json,struct,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front
class Idle(front.FrontendObserver):
    def check_pixels(self,p):pass # Baseline has its frozen frontend layout.
def main():
    target=Path(sys.argv[1]).resolve();frames=int(sys.argv[2]) if len(sys.argv)>2 else 36000
    directory=target/'idle-simulation';directory.mkdir(exist_ok=True)
    f=Idle(front.CORE,target,directory);f.options['melonds_homebrew_sdcard']='disabled'
    report=dict(passed=False,rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'),requested_frames=frames,no_input=True)
    started=time.monotonic()
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tap(front.A);f.wait_for(2);f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8)
        baseline=f.frames
        for frame in range(frames):
            f.tick()
            if frame%600==0:
                report.update(completed_frames=frame+1,wall_seconds=time.monotonic()-started,frontend=list(f.probe))
                (directory/'progress.json').write_text(json.dumps(report),'utf8')
        report.update(passed=True,completed_frames=f.frames-baseline,last_renderer=f.report['last_renderer'])
    except Exception as error:
        report['error']=str(error);report['captures']=f.capture('failure')
        for name in ('crash_diagnostic','failure_line'):
            address,size=front.transport.elf_symbol(target/'dok_nds_gameplay.elf',name)
            report[name]=front.render.ram(f,address,size).hex()
        raise
    finally:
        report.update(wall_seconds=time.monotonic()-started,last_renderer=f.report.get('last_renderer'))
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
        f.close();front.transport.flush_native_stdio()
if __name__=='__main__':main()
