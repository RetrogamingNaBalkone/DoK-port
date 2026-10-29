#!/usr/bin/env python3
"""Independent N04 RES-to-package oracle; no converter/runtime imports.

Full game payloads and previews stay in ignored build/research directories.
The oracle checks every representation byte, not just converter manifests.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[3]
KINDS = {"image": 1, "animation": 2, "font": 3, "palette": 4,
         "light": 5, "level": 6, "text": 7, "pcm": 8}
STATIC = {"BSPR1": "GAMEP1", "ESPR1": "GAMEP1", "BSPR2": "GAMEP2",
          "ESPR2": "GAMEP2", "BSPR3": "GAMEP3", "ESPR3": "GAMEP3",
          "ЗАСТАВКА_СПР": "ЗАСТАВКА_ПАЛ"}
REP_NAMES = (None, "RAW_SOURCE", "IMAGE_INDEX8", "ANIM_META", "FRAME_INDEX8",
             "GPU_INDEX8_POT", "STATIC_INDEX8", "PALETTE_RGB555", "PCM_CLIP",
             "PCM_CHUNK", "TEXT_UTF8", "FONT_BITMAP", "LIGHT_TABLE", "LEVEL_MAPS",
             "LEVEL_CP866", "STATIC_GPU_INDEX8", "TEXT_CP866")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def asset_id(lang: int, index: int, rep: int, part: int = 0) -> int:
    return lang << 30 | index << 18 | rep << 10 | part


def originals() -> dict:
    result = {}
    for lang, label in ((1, "ru"), (2, "en")):
        inv = json.loads((ROOT / f"analysis/originals/inventory-{label}.json").read_text(encoding="utf-8"))
        path = ROOT / inv["resource"]["file"]
        data = path.read_bytes()
        require(sha(data) == inv["resource"]["sha256"], f"Original SHA changed: {path}")
        count = int.from_bytes(data[:2], "little")
        require(count == 146 and 2 + count * 30 <= len(data), "RES directory profile")
        entries = []
        cursor = 2 + count * 30
        for index in range(count):
            record = data[2 + index * 30:32 + index * 30]
            name_bytes = record[:16].split(b"\0", 1)[0]
            require(0 < len(name_bytes) < 16, "Original resource name")
            size, offset = struct.unpack_from("<II", record, 16)
            require(offset == cursor and size <= len(data) - offset, "RES independent boundary/contiguity")
            block = data[offset:offset + size]
            name = name_bytes.decode("cp866")
            if name in ("FONT8", "FONT16"):
                kind = "font"
            elif name.startswith("LEVEL"):
                kind = "level"
            elif name.startswith("LIGHT"):
                kind = "light"
            elif size == 768 and max(block) <= 63:
                kind = "palette"
            elif block[2:5] == b"SVM":
                kind = "pcm"
            elif name in ("README", "ENDTXT", "PANEL_DAT") or name.startswith(("BINTRO", "EINTRO")):
                kind = "text"
            elif size >= 18 and 18 + struct.unpack_from("<H", block)[0] * struct.unpack_from("<H", block, 2)[0] == size:
                kind = "image"
            else:
                kind = "animation"
            entries.append(dict(index=index, name=name, name_bytes=name_bytes, offset=offset,
                                size=size, record=record, data=block, kind=kind))
            cursor += size
        require(len(data) - cursor == (1 if lang == 1 else 4), "RES trailing-byte profile")
        kinds = collections.Counter(e["kind"] for e in entries)
        require(kinds == {"image": 77, "animation": 24, "font": 2, "palette": 5,
                          "light": 3, "level": 3, "text": 9, "pcm": 23}, "RES category completeness")
        result[lang] = dict(path=path, data=data, entries=entries, trailing=data[cursor:])
    return result


def decode_image(block: bytes, offset: int = 0) -> tuple:
    require(offset + 18 <= len(block), "Original image header")
    width, height, flags = struct.unpack_from("<HHH", block, offset)
    size = struct.unpack_from("<I", block, offset + 10)[0]
    require(width > 0 and height > 0 and size == width * height and size <= len(block) - offset - 18,
            "Original image pixel bounds")
    return width, height, flags, block[offset + 18:offset + 18 + size], offset + 18 + size


def level_maps(block: bytes) -> tuple:
    sections = collections.defaultdict(list)
    current = None
    for line in block.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n"):
        if line.startswith(b"[") and line.endswith(b"]"):
            current = line[1:-1]
        elif current is not None:
            sections[current].append(line)
    settings = {}
    for line in sections[b"Game Ini"]:
        if b"=" in line and not line.startswith(b"#"):
            key, value = line.split(b"=", 1)
            settings[key.strip()] = int(value.strip())
    w, length, depth = (settings[k] for k in (b"Level Width", b"Level Length", b"Level Height"))
    side, height = settings[b"Cell Side"], settings[b"Cell Height"]
    require((w, length, depth, side, height) == (64, 64, 1, 480, 960), "Original LEVEL geometry")
    planes = []
    for key in (b"MMap", b"TMap", b"FCMap"):
        rows = [line for line in sections[key] if line and not line.startswith(b"#")]
        require(len(rows) == length and all(len(row) == w for row in rows), "Original map whitespace geometry")
        planes.append(b"".join(rows))
    return w, length, side, height, struct.pack("<HHHHIII", w, length, depth, 3, side, height, 0) + b"".join(planes)


def pot(value: int) -> int:
    return 1 << max(3, (value - 1).bit_length())


def padded(pixels: bytes, w: int, h: int, sw: int, sh: int) -> bytes:
    return b"".join(pixels[y*w:(y+1)*w] + bytes(sw-w) if y < h else bytes(sw) for y in range(sh))


def expected_assets(sources: dict) -> dict:
    expected = {}
    for lang, source in sources.items():
        names = {e["name"]: e for e in source["entries"]}
        for entry in source["entries"]:
            index, block, kind = entry["index"], entry["data"], entry["kind"]

            def put(rep: int, data: bytes, *, part: int = 0, sub: int = 65535,
                    width: int = 0, height: int = 0, sw: int = 0, sh: int = 0,
                    p0: int = 0, p1: int = 0) -> None:
                ident = asset_id(lang, index, rep, part)
                require(ident not in expected, "Independent duplicate semantic ID")
                expected[ident] = dict(id=ident, source_id=asset_id(lang, index, 1), rep=rep, lang=lang,
                                      kind=KINDS[kind], index=index, sub=sub, width=width, height=height,
                                      sw=sw, sh=sh, p0=p0, p1=p1, data=data,
                                      source_sha=sha(block), name=entry["name_bytes"].ljust(16, b"\0"))

            put(1, block)
            if kind == "image":
                w, h, flags, pixels, end = decode_image(block)
                require(end == len(block), "Standalone image exact extent")
                put(2, pixels, width=w, height=h, sw=w, sh=h, p0=flags)
                put(5, padded(pixels, w, h, pot(w), pot(h)), width=w, height=h, sw=pot(w), sh=pot(h), p0=flags)
                if entry["name"] in STATIC:
                    require((w, h) == (320, 200), "DOS static source geometry")
                    target = bytes(pixels[((2*y+1)*200//384)*320 + ((2*x+1)*320//512)]
                                   for y in range(192) for x in range(256))
                    palette_id = asset_id(lang, names[STATIC[entry["name"]]]["index"], 7)
                    put(6, target, width=256, height=192, sw=256, sh=192, p0=palette_id, p1=1)
                    put(15, target + bytes(256*64), width=256, height=192, sw=256, sh=256, p0=palette_id, p1=1)
            elif kind == "animation":
                count, unknown = struct.unpack_from("<HH", block)
                require(0 < count < 32768 and 8 + count*10 <= len(block), "Original animation table")
                meta = bytearray(struct.pack("<HHI", count, unknown, 0))
                cursor = 8 + count*10
                for frame in range(count):
                    ticks, a, b = struct.unpack_from("<HHH", block, 8 + frame*10)
                    w, h, flags, pixels, cursor = decode_image(block, cursor)
                    meta += struct.pack("<IHHHHHH", asset_id(lang, index, 4, frame), ticks, a, b, flags, w, h)
                    put(4, pixels, part=frame, sub=frame, width=w, height=h, sw=w, sh=h, p0=flags, p1=8)
                    put(5, padded(pixels, w, h, pot(w), pot(h)), part=frame, sub=frame,
                        width=w, height=h, sw=pot(w), sh=pot(h), p0=flags, p1=8)
                require(cursor == len(block), "Animation exact source end")
                put(3, bytes(meta))
            elif kind == "palette":
                rgb = b"".join(struct.pack("<H", (r >> 1) | ((g >> 1) << 5) | ((b >> 1) << 10))
                               for r, g, b in zip(block[::3], block[1::3], block[2::3]))
                put(7, rgb)
            elif kind == "pcm":
                require(len(block) >= 36 and block[2:5] == b"SVM", "Original PCM header")
                require(struct.unpack_from("<HBHH", block, 5) == (1, 9, 8, 0), "PCM supported original profile")
                c, d, rate, samples = struct.unpack_from("<HHII", block, 12)
                u28, u32 = struct.unpack_from("<II", block, 28)
                require(0 < rate <= 65535 and len(block) == 36 + samples, "PCM exact source sample range")
                chunks = (samples + 65535) // 65536
                meta = bytearray(struct.pack("<IIIHHII", rate, samples, chunks, c, d, u28, u32))
                for part in range(chunks):
                    start = part * 65536
                    pixels = bytes(value ^ 128 for value in block[36+start:36+min(samples, start+65536)])
                    put(9, pixels, part=part, sub=part, p0=start, p1=rate)
                    meta += struct.pack("<III", asset_id(lang, index, 9, part), start, len(pixels))
                put(8, bytes(meta), p0=rate, p1=samples)
            elif kind == "text":
                put(10, block.decode("cp866").encode("utf-8"), p0=65001)
                put(16, block, p0=866)
            elif kind == "font":
                glyph_h = len(block) // 256
                require(glyph_h in (8, 16) and len(block) == glyph_h*256, "FONT source profile")
                put(11, block, width=8, height=glyph_h, sw=8, sh=glyph_h, p0=256, p1=1)
            elif kind == "light":
                require(len(block) == 4096, "LIGHT source extent")
                put(12, block, width=256, height=16, sw=256, sh=16)
            elif kind == "level":
                w, h, side, height, maps = level_maps(block)
                put(13, maps, width=w, height=h, sw=w, sh=h, p0=side, p1=height)
                put(14, block, p0=866)
    return expected


def check_package(data: bytes, sources: dict, expected: dict) -> dict:
    require(len(data) >= 160 and data[:8] == b"DOKNDS1\0", "Package header/magic")
    version, hb, eb, flags, count, do, payload_start, file_size = struct.unpack_from("<HHHHIIII", data, 8)
    require((version, hb, eb, flags, do) == (1, 160, 128, 0, 160), "Header format")
    require(0 < count <= 2048 and count == len(expected), "Full semantic asset count")
    require(payload_start == 160 + count*128 and file_size == len(data), "Header sizes")
    require(sha(data[:128]) == data[128:160].hex(), "Header SHA")
    directory = data[160:payload_start]
    require(sha(directory) == data[32:64].hex(), "Directory SHA")
    require(data[64:96].hex() == sha(sources[1]["data"]) and data[96:128].hex() == sha(sources[2]["data"]), "Source archive SHA")
    intervals = {}; seen_payloads = {}; last_id = 0; cursor = payload_start
    counts = collections.Counter(); physical_bytes = 0; aliases = 0
    for ordinal in range(count):
        record = directory[ordinal*128:(ordinal+1)*128]
        ident, source_id, offset, size = struct.unpack_from("<IIII", record)
        rep, lang, kind, index, sub, w, h, sw, sh, p0, p1 = struct.unpack_from("<HBBHHHHHHII", record, 16)
        require(ident > last_id and ident in expected, f"ID ordering/coverage {ident:08x}")
        last_id = ident
        exp = expected[ident]
        actual_meta = (source_id, rep, lang, kind, index, sub, w, h, sw, sh, p0, p1)
        target_meta = tuple(exp[k] for k in ("source_id", "rep", "lang", "kind", "index", "sub", "width", "height", "sw", "sh", "p0", "p1"))
        require(actual_meta == target_meta, f"Semantic metadata {ident:08x}: {actual_meta} != {target_meta}")
        require(record[72:104].hex() == exp["source_sha"] and record[104:120] == exp["name"] and record[120:] == bytes(8), f"Source identity/reserved {ident:08x}")
        require(offset % 32 == 0 and offset >= payload_start and offset <= len(data) and size <= len(data)-offset, "Payload bounds/alignment")
        payload = data[offset:offset+size]
        require(size == len(exp["data"]) and payload == exp["data"], f"Full original-derived payload differs {ident:08x}")
        digest = sha(payload)
        require(record[40:72].hex() == digest, "Payload SHA")
        key = (digest, payload)
        if key in seen_payloads:
            require((offset, size) == seen_payloads[key], "Identical payload not physically deduplicated")
            aliases += 1
        else:
            aligned = (cursor + 31) & ~31
            require(offset == aligned and data[cursor:offset] == bytes(offset-cursor), "First-ID placement/zero padding")
            for previous_offset, previous_size in intervals:
                require(offset >= previous_offset+previous_size or offset+size <= previous_offset, "Partial payload overlap")
            intervals[(offset, size)] = digest
            seen_payloads[key] = (offset, size)
            cursor = offset+size; physical_bytes += size
        counts[str(rep)] += 1
    require(cursor == len(data), "Unindexed trailing package bytes")
    require(counts["1"] == 292 and counts["2"] == 154 and counts["4"] == 308 and counts["6"] == 14,
            "Original category completeness")
    return dict(passed=True, entry_count=count, representation_counts=dict(sorted(counts.items(), key=lambda p:int(p[0]))),
                unique_payloads=len(intervals), aliased_entries=aliases, unique_payload_bytes=physical_bytes,
                largest_payload_bytes=max(len(e["data"]) for e in expected.values()),
                largest_gpu_bytes=max(len(e["data"]) for e in expected.values() if e["rep"] == 5),
                original_blocks=292, standalone_images=154, animation_frames=308,
                max_pcm_sample_count=max(e["p1"] for e in expected.values() if e["rep"] == 8),
                directory_workspace_range_bytes=2048*40, directory_io_ceiling_bytes=128*1024)


def check_provenance(path: Path, sources: dict, expected: dict, package: bytes) -> dict:
    manifest=json.loads(path.read_text(encoding="utf-8"))
    require(manifest.get("schema_version")==1, "Provenance schema")
    archives=manifest.get("source_archives",[])
    require(len(archives)==2 and {v["language"] for v in archives}=={"ru","en"},"Provenance source coverage")
    for archive in archives:
        source=sources[1 if archive["language"]=="ru" else 2]
        require(archive["sha256"]==sha(source["data"]) and archive["bytes"]==len(source["data"]) and
                archive["trailing_hex"]==source["trailing"].hex(),"Provenance archive identity")
        require(Path(archive["path"]).as_posix()==source["path"].relative_to(ROOT).as_posix(),"Provenance source path")
        rows=archive["entries"]
        require(len(rows)==146,"Provenance all original entries")
        for row,original in zip(rows,source["entries"]):
            require(row["index"]==original["index"] and row["name"]==original["name"] and
                    row["name_hex"]==original["name_bytes"].hex() and row["offset"]==original["offset"] and
                    row["bytes"]==original["size"] and row["sha256"]==sha(original["data"]) and
                    row["record_hex"]==original["record"].hex(),"Provenance original record/bytes")
            # Names of source kinds belong to the converter's public manifest;
            # accept its documented upper-case names, but verify their meaning.
            require(row["source_kind"] in (original["kind"], original["kind"].upper()),"Provenance original kind")
    variants=manifest.get("variants",[])
    require(len(variants)==len(expected),"Provenance derivative coverage")
    seen=set()
    for row in variants:
        ident=row["id"]
        require(ident in expected and ident not in seen,"Provenance variant identity")
        seen.add(ident); exp=expected[ident]
        require((row["source_id"],row["representation"],row["language"],row["source_index"],row["subindex"],row["name"],row["source_sha256"],row["payload_sha256"],row["bytes"]) ==
                (exp["source_id"],REP_NAMES[exp["rep"]],exp["lang"],exp["index"],exp["sub"],exp["name"].split(b"\0",1)[0].decode("cp866"),exp["source_sha"],sha(exp["data"]),len(exp["data"])),"Provenance semantic variant")
        record=package[160+sorted(expected).index(ident)*128:160+(sorted(expected).index(ident)+1)*128]
        require(row["offset"]==struct.unpack_from("<I",record,8)[0],"Provenance physical payload offset")
    return dict(passed=True,path=str(path.relative_to(ROOT)).replace("\\","/"),sha256=sha(path.read_bytes()),source_records=292,variant_records=len(variants))


def png(path: Path, width: int, height: int, pixels: bytes, description: str) -> None:
    require(len(pixels) == width*height*3, "PNG dimensions")
    def chunk(tag: bytes, payload: bytes) -> bytes:
        return struct.pack(">I",len(payload))+tag+payload+struct.pack(">I",zlib.crc32(tag+payload)&0xffffffff)
    scan = b"".join(b"\0"+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,2,0,0,0))+
                     chunk(b"tEXt",b"Description\0"+description.encode("latin1", "replace"))+
                     chunk(b"IDAT",zlib.compress(scan,9))+chunk(b"IEND",b""))


def visual_oracles(sources: dict, expected: dict, out: Path) -> list:
    out.mkdir(parents=True, exist_ok=True)
    results = []
    def pair(left: bytes, right: bytes) -> bytes:
        return b"".join(left[y*320*3:(y+1)*320*3] + bytes(8*3) +
                        (right[(y-24)*256*3:(y-23)*256*3] if 24 <= y < 216 else bytes(256*3))
                        for y in range(240))
    for lang, source in sources.items():
        names={e["name"]:e for e in source["entries"]}
        for name, palette in STATIC.items():
            e=names[name]; pal=names[palette]["data"]; original=decode_image(e["data"])[3]
            target=expected[asset_id(lang,e["index"],6)]["data"]
            colors=[bytes(v*255//63 for v in pal[i:i+3]) for i in range(0,768,3)]
            colors5=[bytes((v>>1)*255//31 for v in pal[i:i+3]) for i in range(0,768,3)]
            left=b"".join(colors[original[((2*y+1)*200//480)*320+x]] for y in range(240) for x in range(320))
            right=b"".join(colors5[index] for index in target)
            path=out/f"lang{lang}-resource{e['index']:03d}-aspect-pair.png"
            png(path,584,240,pair(left,right),"Left DOS 4:3 320x240; right target 256x192 centered. VGA6 vs RGB555 color quantization.")
            results.append(dict(path=str(path.relative_to(ROOT)).replace("\\","/"), source=name, language=lang, sha256=sha(path.read_bytes())))
    # An anamorphic source circle becomes round under the documented DOS aspect.
    raw=bytearray()
    for y in range(200):
        for x in range(320):
            ellipse=((x-159.5)/90)**2+((y-99.5)/75)**2
            color=(255,230,40) if abs(ellipse-1)<.035 else ((220,220,220) if ((x//20+y//(50/3))%2)<1 else (35,45,65))
            raw.extend(color)
    left=b"".join(raw[(((2*y+1)*200//480)*320+x)*3:(((2*y+1)*200//480)*320+x)*3+3] for y in range(240) for x in range(320))
    right=b"".join(raw[(((2*y+1)*200//384)*320+((2*x+1)*320//512))*3:(((2*y+1)*200//384)*320+((2*x+1)*320//512))*3+3] for y in range(192) for x in range(256))
    path=out/"aspect-control-circle.png"
    png(path,584,240,pair(left,right),"Both circles and grid cells should appear square/round. Left corrected DOS, right target.")
    results.append(dict(path=str(path.relative_to(ROOT)).replace("\\","/"), source="synthetic DOS-aspect circle/grid", sha256=sha(path.read_bytes())))
    return results


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=ROOT/"build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--report", type=Path, default=ROOT/"research/evidence/nds/n04/audit-report.json")
    parser.add_argument("--rebuild-twice", action="store_true", help="Run converter twice, compare the entire package; keep only current package")
    parser.add_argument("--no-previews", action="store_true")
    parser.add_argument("--provenance", type=Path, default=ROOT/"research/evidence/nds/n04/provenance.json")
    args=parser.parse_args()
    report=dict(schema_version=1,task="N04",passed=False,python=sys.version,commands=[],limitations=[
        "Exact resource derivation does not prove renderer/gameplay equivalence or physical DS performance.",
        "Visual PNGs require a separate human/agent view; generation alone is not visual acceptance."])
    args.report.parent.mkdir(parents=True,exist_ok=True)
    try:
        sources=originals(); expected=expected_assets(sources)
        report["originals_before"]=[dict(language=lang,path=str(s["path"].relative_to(ROOT)).replace("\\","/"),sha256=sha(s["data"]),bytes=len(s["data"]),trailing_hex=s["trailing"].hex()) for lang,s in sources.items()]
        if args.rebuild_twice:
            snapshots=[]
            converter=ROOT/"ports/nds/tools/pack_assets.py"
            converter_sha=sha(converter.read_bytes())
            for _ in range(2):
                command=[sys.executable,"-X","utf8",str(ROOT/"ports/nds/tools/pack_assets.py"),"--output",str(args.package),"--report",str(ROOT/"research/evidence/nds/n04/converter-report.json")]
                completed=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,encoding="utf-8",errors="replace",check=False)
                report["commands"].append(dict(argv=command,exit_code=completed.returncode,stdout=completed.stdout,stderr=completed.stderr))
                require(completed.returncode==0,"Converter command failed")
                require(sha(converter.read_bytes())==converter_sha,"Converter changed during reproducibility check")
                snapshots.append(args.package.read_bytes())
            require(snapshots[0]==snapshots[1],"Two full package conversions differ")
            report["reproducibility"]=dict(passed=True,full_byte_compare=True,converter_sha256=converter_sha,
                first_sha256=sha(snapshots[0]),second_sha256=sha(snapshots[1]),bytes=len(snapshots[0]))
        data=args.package.read_bytes()
        report["package"]=dict(path=str(args.package.resolve().relative_to(ROOT)).replace("\\","/"),bytes=len(data),sha256=sha(data))
        report["verification"]=check_package(data,sources,expected)
        report["provenance"]=check_provenance(args.provenance,sources,expected,data)
        if not args.no_previews:
            report["visual_oracles"]=visual_oracles(sources,expected,ROOT/"build/nds/assets/oracle-previews")
        report["sources"]=[dict(path=str(p.relative_to(ROOT)).replace("\\","/"),sha256=sha(p.read_bytes()),bytes=p.stat().st_size) for p in (Path(__file__),ROOT/"ports/nds/tools/pack_assets.py",ROOT/"docs/nds/spec/N04-assets.md")]
        report["python_executable"]=dict(path=sys.executable,sha256=sha(Path(sys.executable).read_bytes()))
        report["originals_after"]=[dict(language=lang,sha256=sha(s["path"].read_bytes())) for lang,s in sources.items()]
        require(all(a["sha256"]==b["sha256"] for a,b in zip(report["originals_before"],report["originals_after"])),"Original changed during audit")
        report["passed"]=True
    except Exception as exc:
        report["error"]=f"{type(exc).__name__}: {exc}"
        print(report["error"],file=sys.stderr)
    finally:
        args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(f"N04 independent audit passed={report['passed']}; report={args.report}")
    return 0 if report["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
