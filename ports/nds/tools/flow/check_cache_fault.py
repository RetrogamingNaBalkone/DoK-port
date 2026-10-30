"""Same-data native C proof for the captured N22 ordinary CPU-cache failure."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

from check_host import ROOT, pin, compiler_environment
from decode_cache_fault import decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--memory', type=Path, required=True)
    parser.add_argument('--layout', type=Path, required=True)
    parser.add_argument('--ordinary', type=Path, required=True)
    parser.add_argument('--replay', type=Path, required=True)
    args = parser.parse_args()
    memory, layout_file = args.memory.resolve(), args.layout.resolve()
    layout = json.loads(layout_file.read_text('utf8'))
    observed = decode(memory, layout)
    assert all(observed['consistency_checks'].values())
    assert all(row['matches'] for row in observed['cached_pixel_hash_checks'])
    raw = memory.read_bytes()

    def data(address, count):
        offset = address-0x02000000
        assert 0 <= offset <= len(raw)-count
        return raw[offset:offset+count]

    def u32(address): return struct.unpack('<I', data(address, 4))[0]

    types = layout['types']
    bank = observed['storage']+types['SpriteWorld']['members']['bank']
    bank_fields = types['dok_sprite_asset_bank']['members']
    images = {u32(bank+bank_fields['images']+120*i): data(bank+bank_fields['images']+120*i, 120)
              for i in range(u32(bank+bank_fields['image_count']))}
    ids = list(dict.fromkeys(row['gpu_id'] for row in observed['list']['items']))
    if observed['weapon']['visible'] and observed['weapon']['current']['gpu_id'] not in ids:
        ids.append(observed['weapon']['current']['gpu_id'])
    captured_keys = {r['key'] for r in observed['cache']['records']}
    missing_index = next(i for i, key in enumerate(ids) if key not in captured_keys)
    cache, frames = observed['cache'], observed['frames']
    assert len(ids) == 15 and missing_index == 13
    header = [0x4343324e, cache['capacity'], cache['used'], cache['age'], cache['count'],
              len(ids), frames['count'], frames['bytes'], missing_index,
              cache['hits'], cache['misses'], cache['evictions'], cache['read_bytes'],
              frames['decoded_bytes'], frames['decodes'], frames['decoded_total']]
    fixture = bytearray(struct.pack('<16I', *header))
    for record in cache['records']:
        fixture += struct.pack('<Q4I', *(record[key] for key in ('key', 'offset', 'bytes', 'age', 'pinned')))
    fixture += data(cache['pool'], cache['used'])
    frames_address = observed['storage']+types['SpriteWorld']['members']['frames']
    fixture += data(frames_address, frames['count']*16)
    fixture += data(frames['pool'], frames['bytes'])
    fixture += b''.join(images[key] for key in ids)
    evidence = ROOT/'research/evidence/nds/n22/cpu-cache-fault-01'
    evidence.mkdir(parents=True, exist_ok=True)
    fixture_file = evidence/'captured-case.bin'
    fixture_file.write_bytes(fixture)
    output = ROOT/'build/nds/n22/host';output.mkdir(parents=True, exist_ok=True)
    binary, decoded_file = output/'cache-fault.exe', output/'cache-fault-decoded.bin'
    sources = ['ports/nds/render/frame_store.c', 'ports/nds/render/texture_cache.c',
               'ports/nds/tools/flow/test_cache_fault.c']
    zig = Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    command = [str(zig), 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
               '-Iports/nds/include', *sources, '-o', str(binary)]
    result = subprocess.run(command, cwd=ROOT, env=compiler_environment(ROOT), capture_output=True, text=True)
    assert not result.returncode, result.stdout+result.stderr
    result = subprocess.run([str(binary), str(fixture_file), str(decoded_file)], cwd=ROOT, capture_output=True, text=True)
    assert not result.returncode, result.stdout+result.stderr
    native = json.loads(result.stdout)
    decoded = decoded_file.read_bytes();offset=0;pixel_checks=[]
    for key in ids:
        actual_key, count = struct.unpack_from('<2I', decoded, offset);offset+=8
        assert actual_key == key and count == struct.unpack_from('<I', images[key], 12)[0]
        actual = hashlib.sha256(decoded[offset:offset+count]).digest();offset+=count
        expected = images[key][40:72]
        assert actual == expected, key
        pixel_checks.append(dict(id=key, bytes=count, sha256=actual.hex()))
    assert offset == len(decoded)
    ordinary = json.loads(args.ordinary.read_text('utf8'))
    replay = json.loads(args.replay.read_text('utf8'))
    assert ordinary['rows'] == replay['rows'] and ordinary['fault_snapshot'] == replay['fault_snapshot']
    ordinary_inputs = [(row['video_frame'], row['buttons']) for row in ordinary['input_steps']]
    replay_inputs = [(row['video_frame'], row['buttons']) for row in replay['input_steps']]
    assert ordinary_inputs == replay_inputs
    assert all('touch' in row for row in ordinary['input_steps']) and replay['touch_replay_complete']
    assert ordinary['disk']['passed'] and replay['disk']['passed']
    assert ordinary['disk']['after'] == replay['disk']['after']
    for source in sources+[str(Path(__file__).relative_to(ROOT)), 'ports/nds/tools/flow/capture_replay_fault.py',
                            'ports/nds/tools/flow/decode_cache_fault.py', 'ports/nds/tools/flow/inspect_cache_layout.py']:
        target=evidence/'sources'/source;target.parent.mkdir(parents=True, exist_ok=True);shutil.copyfile(ROOT/source, target)
    report = dict(task='N22', passed=True, scope='Captured ordinary EN LEVEL2 CPU-cache fault diagnosis; no playthrough acceptance',
        ordinary=pin(args.ordinary.resolve()), replay=pin(args.replay.resolve()),
        rom=ordinary['rom'], elf=ordinary['elf'], core=ordinary['core'],
        memory=pin(memory), layout=pin(layout_file), fixture=pin(fixture_file),
        native=native, command=command, binary=pin(binary),
        same_rows=len(ordinary['rows']), same_fault_snapshot=True,
        same_physical_button_events=len(ordinary_inputs), touch_replay_complete=True,
        unchanged_fat_files=ordinary['disk']['after'],
        failed_gpu_id=ids[missing_index], request_sequence=ids,
        consistency_checks=observed['consistency_checks'],
        cached_pixel_hashes_matched=len(observed['cached_pixel_hash_checks']),
        decoded_pixel_hashes=pixel_checks, sprite_bytes=observed['list']['unique_image_bytes'],
        weapon=observed['weapon'], capacity=cache['capacity'],
        current_total_raw_bytes=observed['weapon']['visible_set_plus_current_bytes'],
        current_deficit_bytes=observed['weapon']['visible_set_plus_current_bytes']-cache['capacity'],
        inputs=[pin(ROOT/source) for source in sources]+[pin(Path(__file__).resolve())]+
               [pin(path) for path in sorted((ROOT/'ports/nds/include/dok').glob('*.h'))],
        limitation='No injected ARM9 cache flush. All retained raw cache payloads and successful host decodes match immutable GPU asset SHA; metadata checks and full replay agree. This proves this working set, not a whole-game maximum.')
    (evidence/'host.json').write_text(json.dumps(report, indent=2)+'\n', 'utf8')
    (evidence/'fault-decode.json').write_text(json.dumps(observed, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(native=native, raw_bytes=report['current_total_raw_bytes'], deficit=report['current_deficit_bytes'],
                          decoded_hashes=len(pixel_checks), report=pin(evidence/'host.json'))))


if __name__ == '__main__':
    main()
