"""SP17 real-process save/load, isolated profiles, both renderers and seven levels."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from compiler_cache import ROOT
from experiment_cleanup import temporary_workspace

SAMPLES = [('old_gold', 'originals/Old_Gold_3D/OLD_GOLD.RES', [4]),
           ('pkr', 'originals/pkr/RUS/PKR.RES', [1, 2, 3]),
           ('pke', 'originals/pkr/ENG/PKE.RES', [1, 2, 3])]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--app', type=Path, default=ROOT / 'build/sdl2/windows/OGDoK.exe')
    parser.add_argument('--out', type=Path, default=ROOT / 'research/evidence/source-port/sp17')
    args = parser.parse_args()
    with temporary_workspace(args.out.resolve(), 'run-') as out:
        check(args, out)


def check(args, out):
    checks = []
    hashes = {res: hashlib.sha256((ROOT / res).read_bytes()).hexdigest() for _, res, _ in SAMPLES}

    def run(name, profile, res, level=None, script='', backend='software', size='640x480', locale='en', frames=24):
        path = out / (name + '.txt')
        path.write_text(script, encoding='utf-8')
        command = [str(args.app.resolve()), '--hidden', '--user-dir', str(profile), '--res', str(ROOT / res),
                   '--renderer', backend, '--locale', locale, '--size', size, '--frames', str(frames),
                   '--ui-script', str(path), '--screenshot', str(out / (name + '.png'))]
        if level:
            command += ['--level', 'LEVEL' + str(level), '--skip-intro']
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                env=dict(os.environ, SDL_AUDIODRIVER='dummy'), timeout=90)
        log = (profile / 'port.log').read_text(encoding='utf-8')
        (out / (name + '.log')).write_text(log, encoding='utf-8')
        assert result.returncode == 0, (name, result.stdout, result.stderr, log)
        checks.append(dict(name=name, command=command, script=script, exit=result.returncode,
                           events=[line for line in log.splitlines() if line.startswith(('STATE ', 'SAVE '))]))
        return log

    for dataset, res, levels in SAMPLES:
        for backend in ('software', 'gl'):
            profile = out / (dataset + '-' + backend) / 'профиль'
            for level in levels:
                name = f'{dataset}-{backend}-{level}'
                log = run(name, profile, res, level, '2 key F5\n4 state saved\n6 key F9\n8 state loaded\n', backend)
                assert 'SAVE written slot=0' in log and 'SAVE loaded slot=0' in log, (name, log)
                assert 'message_key=port.saved' in log and 'message_key=port.loaded' in log
                path = profile / 'saves' / dataset / 'quick.sav'
                data = path.read_bytes()
                assert data[:8] == b'DKSV\x01\0\0\0' and int.from_bytes(data[8:12], 'little') in (1, 2)
                assert hashlib.sha256(data[:-32]).digest() == data[-32:]
                original = hashlib.sha256(data).hexdigest()
                # Load through the new dataset menu in a new process.
                log = run(name + '-restart', profile, res, script=(
                    '1 key Return\n2 key Return\n3 key Down\n4 key Return\n'
                    '5 key Return\n6 key Return\n8 state restarted\n'), backend=backend)
                assert 'SAVE loaded slot=0' in log, (name, log)
                assert hashlib.sha256(path.read_bytes()).hexdigest() == original
                # A different initial level must not constrain quickload.
                alternate = levels[0] if level != levels[0] else levels[-1]
                log = run(name + '-cross-level', profile, res, alternate, '2 key F9\n4 state cross\n', backend)
                assert f'level=LEVEL{level}' in log and 'SAVE loaded slot=0' in log, log

            # Manual slot UI and Unicode name entry, then preview and deletion.
            log = run(dataset + '-' + backend + '-manual', profile, res, levels[0],
                      '1 key Escape\n2 key Down\n3 key Down\n4 key Down\n5 key Return\n'
                      '6 key Return\n7 text Проверка сейва\n8 key Return\n10 state manual\n', backend)
            assert 'SAVE written slot=1' in log, log
            slot = profile / 'saves' / dataset / 'slot01.sav'
            assert 'Проверка сейва'.encode() in slot.read_bytes()
            log = run(dataset + '-' + backend + '-slots', profile, res, levels[0],
                      '1 key Escape\n2 key Down\n3 key Down\n4 key Down\n5 key Down\n6 key Return\n',
                      backend, locale='ru', frames=12)
            assert 'menu=33' in log

    # Invalid data must leave the live world intact.
    dataset, res, levels = SAMPLES[1]
    profile = out / 'failure'
    run('failure-create', profile, res, 1, '2 key F5\n')
    save = profile / 'saves/pkr/quick.sav'
    data = bytearray(save.read_bytes()); data[30] ^= 1; save.write_bytes(data)
    log = run('failure-corrupt', profile, res, 1, '1 key Escape\n2 state before\n3 key F9\n5 state after\n')
    states = dict(re.findall(r'^STATE (\w+) (.+)$', log, re.M))
    assert 'SAVE load failed:' in log
    assert states['before'].split('world=')[1] == states['after'].split('world=')[1]

    # Confirmed backup recovery, preserving the corrupt primary until an explicit save.
    profile = out / 'backup'
    run('backup-create', profile, res, 1, '2 key F5\n4 key F5\n')
    save = profile / 'saves/pkr/quick.sav'
    backup = save.with_suffix('.sav.bak')
    assert backup.exists()
    data = bytearray(save.read_bytes()); data[30] ^= 1; save.write_bytes(data)
    corrupt_hash = hashlib.sha256(data).hexdigest()
    log = run('backup-recover', profile, res, 1,
              '1 key Escape\n2 key F9\n3 key Up\n4 key Return\n6 state recovered\n')
    assert 'SAVE loaded slot=0 backup=1' in log
    assert hashlib.sha256(save.read_bytes()).hexdigest() == corrupt_hash

    # A semantically invalid but correctly checksummed primary must not replace
    # the previous good backup on the next explicit save.
    backup_hash = hashlib.sha256(backup.read_bytes()).hexdigest()
    bad_version = bytearray(backup.read_bytes()); bad_version[4] = 2
    bad_version[-32:] = hashlib.sha256(bad_version[:-32]).digest()
    save.write_bytes(bad_version)
    run('backup-preserve', profile, res, 1, '2 key F5\n')
    assert hashlib.sha256(backup.read_bytes()).hexdigest() == backup_hash

    # Refused write must preserve the destination and the paused world.
    profile = out / 'write-failure'
    blocked = profile / 'saves/pkr/quick.sav'
    blocked.mkdir(parents=True)
    (blocked / 'keep').write_bytes(b'keep')
    log = run('write-failure', profile, res, 1,
              '1 key Escape\n2 state before\n3 key F5\n5 state after\n')
    assert 'SAVE write failed:' in log and (blocked / 'keep').read_bytes() == b'keep'
    states = dict(re.findall(r'^STATE (\w+) (.+)$', log, re.M))
    assert states['before'].split('world=')[1] == states['after'].split('world=')[1]

    # Move the complete profile, including settings and saves, then load from it.
    profile = out / 'move-source'
    run('move-create', profile, res, 1, '1 key F3\n2 key F5\n')
    moved = out / 'перенесённый профиль'
    shutil.copytree(profile, moved)
    log = run('move-load', moved, res, 3, '2 key F9\n4 state moved\n', backend='gl', locale='ru')
    assert 'SAVE loaded slot=0' in log and 'level=LEVEL1' in log
    assert (moved / 'settings.ini').exists() and (moved / 'saves/pkr/quick.sav').exists()

    # Compact RU/EN list/detail screens and explicit deletion.
    profile = out / 'pkr-software' / 'профиль'
    for locale in ('ru', 'en'):
        run('compact-' + locale, profile, res, 1,
            '1 key Escape\n2 key Down\n3 key Down\n4 key Down\n5 key Down\n6 key Return\n',
            size='320x200', locale=locale, frames=12)
    slot = profile / 'saves/pkr/slot01.sav'
    log = run('manual-load', profile, res, 3,
              '1 key Escape\n2 key Down\n3 key Down\n4 key Down\n5 key Down\n6 key Return\n'
              '7 key Down\n8 key Return\n9 key Return\n10 key Up\n11 key Return\n13 state manual_loaded\n')
    assert 'SAVE loaded slot=1' in log and 'level=LEVEL1' in log
    run('manual-delete', profile, res, 1,
        '1 key Escape\n2 key Down\n3 key Down\n4 key Down\n5 key Down\n6 key Return\n'
        '7 key Down\n8 key Return\n9 key Down\n10 key Return\n11 key Up\n12 key Return\n')
    assert not slot.exists() and (profile / 'saves/pkr/quick.sav').exists()

    # F5 captures the game behind the map, then redraws the still-open map.
    # It must not flash one world frame in place of the map.
    for backend in ('software', 'gl'):
        before, during, after = [out / f'map-{backend}-{part}.png' for part in ('before', 'during', 'after')]
        log = run('map-' + backend, out / ('map-' + backend), res, 1,
                  f'1 key Tab\n2 shot {before}\n3 key F5\n3 shot {during}\n4 shot {after}\n',
                  backend=backend, frames=6)
        assert 'SAVE written slot=0' in log
        assert before.read_bytes() == during.read_bytes() == after.read_bytes(), 'F5 replaced the map frame'
        checks[-1]['map_frame_sha256'] = {part: hashlib.sha256(path.read_bytes()).hexdigest()
                                          for part, path in [('before', before), ('during', during), ('after', after)]}

    assert hashes == {res: hashlib.sha256((ROOT / res).read_bytes()).hexdigest() for _, res, _ in SAMPLES}
    report = dict(checks=checks, originals_sha256=hashes, status='PASS',
                  app_sha256=hashlib.sha256(args.app.resolve().read_bytes()).hexdigest(),
                  tool_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    (args.out / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'SP17 {len(checks)} processes PASS; temporary profiles removed on exit')


if __name__ == '__main__':
    main()
