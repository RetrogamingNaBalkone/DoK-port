"""Real application: three natural deaths, retry, Game Over, new game and pause."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    base=Path('research/evidence/source-port/sp16-flow')
    results=[]
    environment=dict(os.environ,SDL_AUDIODRIVER='dummy')
    for profile,resource,level in [('ru','originals/pkr/RUS/PKR.RES','LEVEL1'),('en','originals/pkr/ENG/PKE.RES','LEVEL1'),('og','originals/Old_Gold_3D/OLD_GOLD.RES','LEVEL4')]:
        replay=base/'routes'/f'session-{profile}.replay'
        source=Path(str(replay)+'.script')
        pilot=subprocess.run(['build/sdl2/windows/dok_walkthrough_probe.exe',resource,level,str(replay),'session'],capture_output=True,text=True,timeout=180,env=environment)
        if pilot.returncode:
            raise RuntimeError(pilot.stderr)
        (base/'routes'/f'session-{profile}.log').write_text(pilot.stdout,encoding='utf-8')
        frames=int(re.search(r'SESSION frames=(\d+)',pilot.stdout)[1])
        for renderer in ('gl','software'):
            for locale in ('ru','en'):
                folder=base/'app'/f'session-{profile}-{renderer}-{locale}'
                folder.mkdir(parents=True,exist_ok=True)
                lines=source.read_text().splitlines()
                for line in tuple(lines):
                    frame,action,value=line.split(' ',2)
                    if action=='state':
                        lines.append(f'{frame} shot {folder}/{value}.png')
                script=folder/'events.script'
                script.write_text('\n'.join(lines)+'\n',encoding='ascii')
                command=['build/sdl2/windows/OGDoK.exe','--res',resource,'--level',level,'--skip-intro','--input-replay',str(replay),'--frames',str(frames),'--ui-script',str(script),'--renderer',renderer,'--locale',locale,'--size','800x450','--verbose', '--user-dir',str(folder)]
                before=sha(resource)
                run=subprocess.run(command,capture_output=True,text=True,env=environment,timeout=180)
                log=(folder/'port.log').read_text(encoding='utf-8')
                if run.returncode or 'Audio driver: dummy' not in log:
                    raise RuntimeError(f'{profile}/{renderer}/{locale}: {run.stderr}\n{log}')
                states={}
                for label,data in re.findall(r'^STATE (\S+) (.*)$',log,re.M):
                    states[label]=dict(re.findall(r'(\w+)=(\S+)',data))
                for life in range(3):
                    death=states[f'death-{life}']
                    assert death['hp']=='0' and death['lives']==str(2-life)
                    assert death['phase']==('2' if life==2 else '1')
                    after=states[f'after-confirm-{life}']
                    if life<2:
                        assert after['phase']=='0' and int(after['hp'])>0
                        assert after['world']==death['world']
                        assert after['lives']==death['lives']
                    else:
                        assert after['menu']=='5' and 'world' not in after
                fresh=states['new-game']
                assert states['main-menu']['menu']=='1'
                assert states['title']['menu']=='3'
                assert states['intro']['menu']==('5' if profile=='og' else '4')
                assert fresh['phase']=='0' and fresh['lives']=='3' and int(fresh['hp'])>0
                pause_start,pause_end=states['paused-start'],states['paused-end']
                for key in ('ticks','pose','hp','charge','maps','lives','keys','score'):
                    assert pause_start[key]==pause_end[key],(profile,key)
                assert pause_start['menu']==pause_end['menu']=='18'
                assert states['resumed']['menu']=='0'
                assert sha(resource)==before
                row=dict(profile=profile,renderer=renderer,locale=locale,audio_driver='dummy',frames=frames,
                         command=command,replay_sha256=sha(replay),resource_sha256=before,
                         executable_sha256=sha('build/sdl2/windows/OGDoK.exe'),states=states)
                results.append(row)
                (base/'app/session-report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
                print(f'PASS session {profile} {renderer} {locale}: {frames} frames',flush=True)


if __name__=='__main__':
    main()
