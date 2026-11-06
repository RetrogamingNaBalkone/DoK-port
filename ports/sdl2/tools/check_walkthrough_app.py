"""Render recorded normal-input full routes in the real hidden SDL application."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--routes', nargs='+', default=['ru1','ru2','ru3','en1','en2','en3','og'])
    parser.add_argument('--resume', action='store_true')
    args = parser.parse_args()
    base = Path('research/evidence/source-port/sp16-flow')
    output = base / 'app'
    results = json.loads((output/'matrix-report.json').read_text(encoding='utf-8')) if args.resume and (output/'matrix-report.json').exists() else []
    environment = dict(os.environ,SDL_AUDIODRIVER='dummy')
    executable = Path('build/sdl2/windows/OGDoK.exe').resolve()
    for route in args.routes:
        resource = Path('originals/Old_Gold_3D/OLD_GOLD.RES' if route=='og' else 'originals/pkr/RUS/PKR.RES' if route.startswith('ru') else 'originals/pkr/ENG/PKE.RES')
        replay = base / 'routes' / f'{route}.replay'
        expected = {}
        for renderer in ('gl','software'):
            for locale in ('ru','en'):
                previous = next((row for row in results if (row['route'],row['renderer'],row['locale'])==(route,renderer,locale)),None)
                if previous:
                    if sha(replay)!=previous['replay_sha256'] or sha(resource)!=previous['resource_sha256']:
                        raise RuntimeError(f'{route}: recorded input/source changed since completed replay')
                    if sha(previous['screenshot'])!=previous['screenshot_sha256']:
                        raise RuntimeError(f'{route}: completed screenshot evidence changed')
                    expected = {'state':previous['state']}
                    continue
                folder = output / f'{route}-{renderer}-{locale}-hidden'
                folder.mkdir(parents=True, exist_ok=True)
                command = [str(executable),'--res',str(resource),'--level','LEVEL4' if route=='og' else f'LEVEL{route[-1]}','--skip-intro',
                           '--input-replay',str(replay),'--renderer',renderer,'--locale',locale,'--hud','desktop',
                           '--size','800x450','--user-dir',str(folder),'--screenshot',str(folder/'final.png')]
                before = sha(resource)
                start = time.monotonic()
                run = subprocess.run(command, capture_output=True, text=True, timeout=180,env=environment)
                log = (folder/'port.log').read_text(encoding='utf-8')
                if 'Audio driver: dummy' not in log:
                    raise RuntimeError('Diagnostic replay did not select silent dummy audio')
                state = re.search(r'^REPLAY (.*)$',log,re.M)
                shutdown = re.search(r'Clean shutdown; frames=(\d+) datasets=(\d+) menu=(\d+)',log)
                if run.returncode or not state or not shutdown or shutdown[3] not in ('24','25'):
                    raise RuntimeError(f'{route}/{renderer}/{locale}: replay failed\n{log[-1500:]}')
                if expected and state[1] != expected['state']:
                    raise RuntimeError(f'{route}: simulation endpoint differs by renderer/locale')
                expected = {'state':state[1]}
                assert sha(resource)==before
                entry = dict(route=route,renderer=renderer,locale=locale,command=command,audio_driver='dummy',
                             seconds=round(time.monotonic()-start,2),frames=int(shutdown[1]),
                             final_menu=int(shutdown[3]),state=state[1],replay_sha256=sha(replay),
                             resource_sha256=before,executable_sha256=sha(executable),
                             screenshot=str(folder/'final.png'),screenshot_sha256=sha(folder/'final.png'))
                results.append(entry)
                (output/'matrix-report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
                print(f'PASS {route} {renderer} {locale}: {shutdown[1]} frames, {entry["seconds"]}s',flush=True)


if __name__ == '__main__':
    main()
