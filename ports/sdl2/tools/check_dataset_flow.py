"""Silent hidden real-app transitions PKR -> Old Gold -> PKE through menus."""
import json
import os
from pathlib import Path
import re
import subprocess


base=Path('research/evidence/source-port/sp16-flow')
results=[]
replay=base/'app/dataset-prefix.replay'
replay.write_text('\n'.join((base/'routes/ru1.replay').read_text().splitlines()[:2])+'\n')
script_lines=[]
for frame,label in [(10,'pkr'),(30,'og')]:
    script_lines += [f'{frame} key Escape']+[f'{frame+1} key Down']*4+[
        f'{frame+2} key Return',f'{frame+3} key Up',f'{frame+4} key Return',
        f'{frame+5} state cleared-{label}',f'{frame+6} key Return']
script_lines += ['17 state datasets','18 key Return','19 state title-og','20 key Return',
                 '21 state intro-og','22 key Return','24 state world-og',
                 '37 key Down','37 key Down','38 key Return','39 state title-pke',
                 '40 key Return','41 state intro-pke']
script_lines += [f'{frame} key Return' for frame in range(42,58,2)]
script_lines += ['60 state world-pke']
for renderer in ('gl','software'):
    for locale in ('ru','en'):
        folder=base/'app'/f'datasets-{renderer}-{locale}'
        folder.mkdir(parents=True,exist_ok=True)
        lines=list(script_lines)
        for row in tuple(lines):
            frame,action,label=row.split(' ',2)
            if action=='state':lines.append(f'{frame} shot {folder}/{label}.png')
        script=folder/'events.script'
        script.write_text('\n'.join(lines)+'\n',encoding='ascii')
        command=['build/sdl2/windows/OGDoK.exe','--res','originals/pkr/RUS/PKR.RES',
                 '--res','originals/Old_Gold_3D/OLD_GOLD.RES','--res','originals/pkr/ENG/PKE.RES',
                 '--level','LEVEL1','--skip-intro','--input-replay',str(replay),'--frames','64',
                 '--ui-script',str(script),'--user-dir',str(folder),'--size','800x450',
                 '--renderer',renderer,'--locale',locale]
        run=subprocess.run(command,env=dict(os.environ,SDL_AUDIODRIVER='dummy'),capture_output=True,text=True,timeout=60)
        log=(folder/'port.log').read_text(encoding='utf-8')
        if run.returncode or 'Audio driver: dummy' not in log:raise RuntimeError(run.stderr+'\n'+log)
        states={label:dict(re.findall(r'(\w+)=(\S+)',data)) for label,data in re.findall(r'^STATE (\S+) (.*)$',log,re.M)}
        for name in ('cleared-pkr','cleared-og'):assert states[name]['menu']=='1' and 'world' not in states[name]
        assert states['datasets']['menu']=='2'
        assert states['title-og']['menu']==states['title-pke']['menu']=='3'
        assert states['intro-og']['menu']=='5' and states['intro-pke']['menu']=='4'
        for name in ('world-og','world-pke'):assert states[name]['menu']=='0' and states[name]['lives']=='3'
        assert 'Started Old Gold LEVEL4' in log and 'Started Kremlin PKE LEVEL1' in log
        results.append(dict(renderer=renderer,locale=locale,audio_driver='dummy',command=command,states=states))
        (base/'app/dataset-report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
        print(f'PASS PKR -> OG -> PKE {renderer} {locale}',flush=True)
