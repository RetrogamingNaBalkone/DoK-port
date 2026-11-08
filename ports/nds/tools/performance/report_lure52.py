"""Qualified reproduction of the LEVEL3 key-room crowd outside its door."""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[4]


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    directory=ROOT/'build/nds/n23/angles51d-ru/lure52e-lure-live'
    result=json.loads((directory/'result.json').read_text('utf8'))
    runtime=json.loads((directory/'runtime-profile.json').read_text('utf8'))
    assert result['passed'] and result['all_room_enemies_out'] and result['all_executioners_alive']
    samples=[]
    for s in result['samples']:
        ids={r['frame_id'] for r in s['peaks']}
        samples.append(dict(name=s['name'],pose=s['pose'],fps=s['fps'],
            minimum_window_fps=s['minimum_window_fps'],shots=s['shots'],
            maximum_projectiles=max(r['active_projectiles']for r in s['load_samples']),
            stages_ms=s['stages_ms'],
            peak_uploads=[{k:r[k] for k in ('frame_id','uploads','upload_bytes','texture_bytes','input_quads')}
                          for r in runtime['history_records']if r['frame_id']in ids]))
    rom=ROOT/'artifacts/nds/dok.nds'
    assert pin(rom)['sha256']==result['rom_sha256']
    report=dict(task='N23',reproduction_passed=True,gameplay_changed=False,
        rom=pin(rom),open_door=result['open_door'],corridor_group=result['corridor_group'],
        samples=samples,proofs=[pin(directory/n)for n in ('result.json','runtime-profile.json')],
        limitations=['Hero spawn/retry, HP30000 and key8 fixtures; no enemy writes.',
            'All six room actors crossed the door and all four executioners remained alive.',
            'This is not a complete ordinary-input route or proof that all ten actors stayed inside the camera simultaneously.',
            'DS-mode melonDS; no new physical DS or DraStic test.'],
        next_task='N23: preserve reusable animation textures in spare VRAM, with pressure eviction and upload deadlines unchanged')
    path=ROOT/'analysis/nds-lure52-20261108.json'
    path.write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,samples=[{k:s[k]for k in ('name','fps','minimum_window_fps')}for s in samples])))


if __name__=='__main__':main()
