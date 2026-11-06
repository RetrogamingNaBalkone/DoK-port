"""SP-FILTERS: actual GL/software output and persistent save-disable guards."""
import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import subprocess
from check_video import Image
from compiler_cache import ROOT
from experiment_cleanup import temporary_workspace

SAMPLES = [('old_gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', [4]),
           ('pkr', 'originals/pkr/RUS/PKR.RES', [1, 2, 3]),
           ('pke', 'originals/pkr/ENG/PKE.RES', [1, 2, 3])]
FILTERS = ['nearest', 'linear', 'mipmaps', 'trilinear', 'aniso2', 'aniso4', 'aniso8']

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=ROOT/'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--out', type=Path, default=ROOT/'research/evidence/source-port/sp-filters')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    checks = []
    hashes = {res: hashlib.sha256((ROOT/res).read_bytes()).hexdigest() for _, res, _ in SAMPLES}
    with temporary_workspace(args.out.resolve(), 'run-') as work:
        def run(name, profile, res, level, backend='gl', filter='nearest', script='', size='800x600', locale='ru', frames=4, fxaa='off'):
            profile.mkdir(parents=True, exist_ok=True)
            ini = profile/'settings.ini'
            if not ini.exists():
                ini.write_text('[settings]\nversion=1\n[interface]\nfps=0\n', encoding='utf-8')
            events = work/(name+'.txt'); events.write_text(script, encoding='utf-8')
            shot = work/(name+'.png')
            command = [str(args.app.resolve()), '--hidden', '--user-dir', str(profile), '--res', str(ROOT/res),
                       '--renderer', backend, '--level', 'LEVEL'+str(level), '--skip-intro', '--size', size,
                       '--software-mode', '320x200', '--locale', locale, '--frames', str(frames),
                       '--fxaa', fxaa, '--hud', 'classic', '--ui-script', str(events), '--screenshot', str(shot)]
            command += ['--filter' if backend == 'gl' else '--frame-filter', filter]
            proc = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                  env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=90)
            log = (profile/'port.log').read_text(encoding='utf-8')
            assert proc.returncode == 0, (name, proc.stdout, proc.stderr, log)
            assert 'SAVE load failed:' not in log and 'SAVE write failed:' not in log, (name, log)
            frame = Image.open(shot)
            checks.append(dict(name=name, command=command, png_sha256=hashlib.sha256(shot.read_bytes()).hexdigest(),
                               events=[line for line in log.splitlines() if line.startswith(('STATE ', 'SAVE '))]))
            return frame, log, shot

        for dataset, res, levels in SAMPLES:
            for level in levels:
                for backend, filters in [('gl', FILTERS), ('software', ['nearest', 'linear', 'sharp', 'xbr'])]:
                    reference = None
                    for filter in filters:
                        name=f'{dataset}-{level}-{backend}-{filter}'
                        frame, _, shot = run(name, work/'images', res, level, backend, filter)
                        panel = frame.crop((0, 510 if dataset=='old_gold' else 480, 800, 600)).data
                        if reference is None: reference = panel
                        assert panel == reference, (name, 'Filter modified HUD/weapon panel')
                        if level==levels[0] and filter in ('trilinear', 'aniso8', 'xbr'):
                            (args.out/(name+'.png')).write_bytes(shot.read_bytes())
                # Fractional resize and FXAA combinations, plus actual turning.
                for filter in ('trilinear', 'aniso8'):
                    run(f'{dataset}-{level}-{filter}-fxaa', work/'images', res, level, filter=filter,
                        size='853x641', fxaa='on', frames=24, script='1 keydown Right\n20 keyup Right\n')
            for backend in ('gl', 'software'):
                profile=work/(dataset+'-'+backend)
                run(dataset+'-'+backend+'-create',profile,res,levels[0],backend,script='1 key F5\n',frames=4)
                save=profile/'saves'/dataset/'quick.sav'; saved=save.read_bytes()
                toggle=('1 key Escape\n2 key Down\n3 key Return\n4 key Down\n5 key Down\n6 key Down\n'
                        '7 key Return\n8 key Down\n9 key Right\n10 state saving_switch\n'
                        '11 key Escape\n12 key Escape\n13 key Escape\n14 key F5\n16 key F9\n18 state after\n')
                _, log, _ = run(dataset+'-'+backend+'-disabled',profile,res,levels[0],backend,script=toggle,frames=20)
                assert 'SAVE written' not in log and 'SAVE loaded' not in log
                config=configparser.ConfigParser();config.read(profile/'settings.ini')
                assert config['gameplay']['saves_enabled']=='0' and save.read_bytes()==saved
                _, log, _ = run(dataset+'-'+backend+'-restart-disabled',profile,res,levels[0],backend,script='1 key F5\n3 key F9\n',frames=6)
                assert 'SAVE written' not in log and 'SAVE loaded' not in log and save.read_bytes()==saved
                _, log, _ = run(dataset+'-'+backend+'-enabled',profile,res,levels[0],backend,script=toggle,frames=20)
                assert 'SAVE written' in log and 'SAVE loaded' in log
            run(dataset+'-xbr-turn',work/'images',res,levels[0],backend='software',filter='xbr',size='853x641',frames=24,script='1 keydown Right\n20 keyup Right\n')
            for locale in ('ru','en'):
                script='1 key Escape\n2 key Down\n3 key Return\n4 key Down\n5 key Down\n6 key Down\n7 key Return\n8 key Down\n'
                _,_,shot=run(dataset+'-'+locale+'-gameplay',work/'menu',res,levels[0],script=script,size='320x200',locale=locale,frames=10)
                (args.out/(dataset+'-'+locale+'-gameplay.png')).write_bytes(shot.read_bytes())
            print(dataset, 'filters/save-disable PASS', flush=True)
        assert hashes=={res: hashlib.sha256((ROOT/res).read_bytes()).hexdigest() for _,res,_ in SAMPLES}
    report=dict(status='PASS',checks=checks,originals_sha256=hashes,app_sha256=hashlib.sha256(args.app.read_bytes()).hexdigest())
    (args.out/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(len(checks), 'processes PASS; temporary profiles and unselected frames removed')

if __name__ == '__main__': main()
