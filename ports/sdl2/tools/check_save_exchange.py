"""Create/load quicksaves with actual executables on each OS/architecture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['create', 'load'])
    parser.add_argument('--platform', required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--res', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if not args.platform.replace('-', '').replace('_', '').isalnum():
        parser.error('Invalid platform name')
    producers = [args.platform] if args.mode == 'create' else [p.name for p in root.glob('created-*') if p.is_dir()]
    results = []
    for producer in producers:
        profile = root / (f'created-{producer}' if args.mode == 'create' else f'loaded-{args.platform}-{producer}')
        profile.mkdir(exist_ok=False)
        save = profile / 'saves/pkr/quick.sav'
        original_hash = None
        if args.mode == 'load':
            source = root / producer / 'saves/pkr/quick.sav'
            save.parent.mkdir(parents=True)
            shutil.copyfile(source, save)
            original_hash = hashlib.sha256(save.read_bytes()).hexdigest()
        script = profile / 'keys.txt'
        script.write_text('2 key F5\n' if args.mode == 'create' else '2 key F9\n', encoding='utf-8')
        command = [str(args.exe.resolve()), '--hidden', '--verbose', '--renderer', 'software', '--size', '320x200',
                   '--res', str(args.res.resolve()), '--level', 'LEVEL1', '--skip-intro', '--frames', '6',
                   '--user-dir', str(profile), '--ui-script', str(script)]
        process = subprocess.run(command, env=dict(os.environ, SDL_AUDIODRIVER='dummy', SDL_VIDEODRIVER='dummy',
                                                  DOK_SOFTWARE_SDL_PRESENTATION='1'), capture_output=True, timeout=90)
        log = (profile / 'port.log').read_text(encoding='utf-8')
        expected = 'SAVE written slot=0' if args.mode == 'create' else 'SAVE loaded slot=0'
        if process.returncode or expected not in log:
            raise RuntimeError(f'{producer}: {log}\n{process.stderr!r}')
        digest = hashlib.sha256(save.read_bytes()).hexdigest()
        if original_hash and digest != original_hash:
            raise RuntimeError('Loading changed quicksave bytes')
        results.append(dict(producer=producer, sha256=digest))
    if not results:
        raise RuntimeError('No producer saves found')
    (root / f'{args.mode}-{args.platform}.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    print(args.mode, args.platform, len(results), 'PASS')
