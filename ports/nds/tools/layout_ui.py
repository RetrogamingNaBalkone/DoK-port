"""N06 offline, source-raster HUD/text layouts; not the N19 runtime.

Public canvases are 256*192 lists of RGB555 words with bit 15 clear.
save_frame emits an RGB PNG and opaque little-endian DS RGB555 words.
No GBA modules, system fonts, generated artwork or third-party modules.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[3]
PACKAGE = ROOT / "build/nds/assets/dok-assets.ndspak"
OUT = ROOT / "build/nds/n06/ui"
EVIDENCE = ROOT / "research/evidence/nds/n06"
PACKAGE_SHA = "2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511"
TEXT_NAMES = ("README", "BINTRO1", "EINTRO1", "BINTRO2", "EINTRO2", "BINTRO3", "EINTRO3", "ENDTXT")


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Assets:
    def __init__(self, path=PACKAGE):
        self.path = Path(path)
        data = self.path.read_bytes()
        if sha(data) != PACKAGE_SHA:
            raise ValueError("N06 requires the accepted N04 package")
        count = struct.unpack_from("<I", data, 16)[0]
        self.entries = {}
        for i in range(count):
            r = data[160 + 128*i:288 + 128*i]
            asset_id, source_id, offset, size = struct.unpack_from("<4I", r)
            rep, lang, kind, index, subindex = struct.unpack_from("<HBBHH", r, 16)
            w, h, sw, sh = struct.unpack_from("<4H", r, 24)
            p0, p1 = struct.unpack_from("<2I", r, 32)
            name = r[104:120].split(b"\0", 1)[0].decode("cp866")
            payload = data[offset:offset+size]
            if sha(payload) != r[40:72].hex():
                raise ValueError("Package payload SHA")
            self.entries[lang, name, rep, subindex] = dict(
                id=asset_id, source_id=source_id, source_index=index,
                representation=rep, language=lang, kind=kind, subindex=subindex,
                width=w, height=h, storage_width=sw, storage_height=sh,
                param0=p0, param1=p1, sha256=sha(payload), payload=payload)

    def get(self, language, name, representation, subindex=65535):
        return self.entries[language, name, representation, subindex]

    def palette(self, language, level=1):
        return list(struct.unpack("<256H", self.get(language, f"GAMEP{level}", 7)["payload"]))


def fill(canvas, x, y, width, height, color):
    if len(canvas) != 256*192 or not (0 <= x <= x+width <= 256 and 0 <= y <= y+height <= 192):
        raise ValueError("Raster rectangle outside screen")
    for row in range(y, y+height):
        canvas[row*256+x:row*256+x+width] = [color]*width


def text(canvas, assets, language, value, x, y, height=8, color=0x03e0):
    value = value.encode("cp866") if isinstance(value, str) else value
    font = assets.get(language, f"FONT{height}", 11)["payload"]
    if len(value)*8+x > 256 or y+height > 192 or x < 0 or y < 0:
        raise ValueError("Text does not fit; never clip or narrow glyphs")
    for col, code in enumerate(value):
        for gy in range(height):
            bits = font[code*height+gy]
            for gx in range(8):
                if bits & (128 >> gx):
                    canvas[(y+gy)*256+x+col*8+gx] = color


def icon(canvas, assets, language, name, x, y, width, height, palette):
    image = assets.get(language, name, 2)
    source_w, source_h = image["width"], image["height"]
    pixels = image["payload"]
    for dy in range(height):
        sy = (2*dy+1)*source_h//(2*height)
        for dx in range(width):
            sx = (2*dx+1)*source_w//(2*width)
            index = pixels[sy*source_w+sx]
            if index != 0:  # UI sprite transparency, independent of world LIGHT.
                canvas[(y+dy)*256+x+dx] = palette[index]


def paginate(data, columns=30, rows=9):
    """Lossless byte spans up to native NUL/$; glyph list=(offset,col,row)."""
    if columns < 1 or rows < 1:
        raise ValueError("Positive page geometry required")
    stop = next((i for i, code in enumerate(data) if code in (0, 36)), len(data))
    pages, offset = [], 0
    while offset < stop or not pages:
        start, row, col, glyphs = offset, 0, 0, []
        while offset < stop:
            code = data[offset]
            if code == 13:
                offset += 1
                continue
            if code == 10:
                offset += 1
                row += 1
                col = 0
                if row == rows:
                    break
                continue
            if col == columns:
                col, row = 0, row+1
            if code != 32 and col and (offset == 0 or data[offset-1] in (10,13,32)):
                word_end = offset
                while word_end < stop and data[word_end] not in (10,13,32):
                    word_end += 1
                word_length = word_end-offset
                if word_length <= columns and col+word_length > columns:
                    col, row = 0, row+1
            if row == rows:
                break
            glyphs.append([offset, col, row])
            offset, col = offset+1, col+1
        pages.append(dict(start=start, end=offset, glyphs=glyphs))
        if offset == stop:
            break
    consumed = b"".join(data[p["start"]:p["end"]] for p in pages)
    expected = [(i, c) for i, c in enumerate(data[:stop]) if c not in (10, 13)]
    actual = [(i, data[i]) for p in pages for i, _, _ in p["glyphs"]]
    if consumed != data[:stop] or actual != expected:
        raise AssertionError("Pagination lost, repeated or reordered source bytes")
    return dict(columns=columns, rows=rows, wrap="prefer-whole-word; preserve-space; hard-split-long-word",
                stop=stop, terminator=data[stop] if stop < len(data) else None,
                source_bytes=len(data), source_sha256=sha(data), suffix_bytes=len(data)-stop,
                glyph_count=len(expected), pages=pages)


def number(value):
    if not -32768 <= value <= 32767:
        raise ValueError("HUD state must supply a signed16 number")
    return str(value)


def draw_hud(canvas, assets, language=1, level=1, values=None, message=b"", page=0):
    """Modify y=154..191 only; return exact text/boxes and message page plan."""
    state = dict(health=100, charge=0, health_max=100, charge_max=100,
                 lives=3, score=0, keys=0, weapon=0)
    state.update(values or {})
    palette = assets.palette(language, level)
    panel = assets.get(language, "ПАНЕЛЬ", 2)
    # Aspect-correct source panel border/pixels, then clear original interior art.
    for dy in range(38):
        sy = (2*dy+1)*39//76
        for dx in range(256):
            sx = (2*dx+1)*320//512
            canvas[(154+dy)*256+dx] = palette[panel["payload"][sy*320+sx]]
    fill(canvas, 3, 155, 250, 35, palette[86])
    green = palette[2]
    icon(canvas, assets, language, "ПЖИЗНЬ", 4, 157, 17, 14, palette)
    icon(canvas, assets, language, "ПМАГИЯ", 82, 156, 13, 16, palette)
    nums = {}
    for key, x in (("health", 24), ("charge", 100)):
        value = number(state[key])
        text(canvas, assets, language, value, x+48-len(value)*8, 156, 16, green)
        nums[key] = dict(text=value, box=[x,156,48,16])
        maximum = state[key+"_max"]
        number(maximum)
        fill(canvas, x, 172, 48, 1, palette[0])
        extent = 0 if maximum <= 0 else min(48, max(0, state[key])*48//maximum)
        if extent:
            fill(canvas, x, 172, extent, 1, green)
    for bit, name, x in ((1,"ПКЛЮЧМ",156),(2,"ПКЛЮЧЖ",172),(4,"ПКЛЮЧЗ",188)):
        if state["keys"] & bit:
            icon(canvas, assets, language, name, x, 157, 10, 14, palette)
    if state["keys"] & ~7:
        raise ValueError("No invented icon for absent source key bits")
    if state["weapon"] not in (0,1,2,3):
        raise ValueError("Original weapon slots are 0..3 in this layout profile")
    weapon = ("О:" if language == 1 else "W:")+str(state["weapon"])
    text(canvas, assets, language, weapon, 220, 156, 16, green)
    secondary = ("П:" if language == 1 else "L:")+number(state["lives"])
    secondary += "  "+("С:" if language == 1 else "S:")+number(state["score"])
    text(canvas, assets, language, secondary, 4, 174, 8, green)
    plan = paginate(message, 28, 1)
    if not 0 <= page < len(plan["pages"]):
        raise ValueError("Message page outside plan")
    current = plan["pages"][page]
    for offset, col, row in current["glyphs"]:
        text(canvas, assets, language, bytes([message[offset]]), 4+col*8, 182, 8, green)
    if len(plan["pages"]) > 1:
        marker = f"{page+1}/{len(plan['pages'])}"
        if len(marker) > 3:
            raise ValueError("Message page count exceeds original-sample HUD marker profile")
        text(canvas, assets, language, marker, 228, 182, 8, green)
    return dict(values=state, numbers=nums, secondary=secondary, weapon=weapon,
                message_plan=plan, message_page=page, box=[0,154,256,38])


def render_text_page(assets, language, name, page=0):
    data = assets.get(language, name, 16)["payload"]
    plan = paginate(data)
    if not 0 <= page < len(plan["pages"]):
        raise ValueError("Text page outside plan")
    canvas = [0]*49152
    palette = assets.palette(language, 1)
    if language == 1:
        title = "Предыстория" if name == "README" else "До встречи!" if name == "ENDTXT" else "Начало" if name.startswith("BINTRO") else "Завершение"
    else:
        title = "Story" if name == "README" else "Farewell!" if name == "ENDTXT" else "Beginning" if name.startswith("BINTRO") else "Ending"
    text(canvas, assets, language, title, 8, 4, 16, palette[2])
    for offset, col, row in plan["pages"][page]["glyphs"]:
        text(canvas, assets, language, bytes([data[offset]]), 8+col*8, 24+row*16, 16, palette[2])
    footer = f"{page+1}/{len(plan['pages'])}  B:<  A:>"
    text(canvas, assets, language, footer, 8, 180, 8, palette[2])
    return canvas, plan


def static_frame(assets, language, name):
    entry = assets.get(language, name, 6)
    palettes = [v for v in assets.entries.values() if v["id"] == entry["param0"]]
    palette = struct.unpack("<256H", palettes[0]["payload"])
    return [palette[index] for index in entry["payload"]]


def draw_lower_map_layout(assets, language=1):
    canvas = [0]*49152
    palette = assets.palette(language, 1)
    # Placeholder is wholly unknown; this is not a replacement reveal algorithm.
    fill(canvas, 127, 78, 3, 5, palette[2])
    for x, width, label in ((0,85,"-"),(85,85,"+"),(170,86,"Центр" if language == 1 else "Center")):
        fill(canvas,x,160,width,32,palette[86])
        fill(canvas,x,160,width,1,palette[2])
        fill(canvas,x,160,1,32,palette[2])
        text(canvas,assets,language,label,x+(width-len(label)*8)//2,172,8,palette[2])
    return canvas


def safe_output(path, root=OUT):
    path,root=Path(path),Path(root)
    if ".." in path.parts or ".." in root.parts:
        raise ValueError("Parent traversal in N06 output")
    path,root=path.absolute(),root.absolute()
    allowed=(ROOT/"build/nds/n06",ROOT/"research/evidence/nds/n06")
    if not any(root.is_relative_to(base) for base in allowed) or not path.is_relative_to(root) or path==root:
        raise ValueError("Output must remain in its fixed N06 tree")
    current=ROOT
    for part in path.parent.relative_to(ROOT).parts:
        current/=part
        if current.is_symlink() or (hasattr(current,"is_junction") and current.is_junction()):
            raise ValueError("Output directory cannot be a filesystem alias")
        if current.exists() and not current.is_dir():
            raise ValueError("Output parent is not a directory")
    if path.is_symlink() or (hasattr(path,"is_junction") and path.is_junction()) or (path.exists() and (not path.is_file() or path.stat().st_nlink != 1)):
        raise ValueError("Output is not an unaliased regular file")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def save_frame(path, canvas, root=OUT):
    path = Path(path)
    if len(canvas) != 49152 or any(not 0 <= c <= 32767 for c in canvas):
        raise ValueError("Frame must contain 256*192 RGB555 words, bit15 clear")
    raw = struct.pack("<49152H", *(c | 32768 for c in canvas))
    rgb = bytearray()
    for c in canvas:
        rgb.extend((((c&31)*255+15)//31, (((c>>5)&31)*255+15)//31, (((c>>10)&31)*255+15)//31))
    def chunk(tag, payload):
        return struct.pack(">I",len(payload))+tag+payload+struct.pack(">I",zlib.crc32(tag+payload)&0xffffffff)
    scan = b"".join(b"\0"+rgb[y*768:(y+1)*768] for y in range(192))
    png = b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">2I5B",256,192,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(scan,9))+chunk(b"IEND",b"")
    outputs = {}
    destinations=[(extension,data,safe_output(path.with_suffix(extension),root)) for extension,data in ((".png",png),(".rgb555",raw))]
    for extension, data, dest in destinations:
        dest.write_bytes(data)
        outputs[extension] = dict(path=dest.relative_to(ROOT).as_posix(), bytes=len(data), sha256=sha(data))
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=PACKAGE)
    args = parser.parse_args()
    report_path=safe_output(EVIDENCE/"ui-layout-report.json",EVIDENCE)
    report_path.write_text(json.dumps(dict(task="N06",passed=False,status="starting"))+"\n",encoding="utf-8")
    assets = Assets(args.package)
    # Independent source scanner is a host-only N05 research utility.
    from audit_world import originals, parse_level
    archives = originals()
    report = dict(task="N06", passed=False, package_sha256=PACKAGE_SHA,
                  raster="256x192 RGB555, .rgb555 little-endian bit15=1", stories=[], messages=[], frames=[])
    for language in (1,2):
        for name in TEXT_NAMES:
            plan = paginate(assets.get(language,name,16)["payload"])
            pages = []
            for page in range(len(plan["pages"])):
                canvas, _ = render_text_page(assets,language,name,page)
                pages.append(save_frame(OUT/f"lang{language}-{name.lower()}-p{page+1}",canvas))
            report["stories"].append(dict(language=language,name=name,plan=plan,frames=pages))
        for level in (1,2,3):
            original = next(v for v in archives[language]["entries"] if bytes.fromhex(v["name"]) == f"LEVEL{level}".encode())
            model = parse_level(original["payload"])
            for obj in model["objects"]:
                for field in obj["fields"]:
                    if field["kind"] != 6:
                        continue
                    span = field["token"]
                    data = original["payload"][span["offset"]:span["offset"]+span["bytes"]]
                    plan = paginate(data,28,1)
                    report["messages"].append(dict(language=language,level=level,glyph=obj["glyph"],
                        native_offset=field["native_offset"],source_offset=span["offset"],bytes=len(data),plan=plan))
            for keymask in range(8):
                canvas = [0]*49152
                metadata = draw_hud(canvas,assets,language,level,dict(health=-32768,charge=32767,
                    health_max=32767,charge_max=32767,lives=-32768,score=32767,keys=keymask,weapon=3))
                frames = save_frame(OUT/f"hud-lang{language}-level{level}-keys{keymask}",canvas)
                report["frames"].append(dict(kind="hud-signed16-stress",language=language,level=level,keys=keymask,metadata=metadata,frames=frames))
        for name in ("ЗАСТАВКА_СПР","BSPR1","BSPR2","BSPR3","ESPR1","ESPR2","ESPR3"):
            report["frames"].append(dict(kind="static",language=language,name=name,
                frames=save_frame(OUT/f"static-lang{language}-{assets.get(language,name,6)['source_index']:03}",static_frame(assets,language,name))))
        report["frames"].append(dict(kind="map-layout-only",language=language,
            frames=save_frame(OUT/f"map-layout-lang{language}",draw_lower_map_layout(assets,language))))
        longest = max((m for m in report["messages"] if m["language"]==language),key=lambda m:m["bytes"])
        original = next(v for v in archives[language]["entries"] if bytes.fromhex(v["name"]) == f"LEVEL{longest['level']}".encode())
        data = original["payload"][longest["source_offset"]:longest["source_offset"]+longest["bytes"]]
        for page in range(len(longest["plan"]["pages"])):
            canvas=[0]*49152
            meta=draw_hud(canvas,assets,language,1,dict(health=100,charge=200,keys=7,weapon=2),data,page)
            report["frames"].append(dict(kind="longest-message",language=language,page=page,metadata=meta,
                frames=save_frame(OUT/f"message-lang{language}-p{page+1}",canvas)))
    report["native_door_sources"]=[]
    for language,label,segment,segment_index,offsets,exe,expected_sha in (
        (1,"ru","1190",51,(0x58c,0x59d,0x5ae,0x5c1,0x5d6,0x5ea,0x5fb),"originals/pkr/RUS/PKR.EXE","12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28"),
        (2,"en","10d0",27,(0x59c,0x5a7,0x5b4,0x5ca,0x5de,0x5f4,0x601),"originals/pkr/ENG/PKE.EXE","cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b")):
        sample=(ROOT/exe).read_bytes()
        if sha(sample)!=expected_sha:
            raise ValueError("Unexpected original EXE for saved Ghidra constants")
        exported=(ROOT/f"research/evidence/ui/{label}/{segment}.bin").read_bytes()
        ne=struct.unpack_from("<I",sample,0x3c)[0]
        table=struct.unpack_from("<H",sample,ne+0x22)[0]
        shift=struct.unpack_from("<H",sample,ne+0x32)[0]
        sector=struct.unpack_from("<H",sample,ne+table+(segment_index-1)*8)[0]
        for offset in offsets:
            end=exported.index(b"\0",offset)
            data=exported[offset:end]
            if sample[(sector<<shift)+offset:(sector<<shift)+end+1]!=data+b"\0":
                raise ValueError("Saved Ghidra text differs from original file bytes")
            report["messages"].append(dict(language=language,source="native-door",ghidra_address=f"{segment}:{offset:04X}",
                ne_segment_index=segment_index,file_offset=(sector<<shift)+offset,bytes=len(data),plan=paginate(data,28,1)))
        if sha((ROOT/exe).read_bytes())!=expected_sha:
            raise ValueError("Original EXE changed during check")
        report["native_door_sources"].append(dict(sample=exe,sha256_before=expected_sha,sha256_after=expected_sha,
            saved_ghidra_segment=f"research/evidence/ui/{label}/{segment}.bin",segment_sha256=sha(exported)))
    synthetic = [b"",b"A\r\nB$hidden",b"A\rB\0hidden",b"A"*61,b" "*30+b"B\n"*40,bytes(range(128,256))*10]
    for fixture in synthetic:
        paginate(fixture,30,9)
        paginate(fixture,28,1)
    # Signed bounds prove slot width across the entire word domain, separately
    # from representative image cases and clear-on-value-shrink checks.
    for value in range(-32768,32768):
        if len(number(value))>6:
            raise AssertionError("Signed16 value exceeds reserved number cells")
    shrink_cases=0
    for language in (1,2):
        for level in (1,2,3):
            for keymask in range(8):
                for weapon in range(4):
                    reused=[12345]*49152
                    for value in (32767,-32768,100,9,0):
                        state=dict(health=value,charge=value,lives=value,score=value,keys=keymask,weapon=weapon)
                        draw_hud(reused,assets,language,level,state)
                        fresh=[12345]*49152
                        draw_hud(fresh,assets,language,level,state)
                        if reused!=fresh or reused[:154*256]!=[12345]*(154*256):
                            raise AssertionError("HUD clear/value shrink or scene isolation failed")
                        shrink_cases+=1
    report["hud_verification"]=dict(all_signed16_number_widths=65536,all_six_level_languages=True,
        all_eight_key_masks=True,all_four_weapon_states=True,value_shrink_and_scene_guard_cases=shrink_cases)
    for language, archive in archives.items():
        if sha(archive["path"].read_bytes()) != archive["sha256"]:
            raise ValueError("Original changed during layout generation")
    report["passed"] = True
    report["counts"] = dict(stories=len(report["stories"]),story_pages=sum(len(s["plan"]["pages"]) for s in report["stories"]),
        message_bindings=len(report["messages"]),max_message_bytes=max(m["bytes"] for m in report["messages"]),
        frame_cases=len(report["frames"]),synthetic_cases=len(synthetic))
    target=safe_output(EVIDENCE/"ui-layout-report.json",EVIDENCE)
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(passed=True,counts=report["counts"],report=target.relative_to(ROOT).as_posix())))


if __name__ == "__main__":
    main()
