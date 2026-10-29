"""Build the independent, deterministic DOKNDS1 resource package (N04).

Only Python's standard library is used. Original game bytes remain local.
The authoritative disk specification is docs/nds/spec/N04-assets.md.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import zlib


ROOT = Path(__file__).resolve().parents[3]
SOURCES = (
    (1, "ru", "originals/pkr/RUS/PKR.RES", "af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816"),
    (2, "en", "originals/pkr/ENG/PKE.RES", "865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458"),
)
REP_NAMES = {
    1: "RAW_SOURCE", 2: "IMAGE_INDEX8", 3: "ANIM_META", 4: "FRAME_INDEX8",
    5: "GPU_INDEX8_POT", 6: "STATIC_INDEX8", 7: "PALETTE_RGB555",
    8: "PCM_CLIP", 9: "PCM_CHUNK", 10: "TEXT_UTF8", 11: "FONT_BITMAP",
    12: "LIGHT_TABLE", 13: "LEVEL_MAPS", 14: "LEVEL_CP866",
    15: "STATIC_GPU_INDEX8", 16: "TEXT_CP866",
}
KIND_NAMES = {1: "IMAGE", 2: "ANIMATION", 3: "FONT", 4: "PALETTE", 5: "LIGHT", 6: "LEVEL", 7: "TEXT", 8: "PCM"}
TEXT_NAMES = {"README", "ENDTXT", "PANEL_DAT", *(f"{p}INTRO{i}" for p in ("B", "E") for i in range(1, 4))}
STATIC_NAMES = {"ЗАСТАВКА_СПР", *(f"{p}SPR{i}" for p in ("B", "E") for i in range(1, 4))}


def digest(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def asset_id(language: int, index: int, representation: int, part: int = 0) -> int:
    if language not in (1, 2) or not 0 <= index <= 4095 or not 1 <= representation <= 255 or not 0 <= part <= 1023:
        raise ValueError("ID components exceed the v1 limits")
    return language << 30 | index << 18 | representation << 10 | part


def guarded_path(path: Path, allowed: Path) -> Path:
    """Resolve every existing ancestor, rejecting output outside its fixed tree."""
    expected_root = ROOT.resolve() / allowed.relative_to(ROOT)
    if allowed.resolve() != expected_root:
        raise ValueError("Fixed output tree must not redirect through a filesystem alias")
    result = path.resolve()
    if not result.is_relative_to(allowed.resolve()):
        raise ValueError(f"Output must remain within {allowed.relative_to(ROOT)}")
    if result == allowed.resolve():
        raise ValueError("Output must name a file")
    if result.exists() and (not result.is_file() or result.stat().st_nlink != 1):
        raise ValueError("Output must be a regular file without hard-link aliases")
    return result


def read_sources() -> list[dict]:
    archives = []
    for language, label, relative, expected in SOURCES:
        path = ROOT / relative
        data = path.read_bytes()
        if digest(data).hex() != expected:
            raise ValueError(f"Unexpected original SHA-256: {relative}")
        if len(data) < 2:
            raise ValueError("Truncated RES header")
        count, = struct.unpack_from("<H", data)
        directory_end = 2 + count * 30
        if count != 146 or directory_end > len(data):
            raise ValueError("Unexpected original RES catalog")
        entries = []
        previous_end = directory_end
        for index in range(count):
            record = data[2 + index * 30:32 + index * 30]
            terminator = record[:16].find(b"\0")
            if terminator <= 0:
                raise ValueError("Invalid original resource name")
            name_bytes = record[:terminator]
            size, offset = struct.unpack_from("<II", record, 16)
            if offset != previous_end or offset > len(data) or size > len(data) - offset:
                raise ValueError("Invalid/noncontiguous original resource range")
            payload = data[offset:offset + size]
            entries.append(dict(index=index, name=name_bytes.decode("cp866"), name_bytes=name_bytes,
                                offset=offset, size=size, payload=payload, sha=digest(payload), record=record))
            previous_end = offset + size
        archives.append(dict(language=language, label=label, path=relative, data=data,
                             sha=digest(data), entries=entries, trailing=data[previous_end:]))
    return archives


def image(payload: bytes, start: int = 0) -> dict:
    if start > len(payload) - 18:
        raise ValueError("Truncated original image header")
    width, height, flags, crop_divisor, crop_index, size, pointer = struct.unpack_from("<5H2I", payload, start)
    if not width or not height or size != width * height or size > len(payload) - start - 18:
        raise ValueError("Malformed original image")
    return dict(width=width, height=height, flags=flags, crop_divisor=crop_divisor,
                crop_index=crop_index, pointer=pointer, end=start + 18 + size,
                pixels=payload[start + 18:start + 18 + size])


def source_kind(entry: dict) -> int:
    name, data = entry["name"], entry["payload"]
    if name in ("FONT8", "FONT16"):
        if len(data) != 256 * (8 if name == "FONT8" else 16):
            raise ValueError("Malformed font")
        return 3
    if name in ("LEVEL1", "LEVEL2", "LEVEL3"):
        return 6
    if name in TEXT_NAMES:
        return 7
    if name.startswith("LIGHT"):
        if len(data) != 4096:
            raise ValueError("Malformed LIGHT")
        return 5
    if len(data) == 768 and max(data) <= 63:
        return 4
    if data[2:5] == b"SVM":
        return 8
    if len(data) >= 18:
        w, h = struct.unpack_from("<HH", data)
        size, = struct.unpack_from("<I", data, 10)
        if w and h and size == w * h and len(data) == 18 + size:
            return 1
    return 2


def padded(pixels: bytes, width: int, height: int, storage_width: int, storage_height: int) -> bytes:
    target = bytearray(storage_width * storage_height)
    for y in range(height):
        target[y * storage_width:y * storage_width + width] = pixels[y * width:(y + 1) * width]
    return bytes(target)


def pot(value: int) -> int:
    result = max(8, 1 << (value - 1).bit_length())
    if result > 1024:
        raise ValueError("Image exceeds GPU v1 dimensions")
    return result


def static_pixels(pixels: bytes) -> bytes:
    return bytes(pixels[((2 * y + 1) * 200 // 384) * 320 + (2 * x + 1) * 320 // 512]
                 for y in range(192) for x in range(256))


def level_maps(data: bytes) -> tuple[bytes, int, int, int, int]:
    sections: dict[bytes, list[bytes]] = collections.defaultdict(list)
    section = b""
    for line in data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n"):
        stripped = line.strip()
        if stripped.startswith(b"[") and stripped.endswith(b"]"):
            section = stripped[1:-1]
        else:
            sections[section].append(line)
    settings = {}
    for line in sections[b"Game Ini"]:
        if b"=" in line and not line.lstrip().startswith(b"#"):
            key, value = line.split(b"=", 1)
            settings[key.strip()] = int(value.strip())
    width, height = settings[b"Level Width"], settings[b"Level Length"]
    side, cell_height = settings[b"Cell Side"], settings[b"Cell Height"]
    if (width, height, settings[b"Level Height"]) != (64, 64, 1) or side != 480 or cell_height != 960:
        raise ValueError("Unexpected LEVEL map profile")
    payload = bytearray(struct.pack("<4H3I", width, height, 1, 3, side, cell_height, 0))
    for name in (b"MMap", b"TMap", b"FCMap"):
        rows = [row for row in sections[name] if row and not row.startswith(b"#")]
        if len(rows) != height or any(len(row) != width for row in rows):
            raise ValueError("Malformed LEVEL plane geometry")
        payload.extend(b"".join(rows))
    return bytes(payload), width, height, side, cell_height


def representations(archives: list[dict]) -> list[dict]:
    output = []
    for archive in archives:
        language = archive["language"]
        by_name = {entry["name"]: entry for entry in archive["entries"]}
        for entry in archive["entries"]:
            kind = source_kind(entry)
            entry["kind"] = kind
            data = entry["payload"]

            def add(representation: int, payload: bytes, width: int = 0, height: int = 0,
                    storage_width: int | None = None, storage_height: int | None = None,
                    param0: int = 0, param1: int = 0, subindex: int = 0xFFFF) -> None:
                part = 0 if subindex == 0xFFFF else subindex
                output.append(dict(id=asset_id(language, entry["index"], representation, part),
                    source_id=asset_id(language, entry["index"], 1), representation=representation,
                    language=language, source_kind=kind, source_index=entry["index"], subindex=subindex,
                    width=width, height=height, storage_width=width if storage_width is None else storage_width,
                    storage_height=height if storage_height is None else storage_height,
                    param0=param0, param1=param1, sha=digest(payload), source_sha=entry["sha"],
                    name=entry["name"], name_bytes=entry["name_bytes"], payload=payload))

            def add_image(im: dict, frame: int = 0xFFFF) -> None:
                w, h, flags = im["width"], im["height"], im["flags"]
                add(2 if frame == 0xFFFF else 4, im["pixels"], w, h, param0=flags,
                    param1=0 if frame == 0xFFFF else 8, subindex=frame)
                sw, sh = pot(w), pot(h)
                add(5, padded(im["pixels"], w, h, sw, sh), w, h, sw, sh,
                    param0=flags, param1=0 if frame == 0xFFFF else 8, subindex=frame)

            add(1, data)
            if kind == 1:
                im = image(data)
                if im["end"] != len(data):
                    raise ValueError("Original image has trailing bytes")
                add_image(im)
                if entry["name"] in STATIC_NAMES:
                    if (im["width"], im["height"]) != (320, 200):
                        raise ValueError("Unexpected static image geometry")
                    palette_name = "ЗАСТАВКА_ПАЛ" if entry["name"] == "ЗАСТАВКА_СПР" else "GAMEP" + entry["name"][-1]
                    palette_id = asset_id(language, by_name[palette_name]["index"], 7)
                    target = static_pixels(im["pixels"])
                    add(6, target, 256, 192, param0=palette_id, param1=1)
                    add(15, padded(target, 256, 192, 256, 256), 256, 192, 256, 256, palette_id, 1)
            elif kind == 2:
                if len(data) < 8:
                    raise ValueError("Truncated animation")
                count, unknown = struct.unpack_from("<HH", data)
                if not 1 <= count <= 1024 or 8 + count * 10 > len(data):
                    raise ValueError("Malformed animation table")
                meta = bytearray(struct.pack("<HHI", count, unknown, 0))
                cursor = 8 + count * 10
                for frame in range(count):
                    interval, a, b = struct.unpack_from("<HHH", data, 8 + frame * 10)
                    im = image(data, cursor)
                    cursor = im["end"]
                    meta.extend(struct.pack("<I6H", asset_id(language, entry["index"], 4, frame), interval,
                                            a, b, im["flags"], im["width"], im["height"]))
                    add_image(im, frame)
                if cursor != len(data):
                    raise ValueError("Original animation has trailing bytes")
                add(3, bytes(meta))
            elif kind == 3:
                height = len(data) // 256
                add(11, data, 8, height, param0=256, param1=1)
            elif kind == 4:
                rgb = [struct.pack("<H", (data[i] >> 1) | ((data[i + 1] >> 1) << 5) | ((data[i + 2] >> 1) << 10))
                       for i in range(0, 768, 3)]
                add(7, b"".join(rgb))
            elif kind == 5:
                add(12, data, 256, 16)
            elif kind == 6:
                maps, w, h, side, cell_height = level_maps(data)
                add(13, maps, w, h, param0=side, param1=cell_height)
                add(14, data, param0=866)
            elif kind == 7:
                add(10, data.decode("cp866").encode("utf-8"), param0=65001)
                add(16, data, param0=866)
            elif kind == 8:
                if len(data) < 36 or struct.unpack_from("<H", data, 5)[0] != 1 or data[7] != 9 or struct.unpack_from("<HH", data, 8) != (8, 0):
                    raise ValueError("Unsupported original SVM format")
                c, d, rate, samples, pointer, unknown28, unknown32 = struct.unpack_from("<HH5I", data, 12)
                if samples != len(data) - 36 or rate not in (11025, 22050) or (c, d) != (255, 128):
                    raise ValueError("Malformed original PCM")
                chunks = (samples + 65535) // 65536
                descriptor = bytearray(struct.pack("<3I2H2I", rate, samples, chunks, c, d, unknown28, unknown32))
                for part, start in enumerate(range(0, samples, 65536)):
                    length = min(65536, samples - start)
                    chunk_id = asset_id(language, entry["index"], 9, part)
                    descriptor.extend(struct.pack("<III", chunk_id, start, length))
                    add(9, bytes(value ^ 128 for value in data[36 + start:36 + start + length]),
                        param0=start, param1=rate, subindex=part)
                add(8, bytes(descriptor), param0=rate, param1=samples)
    output.sort(key=lambda item: item["id"])
    if not 1 <= len(output) <= 2048 or len({item["id"] for item in output}) != len(output):
        raise ValueError("Invalid v1 catalog count/IDs")
    return output


def build_package(archives: list[dict], entries: list[dict]) -> bytes:
    data_offset = 160 + 128 * len(entries)
    output = bytearray(data_offset)
    unique: dict[bytes, list[tuple[bytes, int]]] = collections.defaultdict(list)
    for entry in entries:
        payload = entry["payload"]
        for previous, offset in unique[entry["sha"]]:
            if previous == payload:
                break
        else:
            output.extend(b"\0" * (-len(output) % 32))
            offset = len(output)
            output.extend(payload)
            unique[entry["sha"]].append((payload, offset))
        entry["offset"] = offset
    if len(output) > 0xFFFFFFFF:
        raise ValueError("Package exceeds uint32 format limit")
    for index, entry in enumerate(entries):
        start = 160 + 128 * index
        struct.pack_into("<4IH2B6H2I", output, start, entry["id"], entry["source_id"], entry["offset"],
            len(entry["payload"]), entry["representation"], entry["language"], entry["source_kind"],
            entry["source_index"], entry["subindex"], entry["width"], entry["height"], entry["storage_width"],
            entry["storage_height"], entry["param0"], entry["param1"])
        output[start + 40:start + 72] = entry["sha"]
        output[start + 72:start + 104] = entry["source_sha"]
        output[start + 104:start + 120] = entry["name_bytes"].ljust(16, b"\0")
    struct.pack_into("<8s4H4I", output, 0, b"DOKNDS1\0", 1, 160, 128, 0, len(entries), 160, data_offset, len(output))
    output[32:64] = digest(output[160:data_offset])
    output[64:96] = archives[0]["sha"]
    output[96:128] = archives[1]["sha"]
    output[128:160] = digest(output[:128])
    return bytes(output)


def png(path: Path, width: int, height: int, rgb: bytes) -> None:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    raw = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def previews(archives: list[dict], folder: Path) -> list[str]:
    if not folder.resolve().is_relative_to((ROOT / "build/nds").resolve()):
        raise ValueError("Preview folder escapes the NDS build tree")
    folder.mkdir(parents=True, exist_ok=True)
    paths = []
    for archive in archives:
        by_name = {e["name"]: e for e in archive["entries"]}
        for entry in archive["entries"]:
            if entry["name"] not in STATIC_NAMES:
                continue
            palette_name = "ЗАСТАВКА_ПАЛ" if entry["name"] == "ЗАСТАВКА_СПР" else "GAMEP" + entry["name"][-1]
            palette = by_name[palette_name]["payload"]
            pixels = image(entry["payload"])["pixels"]
            for label, width, height, indices in (("source", 320, 200, pixels), ("target", 256, 192, static_pixels(pixels))):
                rgb = bytes(palette[index * 3 + channel] * 255 // 63 for index in indices for channel in range(3))
                path = guarded_path(folder / f"{archive['label']}-{entry['index']:04d}-{label}.png", ROOT / "build/nds")
                png(path, width, height, rgb)
                paths.append(path.relative_to(ROOT).as_posix())
    # The DOS image uses 5:6 pixel aspect. A radius60x50 ellipse in source
    # coordinates must become a radius48x48 circle on the target square pixels.
    test = bytearray(320 * 200)
    for y in range(200):
        for x in range(320):
            circle = abs(((x - 160) / 60) ** 2 + ((y - 100) / 50) ** 2 - 1) < 0.035
            test[y * 320 + x] = 255 if circle else (100 if x % 40 == 0 or y % 25 == 0 else 10)
    for label, w, h, pixels in (("source", 320, 200, bytes(test)), ("target", 256, 192, static_pixels(test))):
        path = guarded_path(folder / f"aspect-{label}.png", ROOT / "build/nds")
        png(path, w, h, bytes(value for value in pixels for _ in range(3)))
        paths.append(path.relative_to(ROOT).as_posix())
    return paths


def json_write(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def run(output: Path, report_path: Path) -> dict:
    output = guarded_path(output, ROOT / "build/nds")
    report_path = guarded_path(report_path, ROOT / "research/evidence/nds/n04")
    archives = read_sources()
    entries = representations(archives)
    package = build_package(archives, entries)
    # Do not publish any result until original immutability has been checked.
    for archive in archives:
        if digest((ROOT / archive["path"]).read_bytes()) != archive["sha"]:
            raise ValueError("Original archive changed during conversion")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)
    if digest(output.read_bytes()) != digest(package):
        raise IOError("Written package hash mismatch")
    preview_paths = previews(archives, output.parent / "previews")
    provenance = dict(schema_version=1, format="DOKNDS1", source_archives=[], variants=[])
    for archive in archives:
        provenance["source_archives"].append(dict(language=archive["label"], path=archive["path"],
            bytes=len(archive["data"]), sha256=archive["sha"].hex(), trailing_hex=archive["trailing"].hex(),
            entries=[dict(index=e["index"], name=e["name"], name_hex=e["name_bytes"].hex(),
                          offset=e["offset"], bytes=e["size"], sha256=e["sha"].hex(),
                          record_hex=e["record"].hex(), source_kind=KIND_NAMES[e["kind"]]) for e in archive["entries"]]))
    for entry in entries:
        provenance["variants"].append(dict(id=entry["id"], source_id=entry["source_id"],
            representation=REP_NAMES[entry["representation"]], language=entry["language"],
            source_index=entry["source_index"], subindex=entry["subindex"], name=entry["name"],
            source_sha256=entry["source_sha"].hex(), payload_sha256=entry["sha"].hex(),
            offset=entry["offset"], bytes=len(entry["payload"])))
    provenance_path = guarded_path(report_path.with_name("provenance.json"), ROOT / "research/evidence/nds/n04")
    if provenance_path == report_path:
        raise ValueError("Report must not overwrite provenance.json")
    json_write(provenance_path, provenance)
    unique = {(e["offset"], len(e["payload"])) for e in entries}
    report = dict(schema_version=1, success=True, format="DOKNDS1", version=1,
        converter_sha256=digest(Path(__file__).read_bytes()).hex(),
        package=dict(path=output.relative_to(ROOT).as_posix(), bytes=len(package), sha256=digest(package).hex(),
                     header_sha256=package[128:160].hex(), directory_sha256=package[32:64].hex()),
        sources=[dict(language=a["label"], path=a["path"], bytes=len(a["data"]), sha256_before=a["sha"].hex(),
                      sha256_after=digest((ROOT / a["path"]).read_bytes()).hex(), records=len(a["entries"]),
                      categories=dict(sorted(collections.Counter(KIND_NAMES[e["kind"]] for e in a["entries"]).items())))
                 for a in archives], entries=len(entries), representations=dict(sorted(collections.Counter(REP_NAMES[e["representation"]] for e in entries).items())),
        deduplication=dict(unique_payloads=len(unique), aliases=len(entries) - len(unique),
                           logical_payload_bytes=sum(len(e["payload"]) for e in entries),
                           physical_payload_bytes=sum(size for _, size in unique)),
        max_payload_bytes=max(len(e["payload"]) for e in entries),
        max_gpu_payload_bytes=max(len(e["payload"]) for e in entries if e["representation"] == 5),
        provenance=dict(path=provenance_path.relative_to(ROOT).as_posix(), sha256=digest(provenance_path.read_bytes()).hex()),
        previews=preview_paths,
        limitations=["No original execution or renderer equivalence claim", "GPU material/LIGHT/UV policy refined in N06/N07/N13", "Hardware validation deferred to N24"])
    if any(item["sha256_before"] != item["sha256_after"] for item in report["sources"]):
        raise ValueError("Original archive changed before final reporting")
    json_write(report_path, report)
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--report", type=Path, default=ROOT / "research/evidence/nds/n04/converter-report.json")
    args = parser.parse_args()
    report = run(args.output, args.report)
    print(json.dumps(dict(success=True, package=report["package"], entries=report["entries"],
                          unique_payloads=report["deduplication"]["unique_payloads"]), ensure_ascii=False))


if __name__ == "__main__":
    main()
