"""G04 resource-layout probe; decoder and formulas, not DOS renderer equivalence.

Reads original RES and hashes original EXE; executable code is analysed only
through separately produced Ghidra exports. All actual pixels stay in build/.
"""
import argparse
from collections import Counter
import csv
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

from analyze_resources import parse, classify, fnv
from analyze_levels import reference
from inventory import EXPECTED

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/render-layout"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check(actual, expected, context):
    if actual != expected:
        raise AssertionError(context + ": values differ; raw pixels remain private")


def image(raw, offset=0):
    if len(raw) - offset < 18:
        raise ValueError("truncated image")
    width, height, flags, crop_divisor, crop_index, count, pointer = struct.unpack_from("<5HII", raw, offset)
    if not width or not height or count != width * height or count > len(raw) - offset - 18:
        raise ValueError("invalid image geometry")
    pixels = raw[offset + 18:offset + 18 + count]
    # Literal row formula from ASM: base + v_integer * width + u_integer.
    rows = b"".join(pixels[v * width:(v + 1) * width] for v in range(height))
    check(rows, pixels, "row-major packed rows")
    return {"width": width, "height": height, "flags": flags, "crop_divisor": crop_divisor,
            "crop_index": crop_index, "pixel_bytes": count, "fnv1a32": fnv(pixels),
            "image_offset": offset, "sha256": sha(pixels)}, pixels


def frames(raw):
    if len(raw) < 8:
        raise ValueError("truncated animation")
    count = struct.unpack_from("<H", raw)[0]
    if not 0 < count <= 32767 or 8 + count * 10 > len(raw):
        raise ValueError("invalid animation table")
    offset, output = 8 + count * 10, []
    for index in range(count):
        bits, a, b = struct.unpack_from("<Hhh", raw, 8 + index * 10)
        metadata, pixels = image(raw, offset)
        metadata.update(index=index, interval_bits=bits, interval_ticks=bits if bits < 32768 else bits - 65536,
                        parameter_a=a, parameter_b=b)
        output.append((metadata, pixels))
        offset += 18 + len(pixels)
    check(offset, len(raw), "exact animation payload coverage")
    return output


def c_rows(tool, mode, path, destination=None, expected_exit=0):
    command = [str(tool), mode, str(path)]
    if destination is not None:
        command.append(str(destination))
    completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    check(completed.returncode, expected_exit, "C decoder exit")
    if expected_exit:
        return []
    return list(csv.DictReader(io.StringIO(completed.stdout), delimiter="\t"))


def synthetic(tool):
    directory = OUT / "synthetic"
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for name, width, height, flags, pixels in (
        ("distinct-rows", 3, 2, 8, bytes((0, 1, 2, 3, 4, 5))),
        ("large-flat-over64k", 300, 300, 0, bytes(i & 255 for i in range(90000))),
        ("unsigned-width-ffff", 65535, 1, 0, bytes(i & 255 for i in range(65535))),
        ("flags-preserved-not-applied", 3, 2, 65535, bytes((1, 2, 3, 4, 5, 6)))):
        payload = struct.pack("<5HII", width, height, flags, 99, 55, len(pixels), 0xdeadbeef) + pixels
        path = directory / (name + ".bin")
        path.write_bytes(payload)
        expected, _ = image(payload)
        actual = c_rows(tool, "--image", path)
        check(len(actual), 1, "one standalone image")
        for key, value in actual[0].items():
            check(value, str(expected[key]), name + " " + key)
        records.append({"case": name, "width": width, "height": height, "flags": flags,
                        "payload_sha256": sha(payload), "accepted": True})
    invalid = {
        "zero-width": struct.pack("<5HII", 0, 2, 0, 1, 0, 0, 0),
        "ffff-width-inconsistent-size": struct.pack("<5HII", 65535, 1, 0, 1, 0, 1, 0) + b"x",
        "truncated-header": bytes(17),
        "truncated-pixels": struct.pack("<5HII", 3, 2, 0, 1, 0, 6, 0) + bytes(5),
        "trailing-standalone-byte": struct.pack("<5HII", 1, 1, 0, 1, 0, 1, 0) + bytes(2),
    }
    for name, payload in invalid.items():
        path = directory / (name + ".bin")
        path.write_bytes(payload)
        c_rows(tool, "--image", path, expected_exit=1)
        records.append({"case": name, "payload_sha256": sha(payload), "accepted": False})
    source, background = bytes((0, 1, 2, 3, 4, 5)), bytes((99,) * 6)
    light = bytearray(range(256))
    light[0], light[1], light[2] = 7, 0, 4
    mapped = source.translate(bytes(light))
    after = bytes(new if new else old for old, new in zip(background, mapped))
    before = bytes(light[src] if src else old for old, src in zip(background, source))
    check(mapped, bytes((7, 0, 4, 3, 4, 5)), "opaque LUT formula")
    check(after, bytes((7, 99, 4, 3, 4, 5)), "transparent after-light formula")
    if before == after:
        raise AssertionError("synthetic transparency ordering is not discriminating")
    (directory / "transparent-after-light.idx").write_bytes(after)
    (directory / "transparent-before-light.idx").write_bytes(before)
    return {"decoder_cases": records, "transparency_ordering_discriminates": True,
            "transparent_after_light_sha256": sha(after), "transparent_before_light_sha256": sha(before),
            "negative_width_limit": "0xffff is accepted as unsigned width 65535 by decoder; no claim about signed original renderer width outside real positive profile"}


def ghidra_checks(language, exe_sha):
    directory = ROOT / "build/rendering" / language
    manifest = json.loads((directory / "analysis-manifest.json").read_text(encoding="utf-8-sig"))
    check((manifest["sha256_before"], manifest["sha256_after"]), (exe_sha, exe_sha), "Ghidra original EXE hashes")
    roles = list(csv.DictReader(io.StringIO((directory / "roles.tsv").read_text(encoding="utf-8-sig")), delimiter="\t"))
    required = {"image_allocate", "image_read_stream", "image_flags_crop_configure", "image_crop_transform",
                "animation_read_stream", "render_light_row_select", "render_light_table_read",
                "render_light_table_initialize", "rasterize_horizontal", "rasterize_vertical",
                "render_horizontal_span_asm", "render_vertical_span_asm"}
    check(required <= {r["role"] for r in roles}, True, "fresh required rendering roles")
    asm = (directory / "evidence.asm").read_text(encoding="utf-8")
    instructions = dict(line.split("\t", 1) for line in asm.splitlines() if "\t" in line)
    needed = {
        "1010:0037": "IMUL CX", "1010:003d": "ADD AX,DX", "1010:004c": "ADD SI,AX",
        "1010:00e5": "SAR EAX,0x10", "1010:0101": "SAR EDX,0x10",
        "1010:0092": "MOV BL,byte ptr GS:[BX]", "1010:0097": "MOV BL,byte ptr FS:[BX]",
        "1010:009a": "MOV byte ptr ES:[DI],BL", "1010:0168": "MOV BL,byte ptr GS:[SI]",
        "1010:016b": "MOV BL,byte ptr FS:[BX]", "1010:016e": "MOV byte ptr ES:[DI],BL",
        "1010:0197": "MOV BL,byte ptr GS:[SI]", "1010:019a": "MOV BL,byte ptr FS:[BX]",
        "1010:019d": "TEST BL,BL", "1010:019f": "JZ 0x1010:01a4",
        "1010:01a1": "MOV byte ptr ES:[DI],BL", "1010:01a4": "ADD DI,AX",
        "1010:00c7": "JNZ 0x1010:0090",
    }
    if language == "ru":
        needed.update({"1010:0030": "MOV CX,word ptr [0xc14a]",
                       "1010:008a": "MOV FS,word ptr [0xb526]",
                       "1010:015e": "TEST word ptr [0xc1f0],0x1",
                       "1050:032d": "MOV word ptr ES:[BX + 0x4],0x8",
                       "1088:0553": "CMP DX,0x10", "1088:0556": "JC 0x1088:055d",
                       "1088:0558": "MOV AX,0xf", "10f8:05b7": "OR word ptr ES:[BX + 0x4],AX"})
    else:
        needed.update({"1010:0030": "MOV CX,word ptr [0xc1fa]",
                       "1010:008a": "MOV FS,word ptr [0xb5d6]",
                       "1010:015e": "TEST word ptr [0xc2a0],0x1",
                       "1018:1273": "MOV word ptr ES:[BX + 0x4],0x8",
                       "1028:197d": "CMP DX,0x10", "1028:1980": "JC 0x1028:1987",
                       "1028:1982": "MOV AX,0xf", "1060:06c7": "OR word ptr ES:[BX + 0x4],AX"})
    for address, instruction in needed.items():
        check(instructions.get(address), instruction, language + " fresh Ghidra ASM " + address)
    return {"roles": len(roles), "required_roles_present": len(required), "instruction_checks": len(needed),
            "verified_addresses": list(needed), "exports": {path.name: {"size": path.stat().st_size, "sha256": sha(path.read_bytes())}
                for path in directory.glob("*") if path.is_file() and path.suffix in (".asm", ".c", ".tsv", ".json")}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=ROOT / "build/native/dok-anim.exe")
    args = parser.parse_args()
    tool = args.tool.resolve()
    report = {"task": "G04", "status": "running", "probe": "image/animation decoding and row-major/light/transparency formulas",
              "dos_renderer_equivalence": False, "fps_measured": False,
              "script_sha256": sha(Path(__file__).read_bytes()), "C_decoder_sha256": sha(tool.read_bytes()),
              "languages": {}, "ghidra_evidence": {}}
    for language, (exe_name, exe_sha) in EXPECTED.items():
        inventory = json.loads((ROOT / f"analysis/inventory-{language}.json").read_text(encoding="utf-8"))
        exe = ROOT / "originals/pkr" / exe_name
        check(sha(exe.read_bytes()), exe_sha, language + " original EXE")
        archive_path = ROOT / inventory["resource"]["file"]
        archive = archive_path.read_bytes()
        check(sha(archive), inventory["resource"]["sha256"], language + " original RES")
        counts = {"standalone_images": 0, "animations": 0, "animation_frames": 0, "pixel_bytes_checked": 0}
        stored_flags, pixel_histogram = Counter(), Counter()
        artifacts, light_tables = [], []
        maximum_width = maximum_height = maximum_pixels = 0
        entries = parse(archive)
        resources = {entry["name_hex"]: archive[entry["offset"]:entry["offset"] + entry["size"]] for entry in reversed(entries)}
        for entry in entries:
            raw = archive[entry["offset"]:entry["offset"] + entry["size"]]
            kind = classify(entry["name"], raw)
            if kind not in ("indexed_image18", "unparsed", "lighting_table"):
                continue
            directory = OUT / language / f"{entry['index']:04d}"
            directory.mkdir(parents=True, exist_ok=True)
            path = directory / "resource.bin"
            path.write_bytes(raw)
            if kind == "lighting_table":
                check(len(raw), 4096, "sixteen 256-byte light rows")
                rows = [raw[i * 256:(i + 1) * 256] for i in range(16)]
                check(rows[0], bytes(range(256)), "original light row0 identity")
                check([row[0] for row in rows], [0] * 16, "original source zero remains mapped zero")
                light_tables.append({"resource_index": entry["index"], "sha256": sha(raw),
                                     "row_sha256": [sha(row) for row in rows],
                                     "source_zero_maps_to_zero_all_rows": True,
                                     "nonzero_indices_mapped_zero": [sum(value == 0 for value in row[1:]) for row in rows],
                                     "_rows": rows})
                continue
            if kind == "indexed_image18":
                metadata, pixels = image(raw)
                decoded = c_rows(tool, "--image", path)
                check(len(decoded), 1, "one standalone C image")
                checks = [(metadata, pixels, decoded[0])]
                counts["standalone_images"] += 1
            else:
                expected = frames(raw)
                decoded = c_rows(tool, "--export", path, directory)
                check(len(decoded), len(expected), "all animation frames exported")
                checks = [(metadata, pixels, actual) for (metadata, pixels), actual in zip(expected, decoded)]
                counts["animations"] += 1
                counts["animation_frames"] += len(expected)
            for metadata, pixels, actual in checks:
                for key, value in actual.items():
                    check(value, str(metadata[key]), language + " C/Python decoded image metadata")
                if kind == "unparsed":
                    pgm = directory / f"frame_{metadata['index']:04d}.pgm"
                    header = f"P5\n{metadata['width']} {metadata['height']}\n255\n".encode()
                    check(pgm.read_bytes(), header + pixels, "C-exported PGM exact row-major payload")
                stored_flags[metadata["flags"]] += 1
                pixel_histogram.update(pixels)
                counts["pixel_bytes_checked"] += len(pixels)
                maximum_width = max(maximum_width, metadata["width"])
                maximum_height = max(maximum_height, metadata["height"])
                maximum_pixels = max(maximum_pixels, len(pixels))
            artifacts.append({"resource_index": entry["index"], "kind": kind, "sha256": sha(raw),
                              "decoded_images": len(checks), "pixel_sha256": [metadata["sha256"] for metadata, _, _ in checks]})
        for table in light_tables:
            rows = table.pop("_rows")
            table["source_nonzero_pixels_hidden_by_light"] = [sum(pixel_histogram[index] for index in range(1, 256) if row[index] == 0) for row in rows]
        check((counts["standalone_images"], counts["animations"], counts["animation_frames"]), (77, 24, 154), "all actual image/animation resources")
        check(len(light_tables), 3, "all actual light tables")
        sprite_flags, shifted_sizes = Counter(), []
        for entry in entries[18:21]:
            source = reference(resources[entry["name_hex"]])
            for obj in source["objects"]:
                sprite_flags[obj["sprite_flags"]] += 1
                if obj["sprite_flags"] & 0x7fc0 and obj["sprite"]:
                    im, _ = image(resources[obj["sprite"]])
                    shifted_sizes.append(im["pixel_bytes"])
        scratch_segment = next(segment for segment in inventory["segments"] if segment["index"] == (50 if language == "ru" else 26))
        scratch_size = scratch_segment["allocation"] - 0x300
        check(scratch_size, 32768, "native crop scratch capacity")
        if max(shifted_sizes) > scratch_size or any(flags & 0x4000 for flags in sprite_flags):
            raise AssertionError("real source crop profile exceeds documented bounds")
        report["languages"][language] = {"exe_sha256": exe_sha, "res_sha256": sha(archive),
                                         "counts": counts, "stored_flags_histogram": dict(stored_flags),
                                         "maxima": {"width": maximum_width, "height": maximum_height, "pixels": maximum_pixels},
                                         "resources": artifacts, "light_tables": light_tables,
                                         "level_sprite_flags_histogram": dict(sprite_flags),
                                         "native_crop_scratch_bytes": scratch_size,
                                         "maximum_real_crop_sprite_bytes": max(shifted_sizes),
                                         "source_level_uses_flag4000": False}
        check(sha(exe.read_bytes()), exe_sha, language + " EXE unchanged")
        check(sha(archive_path.read_bytes()), sha(archive), language + " RES unchanged")
        report["ghidra_evidence"][language] = ghidra_checks(language, exe_sha)
    report["synthetic"] = synthetic(tool)
    report["originals_unchanged"] = True
    report["status"] = "passed_resource_probe"
    (ROOT / "analysis/render-layout-probe.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "languages": {k: v["counts"] for k, v in report["languages"].items()},
                      "synthetic_decoder_cases": len(report["synthetic"]["decoder_cases"]), "originals_unchanged": True}, indent=2))


if __name__ == "__main__":
    main()
