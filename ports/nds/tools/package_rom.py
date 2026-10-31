"""Verify embedded NitroFS payload and publish one ROM under artifacts/nds."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

ROOT = Path(__file__).resolve().parents[3]
OUTPUT = ROOT / 'artifacts/nds'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                sha256=digest(path.read_bytes()))


def nitro_files(rom):
    """Read the ROM's FNT/FAT independently of the SDK/packing directory."""
    assert len(rom) >= 512, 'Truncated NDS header'
    fnt_at, fnt_bytes, fat_at, fat_bytes = struct.unpack_from('<4I', rom, 0x40)
    assert fnt_bytes >= 8 and fat_bytes and fat_bytes % 8 == 0
    assert 512 <= fnt_at <= len(rom) - fnt_bytes
    assert 512 <= fat_at <= len(rom) - fat_bytes
    fnt = rom[fnt_at:fnt_at + fnt_bytes]
    count = struct.unpack_from('<H', fnt, 6)[0]
    assert 1 <= count <= 4096 and count * 8 <= len(fnt)
    result, visited, ids = {}, set(), set()

    def directory(index, prefix):
        assert index < count and index not in visited
        visited.add(index)
        at, file_id = struct.unpack_from('<IH', fnt, index * 8)
        assert count * 8 <= at < len(fnt)
        while True:
            assert at < len(fnt)
            length = fnt[at]
            at += 1
            if not length:
                break
            n = length & 127
            assert n and at + n <= len(fnt)
            name = fnt[at:at + n].decode('ascii')
            assert name not in ('.', '..') and '/' not in name and '\\' not in name
            at += n
            path = prefix + name
            if length & 128:
                assert at + 2 <= len(fnt)
                child = struct.unpack_from('<H', fnt, at)[0]
                at += 2
                assert child >= 0xf000
                directory(child - 0xf000, path + '/')
            else:
                assert file_id < fat_bytes // 8 and file_id not in ids and path not in result
                ids.add(file_id)
                start, end = struct.unpack_from('<II', rom, fat_at + 8 * file_id)
                assert 512 <= start <= end <= len(rom)
                result[path] = rom[start:end]
                file_id += 1

    directory(0, '')
    assert len(visited) == count and len(ids) == fat_bytes // 8
    return result


def package(target):
    target = target.resolve()
    assert target.is_relative_to(ROOT / 'build/nds')
    manifest_path = target / 'build-manifest.json'
    manifest = json.loads(manifest_path.read_text('utf8'))
    assert manifest['embedded_resources'] and manifest['frontend']
    assert not manifest['audit'] and not manifest['frontend_fixtures']
    source = target / 'dok_nds_gameplay.nds'
    raw = source.read_bytes()
    record = next(r for r in manifest['artifacts'] if r['path'].endswith('.nds'))
    assert digest(raw) == record['sha256'] and len(raw) == record['bytes']
    files = nitro_files(raw)
    expected = {'dok/assets.ndp', 'dok/ui.dat', 'dok/frontend.dat','dok/frames.dat','dok/sounds.dat'}
    assert set(files) == expected, ('Unexpected embedded payload', sorted(files))
    payload = []
    for name in sorted(expected):
        suffix = '/nitrofs/' + name
        entry = next(r for r in manifest['generated_inputs'] if r['path'].endswith(suffix))
        assert len(files[name]) == entry['bytes'] and digest(files[name]) == entry['sha256']
        payload.append(dict(file=name, bytes=len(files[name]), sha256=digest(files[name])))
    OUTPUT.mkdir(parents=True, exist_ok=True)
    assert not any(p.is_symlink() or p.is_junction() for p in (OUTPUT, *OUTPUT.parents))
    destination = OUTPUT / 'dok.nds'
    assert not destination.is_symlink() and (not destination.exists() or destination.stat().st_nlink == 1)
    shutil.copyfile(source, destination)
    assert destination.read_bytes() == raw
    report = dict(build_manifest=pin(manifest_path), source=pin(source),
                  published=pin(destination), payload=payload,
                  single_rom=True, external_resources_required=False,
                  settings='Optional fat:/dok/settings.dat; not required to play')
    (OUTPUT / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n', 'utf8')
    print(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', type=Path, required=True)
    args = parser.parse_args()
    package(args.target)
