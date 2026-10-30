"""Read-only FAT16 audit for N21 settings-only persistence experiments."""
from __future__ import annotations

import hashlib
import struct
from pathlib import Path


def volume_files(path: Path):
    raw = path.read_bytes()
    assert raw[510:512] == b"\x55\xaa"
    start = struct.unpack_from("<I", raw, 454)[0] * 512
    assert raw[start + 510:start + 512] == b"\x55\xaa"
    sector, cluster_sectors, reserved, copies, roots = struct.unpack_from("<HBHBH", raw, start + 11)
    fat_sectors = struct.unpack_from("<H", raw, start + 22)[0]
    assert sector == 512 and cluster_sectors and copies == 2
    fat_at = start + reserved * sector
    fat_size = fat_sectors * sector
    fat = raw[fat_at:fat_at + fat_size]
    assert fat == raw[fat_at + fat_size:fat_at + 2 * fat_size], "FAT mirrors diverged"
    root_at = fat_at + copies * fat_size
    root_size = ((roots * 32 + sector - 1) // sector) * sector
    data_at = root_at + root_size
    cluster_bytes = cluster_sectors * sector
    used = set()

    def chain(first):
        data = bytearray()
        local = set()
        current = first
        while current < 0xfff8:
            assert current >= 2 and current not in local and current not in used
            assert current * 2 + 2 <= len(fat)
            local.add(current)
            offset = data_at + (current - 2) * cluster_bytes
            assert offset + cluster_bytes <= len(raw)
            data.extend(raw[offset:offset + cluster_bytes])
            current = struct.unpack_from("<H", fat, current * 2)[0]
        used.update(local)
        return bytes(data)

    files = {}

    def directory(data, parent):
        for at in range(0, len(data), 32):
            row = data[at:at + 32]
            if not row[0]:
                break
            if row[0] == 0xe5 or row[11] & 0x08 or row[11] == 0x0f:
                continue
            base, extension = row[:8].rstrip(b" "), row[8:11].rstrip(b" ")
            if base in (b".", b".."):
                continue
            name = (base + (b"." + extension if extension else b"")).decode("ascii")
            full = parent + "/" + name
            assert full not in files
            first, size = struct.unpack_from("<HI", row, 26)
            payload = chain(first) if first else b""
            if row[11] & 0x10:
                assert first >= 2
                directory(payload, full)
            else:
                assert size <= len(payload)
                files[full] = payload[:size]

    directory(raw[root_at:root_at + root_size], "")
    return files


def audit_changes(before, after, allowed):
    """Compare logical file contents, including files the core injects at load."""
    changed = sorted(name for name in before.keys() | after.keys()
                     if before.get(name) != after.get(name))
    assert set(changed) <= set(allowed), f"Unexpected disk writes: {changed}"
    return {"passed": True, "changed_files": changed,
            "after": {name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                      for name, data in after.items()}}
