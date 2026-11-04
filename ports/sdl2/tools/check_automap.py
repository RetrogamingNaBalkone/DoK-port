"""Actual hidden application map toggles/live input and native presentation."""
from pathlib import Path
import json,os,re,subprocess
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp15-map/process'
PROFILES=[('og','old_gold','originals/Old_Gold_3D/OLD_GOLD.RES','LEVEL4'),('ru','kremlin_pkr','originals/pkr/RUS/PKR.RES','LEVEL1'),('en','kremlin_pke','originals/pkr/ENG/PKE.RES','LEVEL1')]
def main():
    results=[]
    for profile,dataset,res,level in PROFILES:
        for backend in ('gl','software'):
            for size in ('1280x720','960x720'):
                out=OUT/f'{profile}-{backend}-{size}';out.mkdir(parents=True,exist_ok=True)
                replay=out/'clock.txt';replay.write_text(f'DOK_INPUT_REPLAY 1 {dataset} {level} 1\n416 0 0 0 0 0 0 0 0 -1 0 0\n')
                script=out/'script.txt';script.write_text('0 keydown Tab\n1 state held\n1 keyup Tab\n2 state open\n'
                    f'2 shot {out/"map-start.png"}\n3 keydown W\n13 keyup W\n14 state forward\n15 keydown D\n25 keyup D\n26 state right\n'
                    f'26 shot {out/"map-live.png"}\n27 key Tab\n28 state closed\n29 shot {out/"closed.png"}\n')
                command=[str(ROOT/'build/sdl2/windows/dok_port.exe'),'--hidden','--res',str(ROOT/res),'--level',level,'--skip-intro','--renderer',backend,'--hud','desktop','--size',size,'--user-dir',str(out/'user'),'--ui-script',str(script),'--frames','30','--input-replay',str(replay),'--replay-live-input']
                result=subprocess.run(command,cwd=ROOT,env=dict(os.environ,SDL_AUDIODRIVER='dummy'),capture_output=True,text=True,timeout=60);log=(out/'user/port.log').read_text(encoding='utf-8');assert result.returncode==0,(command,result.stdout,result.stderr,log)
                states={m[1]:dict(re.findall(r'(\w+)=([^\s]*)',m[2])) for m in re.finditer(r'^STATE (\w+) (.*)$',log,re.M)}
                assert states['held']['menu']=='0',states
                assert states['open']['menu']=='27' and states['forward']['menu']=='27' and states['right']['menu']=='27',states
                assert states['closed']['menu']=='0',states
                assert int(states['right']['ticks'])>int(states['open']['ticks']),states
                assert states['forward']['pose']!=states['open']['pose'] or states['right']['pose']!=states['open']['pose'],states
                assert 'Audio driver: dummy' in log
                results.append(dict(profile=profile,backend=backend,size=size,ticks_open=states['open']['ticks'],ticks_live=states['right']['ticks'],moved=True,release_toggle=True,closed=True))
    (OUT/'report.json').write_text(json.dumps(dict(processes=results,clock='fixed diagnostic elapsed; raw SdlInput; ordinary world callbacks',audio='dummy',windows='hidden'),indent=2)+'\n');print('PASS 12 actual maps: release toggle, live time/movement, GL/software and 4:3/16:9')
if __name__=='__main__':main()
