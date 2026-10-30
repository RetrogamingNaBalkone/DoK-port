"""N19 deterministic UI banks and complete original text/source catalog.

Original payloads remain in ignored build/research. Only NDS research parsers
are imported; neither GBA code nor GBA-adapted messages are inputs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "ports/nds/tools"))
from audit_world import originals, parse_level, T
from layout_ui import paginate

OUT = ROOT / "build/nds/n19"
EVIDENCE = ROOT / "research/evidence/nds/n19"
STORIES = ("README", "BINTRO1", "EINTRO1", "BINTRO2", "EINTRO2", "BINTRO3", "EINTRO3", "ENDTXT")
DOOR_ROLES = ("opened", "closed", "copper", "iron", "gold", "secret", "no-key")
EXE_INFO = {
    1: ("ru", "originals/pkr/RUS/PKR.EXE", "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28", 51, "1190",
        (0x58c, 0x59d, 0x5ae, 0x5c1, 0x5d6, 0x5ea, 0x5fb)),
    2: ("en", "originals/pkr/ENG/PKE.EXE", "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b", 27, "10d0",
        (0x59c, 0x5a7, 0x5b4, 0x5ca, 0x5de, 0x5f4, 0x601)),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def checked_output(path, root):
    """Fixed local output trees, no symlink/junction/hardlink aliases."""
    root = Path(root)
    if root.resolve() != ROOT.resolve() / root.relative_to(ROOT):
        raise ValueError("Redirected fixed output tree")
    path = Path(path)
    if not path.resolve().is_relative_to(root.resolve()) or path.resolve() == root.resolve():
        raise ValueError("Output escapes fixed tree")
    for ancestor in (path, *path.parents):
        if ancestor == root.parent:
            break
        if ancestor.is_symlink() or ancestor.is_junction():
            raise ValueError("Output path contains filesystem alias")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError("Output alias")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def save_json(path, value, root):
    checked_output(path, root).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf8")


def load_exports(language):
    label, sample, expected, index, segment, offsets = EXE_INFO[language]
    directory = EVIDENCE / "original" / label
    manifest = json.loads((directory / "analysis-manifest.json").read_text(encoding="utf-8-sig"))
    exe = (ROOT / sample).read_bytes()
    if sha(exe) != expected or manifest["sha256_before"] != expected or manifest["sha256_after"] != expected:
        raise ValueError("Original EXE source SHA mismatch")
    if sha((ROOT / manifest["script"]).read_bytes()) != manifest["script_sha256"]:
        raise ValueError("Ghidra exporter changed")
    for name, expected_file_sha in manifest["exports"].items():
        if sha((directory / name).read_bytes()) != expected_file_sha:
            raise ValueError("Ghidra export changed")
    exported = (directory / f"{segment}.bin").read_bytes()
    ne = struct.unpack_from("<I", exe, 0x3c)[0]
    if exe[ne:ne + 2] != b"NE":
        raise ValueError("Expected NE sample")
    table, shift = [struct.unpack_from("<H", exe, ne + n)[0] for n in (0x22, 0x32)]
    sector, segment_size = struct.unpack_from("<HH", exe, ne + table + (index - 1) * 8)
    start, count = sector << shift, segment_size or 65536
    if len(exported) < count:
        raise ValueError("Truncated Ghidra NE data segment")
    constants = []
    for role, offset in zip(DOOR_ROLES, offsets):
        end = exported.index(0, offset)
        text = exported[offset:end]
        # Ghidra applies NE relocations to pointer slots elsewhere in this block;
        # only the exact immutable constant spans are compared with raw EXE.
        if exe[start + offset:start + end + 1] != text + b"\0":
            raise ValueError("Ghidra text constant differs from original file bytes")
        constants.append(dict(language=language, source="native-door", role=role, ghidra_address=f"{segment}:{offset:04X}",
            ne_segment_index=index, file_offset=start + offset, bytes=len(text), cp866_hex=text.hex(),
            text=text.decode("cp866"), sha256=sha(text), plan=paginate(text, 28, 1)))
    return constants, dict(sample=sample, sha256_before=sha(exe), sha256_after=sha((ROOT / sample).read_bytes()),
        ne_segment_index=index, ne_data_segment_file_offset=start, ghidra_segment=segment,
        project=manifest["project"], analysis_manifest_sha256=sha((directory / "analysis-manifest.json").read_bytes()),
        ghidra_exports=manifest["exports"], new_analysis=False, read_only_ghidra_export=True)


def scaled(entry, width, height):
    data = entry["payload"]
    sw, sh, flags, divisor, crop, size, pointer = struct.unpack_from("<5H2I", data)
    if len(data) != 18 + sw * sh or size != sw * sh:
        raise ValueError("Source UI image geometry")
    pixels = data[18:]
    return bytes(pixels[((2 * y + 1) * sh // (2 * height)) * sw + ((2 * x + 1) * sw // (2 * width))]
                 for y in range(height) for x in range(width))


def make_bank(resources):
    bank = bytearray(8192)
    chunks, cursor = [], 0
    for name in ("FONT8", "FONT16"):
        entry = resources[name]
        payload = entry["payload"]
        expected = 2048 if name == "FONT8" else 4096
        if len(payload) != expected:
            raise ValueError("Source CP866 font length")
        bank[cursor:cursor + expected] = payload
        chunks.append(dict(name=name, offset=cursor, bytes=expected, sha256=sha(payload), source_index=entry["index"]))
        cursor += expected
    for name, width, height in (("ПЖИЗНЬ", 17, 14), ("ПМАГИЯ", 13, 16),
                               ("ПКЛЮЧМ", 10, 14), ("ПКЛЮЧЖ", 10, 14), ("ПКЛЮЧЗ", 10, 14)):
        payload = scaled(resources[name], width, height)
        bank[cursor:cursor + len(payload)] = payload
        chunks.append(dict(name=name, offset=cursor, bytes=len(payload), width=width, height=height,
                           sha256=sha(payload), source_index=resources[name]["index"]))
        cursor += len(payload)
    panel = scaled(resources["ПАНЕЛЬ"], 256, 38)
    border = bytearray()
    for y in (0, 36, 37):
        border.extend(panel[y * 256:(y + 1) * 256])
    for y in range(1, 36):
        border.extend(panel[y * 256:y * 256 + 3])
        border.extend(panel[y * 256 + 253:y * 256 + 256])
    if (cursor, len(border)) != (7010, 978):
        raise AssertionError("N19 fixed bank offsets")
    bank[cursor:cursor + len(border)] = border
    chunks.append(dict(name="ПАНЕЛЬ-border", offset=cursor, bytes=len(border), sha256=sha(border),
                       source_index=resources["ПАНЕЛЬ"]["index"],
                       order="full rows 0,36,37 then rows1..35 side pixels x0,1,2,253,254,255"))
    return bytes(bank), chunks


def c_array(data):
    return "{" + ",".join(f"0x{v:02x}" for v in data) + "}"


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    archives = originals()
    report = dict(task="N19", passed=False, messages=[], stories=[], icons=[], fonts=[], exe_sources=[], resource_sources=[], banks=[])
    banks, native = [], []
    for language, archive in archives.items():
        resources = {bytes.fromhex(e["name"]).decode("cp866"): e for e in archive["entries"]}
        report["resource_sources"].append(dict(language=language, sample=archive["relative"],
            sha256_before=archive["sha256"], sha256_after=sha(archive["path"].read_bytes())))
        native_row, exe_source = load_exports(language)
        report["exe_sources"].append(exe_source)
        native.append(native_row)
        report["messages"].extend(native_row)
        for level in range(1, 4):
            entry = resources[f"LEVEL{level}"]
            model = parse_level(entry["payload"])
            for object_index, obj in enumerate(model["objects"]):
                for field in obj["fields"]:
                    if field["kind"] != T:
                        continue
                    span = field["value"]
                    text = bytes.fromhex(span["hex"])
                    report["messages"].append(dict(language=language, source="LEVEL", level=level, object_index=object_index,
                        glyph=obj["glyph"], class_id=obj["class_id"], native_offset=field["native_offset"],
                        resource_index=entry["index"], resource_offset=span["offset"], file_offset=entry["offset"] + span["offset"],
                        bytes=len(text), cp866_hex=text.hex(), text=text.decode("cp866"), sha256=sha(text), plan=paginate(text, 28, 1)))
        for name in STORIES:
            entry = resources[name]
            payload = entry["payload"]
            report["stories"].append(dict(language=language, name=name, resource_index=entry["index"],
                file_offset=entry["offset"], bytes=len(payload), cp866_hex=payload.hex(), text=payload.decode("cp866"),
                sha256=entry["sha256"], plan=paginate(payload, 30, 9)))
        for name in ("FONT8", "FONT16"):
            entry = resources[name]
            report["fonts"].append(dict(language=language, name=name, resource_index=entry["index"],
                file_offset=entry["offset"], bytes=entry["bytes"], sha256=entry["sha256"],
                glyphs=256, glyph_width=8, glyph_height=int(name[4:]), row_mask="MSB first"))
        for name in ("ПАНЕЛЬ", "ПЖИЗНЬ", "ПМАГИЯ", "ПКЛЮЧМ", "ПКЛЮЧЖ", "ПКЛЮЧЗ"):
            entry = resources[name]
            width, height = struct.unpack_from("<HH", entry["payload"])
            report["icons"].append(dict(language=language, name=name, resource_index=entry["index"],
                file_offset=entry["offset"], bytes=entry["bytes"], sha256=entry["sha256"], width=width, height=height))
        bank, chunks = make_bank(resources)
        banks.append(bank)
        report["banks"].append(dict(language=language, file_offset=32 + 8192 * (language - 1), bytes=len(bank), sha256=sha(bank), chunks=chunks))
    blob = b"N19UI01\0" + struct.pack("<6I", 1, 8192, 2, 2, 86, 0) + b"".join(banks)
    include = ["/* Generated by ports/nds/tools/ui/extract_ui.py; original bytes stay local. */",
               "static const unsigned char dok_ui_native_text[2][7][39] = {"]
    for row in native:
        include.append("{" + ",\n".join(c_array(bytes.fromhex(item["cp866_hex"])) for item in row) + "},")
    include.append("};")
    include.append("static const unsigned char dok_ui_native_text_lengths[2][7] = {" +
                   ",".join(c_array(bytes(item["bytes"] for item in row)) for row in native) + "};")
    include.append("static const unsigned char dok_ui_bank_sha256[2][32] = {" +
                   ",".join(c_array(hashlib.sha256(bank).digest()) for bank in banks) + "};")
    header = ("\n".join(include) + "\n").encode("ascii")
    destinations = [(OUT / "ui.dat", blob), (OUT / "generated/ui_native.inc", header)]
    for path, payload in destinations:
        checked_output(path, OUT)
    for path, payload in destinations:
        path.write_bytes(payload)
    report["outputs"] = [dict(path=p.relative_to(ROOT).as_posix(), bytes=len(data), sha256=sha(data)) for p, data in destinations]
    report["fonts_equal_between_languages"] = banks[0][:6144] == banks[1][:6144]
    report["all_banks_equal_between_languages"] = banks[0] == banks[1]
    report["counts"] = dict(messages=len(report["messages"]), level_messages=sum(m["source"] == "LEVEL" for m in report["messages"]),
        native_messages=14, max_message_bytes=max(m["bytes"] for m in report["messages"]),
        message_pages=sum(len(m["plan"]["pages"]) for m in report["messages"]), stories=len(report["stories"]),
        story_pages=sum(len(m["plan"]["pages"]) for m in report["stories"]), fonts=len(report["fonts"]), icons=len(report["icons"]))
    for source in report["exe_sources"]:
        if sha((ROOT / source["sample"]).read_bytes()) != source["sha256_before"]:
            raise ValueError("Original EXE changed")
    report["script_sha256"] = sha(Path(__file__).read_bytes())
    report["passed"] = True
    save_json(EVIDENCE / "ui-catalog.json", report, EVIDENCE)
    summary = {key: report[key] for key in ("task", "passed", "counts", "outputs", "fonts_equal_between_languages", "all_banks_equal_between_languages", "script_sha256")}
    summary["catalog"] = dict(path="research/evidence/nds/n19/ui-catalog.json", sha256=sha((EVIDENCE / "ui-catalog.json").read_bytes()))
    summary["source_sha256"] = {source["sample"]: source["sha256_before"] for source in report["resource_sources"] + report["exe_sources"]}
    save_json(ROOT / "analysis/nds-n19-ui-source.json", summary, ROOT / "analysis")
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == "__main__":
    main()
