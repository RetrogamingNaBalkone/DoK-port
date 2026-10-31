"""Build a regular FAT16 image for the N04 emulator, never a physical device.

The real package and tiny independent synthetic controls share one image.
Only current outputs under build/nds/n04 are written. No packer import.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[3]


def safe_output(path):
    if ".." in path.parts:
        raise ValueError("Fixture output cannot contain parent traversal")
    path = path.absolute()
    allowed = ROOT / "build/nds/n04"
    if not path.is_relative_to(allowed):
        raise ValueError("Fixture output must stay inside build/nds/n04")
    current = ROOT
    for part in path.parent.relative_to(ROOT).parts:
        current /= part
        if current.is_symlink() or (hasattr(current, "is_junction") and current.is_junction()):
            raise ValueError("Fixture directory cannot contain links")
        if current.exists() and not current.is_dir():
            raise ValueError("Fixture directory is not a directory")
    if path.is_symlink() or (path.exists() and
            (not path.is_file() or path.stat().st_nlink != 1)):
        raise ValueError("Fixture output must be a regular file without links")
    return path


def sha(data):
    return hashlib.sha256(data).digest()


def tiny_package(archive_hashes, mutation=None):
    """Two valid records: RAW zero VGA6 palette and its RGB555 derivative.

    These are format controls, not claimed original-derived game resources.
    The malformed derivative has a correct digest so schema validation is
    tested independently from integrity validation.
    """
    raw = bytes(768)
    converted = bytearray(512)
    if mutation == "schema":
        converted[1] = 0x80  # reserved bit 15 of first RGB555 entry
    raw_id = (1 << 30) | (1 << 10)
    data_offset = 160 + 2 * 128
    records = bytearray(256)
    for i, representation, data, offset in (
            (0, 1, raw, data_offset),
            (1, 7, bytes(converted), data_offset + len(raw))):
        record = memoryview(records)[i * 128:(i + 1) * 128]
        identifier = (1 << 30) | (representation << 10)
        struct.pack_into("<4I", record, 0, identifier, raw_id, offset, len(data))
        struct.pack_into("<HBB6H2I", record, 16, representation, 1, 4, 0,
                         0xFFFF, 0, 0, 0, 0, 0, 0)
        record[40:72] = sha(data)
        record[72:104] = sha(raw)
        record[104:111] = b"TESTPAL"
    header = bytearray(160)
    header[:8] = b"DOKNDS1\0"
    struct.pack_into("<4H4I", header, 8, 2 if mutation == "version" else 1,
                     160, 128, 0, 2, 160, data_offset, data_offset + 768 + 512)
    header[32:64] = sha(records)
    header[64:128] = archive_hashes
    header[128:160] = sha(header[:128])
    result = bytearray(header + records + raw + converted)
    if mutation == "hash":
        result[-1] ^= 1
    elif mutation == "truncated":
        del result[160 + 127:]
    return bytes(result)


def directory_entry(short_name, attributes, cluster, size=0):
    if len(short_name) != 11:
        raise ValueError("Short FAT name must contain eleven bytes")
    row = bytearray(32)
    row[:11] = short_name
    row[11] = attributes
    date = ((2026 - 1980) << 9) | (10 << 5) | 29
    struct.pack_into("<H", row, 16, date)
    struct.pack_into("<H", row, 18, date)
    struct.pack_into("<H", row, 24, date)
    struct.pack_into("<HI", row, 26, cluster, size)
    return row


def long_name_entries(name, short_name):
    checksum = 0
    for byte in short_name:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + byte) & 255
    units = list(struct.unpack("<" + "H" * len(name), name.encode("utf-16le"))) + [0]
    units += [0xFFFF] * ((-len(units)) % 13)
    rows = []
    count = len(units) // 13
    for index in range(count - 1, -1, -1):
        row = bytearray(32)
        row[0] = index + 1 | (0x40 if index == count - 1 else 0)
        row[11] = 0x0F
        row[13] = checksum
        part = units[index * 13:(index + 1) * 13]
        for offset, first, end in ((1, 0, 5), (14, 5, 11), (28, 11, 13)):
            struct.pack_into("<" + "H" * (end - first), row, offset, *part[first:end])
        rows.append(row)
    return b"".join(rows)


def write_image(path, files, include_dok=True):
    if not include_dok and files:
        raise ValueError("Root-only empty fixture cannot contain DOK files")
    sectors, start, fat_sectors, root_sectors, cluster_bytes = 65536, 2048, 64, 32, 2048
    volume_sectors = sectors - start
    root_start = start + 1 + 2 * fat_sectors
    data_start = root_start + root_sectors
    cluster_limit = (sectors - data_start) // 4 + 2
    fat = bytearray(fat_sectors * 512)
    struct.pack_into("<3H", fat, 0, 0xFFF8, 0xFFFF, 0xFFFF if include_dok else 0)
    directory = directory_entry(b".          ", 0x10, 2)
    directory += directory_entry(b"..         ", 0x10, 0)
    allocated = []
    next_cluster = 3
    for name, short_name, data in files:
        count = max(1, (len(data) + cluster_bytes - 1) // cluster_bytes)
        if next_cluster + count > cluster_limit:
            raise ValueError("Fixture does not fit FAT16 data region")
        for i in range(count):
            struct.pack_into("<H", fat, 2 * (next_cluster + i),
                             0xFFFF if i == count - 1 else next_cluster + i + 1)
        if name == "badschema.ndp":
            directory += long_name_entries(name, short_name)
        directory += directory_entry(short_name, 0x20, next_cluster, len(data))
        allocated.append((next_cluster, data))
        next_cluster += count
    if len(directory) > cluster_bytes:
        raise ValueError("Fixture directory needs a larger allocation")
    with path.open("wb") as stream:
        stream.truncate(sectors * 512)
        mbr = bytearray(512)
        mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 6,
                                   b"\xfe\xff\xff", start, volume_sectors)
        mbr[510:512] = b"\x55\xaa"
        stream.write(mbr)
        boot = bytearray(512)
        boot[:11] = b"\xeb\x3c\x90DOKNDS04"
        struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, 4, 1, 2, 512,
                         volume_sectors, 0xF8, fat_sectors, 63, 255, start, 0)
        boot[36:39] = b"\x80\x00\x29"
        struct.pack_into("<I", boot, 39, 0x344B4F44)
        boot[43:54] = b"DOK N04    "
        boot[54:62] = b"FAT16   "
        boot[510:512] = b"\x55\xaa"
        stream.seek(start * 512)
        stream.write(boot)
        for sector in (start + 1, start + 1 + fat_sectors):
            stream.seek(sector * 512)
            stream.write(fat)
        stream.seek(root_start * 512)
        if include_dok:
            stream.write(directory_entry(b"DOK        ", 0x10, 2))
            stream.seek(data_start * 512)
            stream.write(directory)
        for cluster, data in allocated:
            stream.seek(data_start * 512 + (cluster - 2) * cluster_bytes)
            stream.write(data)


def make_fixture(package, output, include_package=True):
    output = safe_output(output)
    data = package.read_bytes()
    if len(data) < 160 or data[:8] != b"DOKNDS1\0":
        raise ValueError("Expected the actual DOKNDS1 package")
    archives = data[64:128]
    files = [("assets.ndp", b"ASSETS  NDP", data),
             ("control.ndp", b"CONTROL NDP", tiny_package(archives)),
             ("wrong.ndp", b"WRONG   NDP", tiny_package(archives, "version")),
             ("trunc.ndp", b"TRUNC   NDP", tiny_package(archives, "truncated")),
             ("badhash.ndp", b"BADHASH NDP", tiny_package(archives, "hash")),
             ("badschema.ndp", b"BADSCH~1NDP", tiny_package(archives, "schema"))]
    if not include_package:
        files = [item for item in files if item[0] != "assets.ndp"]
    output.parent.mkdir(parents=True, exist_ok=True)
    write_image(output, files)
    return {"schema_version": 1, "task": "N04", "image": str(output),
            "format": "MBR/FAT16, 32 MiB; /DOK; one VFAT filename",
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
            "package_sha256": sha(data).hex(),
            "files": [{"path": "/dok/" + name, "bytes": len(payload),
                       "sha256": sha(payload).hex()}
                      for name, _short, payload in files],
            "controls": "Tiny palette packages are synthetic format controls, not original assets."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT / "build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--out", type=Path, default=ROOT / "build/nds/n04/assets-fixture.bin")
    args = parser.parse_args()
    if args.out.suffix.lower() != ".bin":
        parser.error("--out must use .bin so image and report remain separate")
    path = safe_output(args.out.with_suffix(".json"))
    report = make_fixture(args.package.resolve(), args.out)
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"N04 fixture: {args.out}; {report['sha256']}")


if __name__ == "__main__":
    main()
