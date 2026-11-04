"""Full normal-input routes followed by actual ending/selection/new-game UI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--profiles',nargs='+',default=['ru1','en1','og'])
    parser.add_argument('--exe',default='build/sdl2/windows/dok_port.exe')
    parser.add_argument('--renderers',nargs='+',choices=['gl','software'],default=['gl','software'])
    parser.add_argument('--report-name',default='completed-session-report.json')
    args=parser.parse_args()
    base=Path('research/evidence/source-port/sp16-flow')
    routes={row['name']:row for row in json.loads((base/'routes/successful-route-report.json').read_text(encoding='utf-8'))['routes']}
    results=[]
    environment=dict(os.environ,SDL_AUDIODRIVER='dummy')
    for profile in args.profiles:
        route=routes[profile]
        frames=int(re.search(r'frames=(\d+)',route['endpoint'])[1])
        replay=base/'routes'/f'{profile}.replay'
        level='LEVEL4' if profile=='og' else 'LEVEL1'
        expected_pose=re.search(r'pose=(\S+)',route['endpoint'])[1]
        for renderer in args.renderers:
            for locale in ('ru','en'):
                folder=base/'app'/f'completed-{profile}-{renderer}-{locale}'
                folder.mkdir(parents=True,exist_ok=True)
                script=folder/'events.script'
                rows=[f'{frames+1} state completed',f'{frames+2} key Return',
                      f'{frames+4} state level-selection',f'{frames+6} key Return',
                      f'{frames+8} state new-game']
                for row in tuple(rows):
                    frame,action,label=row.split(' ',2)
                    if action=='state':rows.append(f'{frame} shot {folder}/{label}.png')
                script.write_text('\n'.join(rows)+'\n',encoding='ascii')
                command=[args.exe,'--res',route['resource'],'--level',level,'--skip-intro',
                         '--input-replay',str(replay),'--frames',str(frames+12),'--ui-script',str(script),
                         '--user-dir',str(folder),'--size','800x450','--renderer',renderer,'--locale',locale]
                before=sha(route['resource'])
                run=subprocess.run(command,env=environment,capture_output=True,text=True,timeout=180)
                log=(folder/'port.log').read_text(encoding='utf-8')
                if run.returncode or 'Audio driver: dummy' not in log:raise RuntimeError(run.stderr+'\n'+log[-2000:])
                states={label:dict(re.findall(r'(\w+)=(\S+)',data)) for label,data in re.findall(r'^STATE (\S+) (.*)$',log,re.M)}
                complete=states['completed']
                assert complete['menu']=='24' and complete['phase']=='3' and complete['hp']=='-1'
                assert complete['pose']==expected_pose,(profile,complete['pose'],expected_pose)
                assert complete['keys']=='7'
                if profile=='og':assert complete['score']=='105'
                selection=states['level-selection']
                assert selection['menu']=='5' and 'world' not in selection
                fresh=states['new-game']
                assert fresh['menu']=='0' and fresh['phase']=='0' and fresh['lives']=='3'
                assert fresh['keys']=='0' and fresh['score']=='0' and int(fresh['hp'])>0
                assert int(fresh['ticks'])<2000
                assert sha(route['resource'])==before
                results.append(dict(profile=profile,renderer=renderer,locale=locale,audio_driver='dummy',
                                    command=command,frames=frames+12,states=states,
                                    replay_sha256=sha(replay),resource_sha256=before,
                                    executable_sha256=sha(args.exe)))
                (base/'app'/args.report_name).write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
                print(f'PASS completed -> ending -> selection -> new game {profile} {renderer} {locale}',flush=True)


if __name__=='__main__':
    main()
