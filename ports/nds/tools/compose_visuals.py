"""Compose N06 review bitmaps from measured DOS PLAY and accepted N04 assets.

This offline target is not a 3D renderer. It never modifies originals, captures
or the package. Both localized presentations share the measured RU world.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import zlib

import layout_ui as ui

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n06/frames"
EVIDENCE = ROOT / "research/evidence/nds/n06"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def artifact(path):
    data = path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=len(data), sha256=sha(data))


def write(path, data, root=OUT):
    ui.safe_output(path, root).write_bytes(data)
    return artifact(path)


def write_json(path, data, root=OUT):
    return write(path, (json.dumps(data, ensure_ascii=False, indent=2)+"\n").encode("utf8"), root)


def png(width, height, rgb):
    if len(rgb) != width*height*3:
        raise ValueError("RGB888 dimensions")
    def chunk(tag, data):
        return struct.pack(">I", len(data))+tag+data+struct.pack(">I", zlib.crc32(tag+data)&0xffffffff)
    scan = b"".join(b"\0"+rgb[y*width*3:(y+1)*width*3] for y in range(height))
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR", struct.pack(">2I5B", width, height, 8, 2, 0, 0, 0))+chunk(b"IDAT", zlib.compress(scan, 9))+chunk(b"IEND", b"")


def rgb555(canvas):
    return bytes(c for word in canvas for c in (((word&31)*255+15)//31, (((word>>5)&31)*255+15)//31, (((word>>10)&31)*255+15)//31))


def blit_scaled(destination, dw, source, sw, sh, x, y, width, height):
    for row in range(height):
        sy = (2*row+1)*sh//(2*height)
        for col in range(width):
            sx = (2*col+1)*sw//(2*width)
            dest = ((y+row)*dw+x+col)*3
            src = (sy*sw+sx)*3
            destination[dest:dest+3] = source[src:src+3]


def label(destination, width, font, value, x, y):
    for col, code in enumerate(value.encode("ascii")):
        for row in range(8):
            bits = font[code*8+row]
            for bit in range(8):
                if bits & (128 >> bit):
                    pos = ((y+row)*width+x+col*8+bit)*3
                    destination[pos:pos+3] = b"\xff\xff\xff"


def load_clean(level):
    directory = EVIDENCE / f"dos/ru/level{level}-clean"
    report_path = directory / "report.json"
    report = json.loads(report_path.read_text("utf8"))
    state = report["clean"]
    if report["status"] != "passed" or report["language"] != "ru" or report["level"] != level:
        raise ValueError("Wrong DOS reference identity")
    if not report["originals_unchanged"] or not report["copied_runtime_unchanged"]:
        raise ValueError("DOS original/runtime integrity")
    if state["attempt_counter"] != 2 or any(not state[key] for key in (
        "dac_before_after_identical", "two_vga_frames_and_two_rgb_frames_identical", "dac_and_vga_indices_match_raw_rgb")):
        raise ValueError("Clean PLAY consistency")
    before, after = state["state_before"], state["state_after"]
    for key in ("camera_xyz", "sin_cos", "hero", "D", "clip", "width_height_cx_cy", "focal_q12", "viewport_scale"):
        if before[key] != after[key]:
            raise ValueError(f"Unstable clean PLAY {key}")
    if state["scheduler_before_dac"]["scheduler_elapsed"] == state["scheduler_after_dac"]["scheduler_elapsed"]:
        raise ValueError("Clean PLAY scheduler did not advance")
    files = {}
    inputs = [artifact(report_path)]
    for extension, pin in state["files"].items():
        path = directory / ("play-clean."+extension)
        row = artifact(path)
        if row["bytes"] != pin["bytes"] or row["sha256"] != pin["sha256"]:
            raise ValueError("DOS reference digest mismatch")
        files[extension] = path.read_bytes()
        inputs.append(row)
    indices, dac = files["idx"], files["vga6"]
    if len(indices) != 64000 or len(dac) != 768 or max(dac) > 63:
        raise ValueError("DOS reference dimensions/range")
    palette = [(dac[i]>>1)|((dac[i+1]>>1)<<5)|((dac[i+2]>>1)<<10) for i in range(0,768,3)]
    expected_rgb = bytes((dac[index*3+channel]*255+31)//63 for index in indices for channel in range(3))
    if expected_rgb != files["api.rgb"]:
        raise ValueError("Independent DOS displayed RGB mismatch")
    return dict(level=level, inputs=inputs, state=before, indices=indices,
                palette=palette, rgb=expected_rgb, dac_sha256=sha(dac))


def scene(indices, palette):
    if len(indices) != 64000 or len(palette) != 256:
        raise ValueError("Source scene dimensions")
    canvas = [0]*49152
    for y in range(154):
        sy = (2*y+1)*160//308
        for x in range(256):
            sx = (2*x+1)*320//512
            canvas[y*256+x] = palette[indices[sy*320+sx]]
    return canvas


class MeasuredPalette:
    def __init__(self, assets, palette):
        self.assets, self.measured = assets, palette

    def get(self, *args):
        return self.assets.get(*args)

    def palette(self, language, level):
        return self.measured


def aspect_control(assets):
    # DOS pixels have physical width/height 5:6 on a 4:3 display.
    indices = bytearray(64000)
    for y in range(160):
        for x in range(320):
            value = 1 if x % 40 == 0 or y % 40 == 0 else 0
            for cx in (80,240):
                d = 25*(x-cx)**2+36*(y-80)**2
                if abs(d-25*50**2) < 1800:
                    value = 2
            if (x == 159 and 69 <= y <= 89) or (y == 79 and 149 <= x <= 169):
                value = 3
            indices[y*320+x] = value
    palette = [0,0x4210,0x7fff,0x03ff]+[0]*252
    top = scene(indices, palette)
    for y in range(154,192):
        for x in range(256):
            v = x//8
            top[y*256+x] = v if y<164 else v<<5 if y<174 else v<<10 if y<184 else v|(v<<5)|(v<<10)
    bottom = [0]*49152
    for row, value in enumerate(("DOS 4:3 -> DS", "Scene 320x160 -> 256x154", "Circle: pixel aspect 5:6", "No vertical view expansion", "RGB5 ramps: 0..31")):
        ui.text(bottom, assets, 2, value, 8, 16+row*32, 16)
    return top, bottom


def height_control(assets):
    path = EVIDENCE / "height-near-fixtures.json"
    data = json.loads(path.read_text("utf8"))
    if not data["passed"] or len(data["analytic_height_controls"]) != 2:
        raise ValueError("Height controls did not pass")
    top, bottom = [0]*49152, [0]*49152
    for i, control in enumerate(data["analytic_height_controls"]):
        points = control["target_xy"]
        xs, ys = [p[0]["decimal"] for p in points], [p[1]["decimal"] for p in points]
        x0, x1 = round(min(xs)), round(max(xs))
        y0, y1 = max(0,round(min(ys))), min(153,round(max(ys)))
        color = 0x7fff if i == 0 else 0x03e0
        for y in range(y0,y1+1):
            top[y*256+x0] = top[y*256+x1] = color
        for x in range(x0,x1+1):
            top[y0*256+x] = top[y1*256+x] = color
    for x in range(256):
        top[76*256+x] = 0x001f
    for row, value in enumerate(("SYNTHETIC HEIGHT CONTROL", "Not a DOS screenshot", "White: ceiling Y=-951", "Green: ceiling Y=-561", "Native depth=1440, D=200", "Horizon y=76.01875")):
        ui.text(bottom, assets, 2, value, 8, 8+row*28, 16)
    return top, bottom, artifact(path)


def main():
    report_path = EVIDENCE / "visual-report.json"
    report = dict(task="N06", passed=False, sources=[], pairs=[], contacts=[],
                  scope="offline target bitmaps; separate ARM9 viewer, not a 3D renderer",
                  limitations=["DOS effects/animation phase unknown; running endpoint brackets, not global pause.",
                               "Captured DAC6 is measured per frame; no rewrite of resource palettes.",
                               "Lower gameplay screen is a layout of unknown map cells, not discovered automap.",
                               "Synthetic aspect/height controls are not original screenshots."])
    write_json(report_path, report, EVIDENCE)
    try:
        assets = ui.Assets()
        references = [load_clean(level) for level in (1,2,3)]
        report["sources"] = [artifact(assets.path), artifact(Path(__file__)), artifact(Path(ui.__file__))]
        report["sources"] += [row for ref in references for row in ref["inputs"]]
        pending = []
        scenes = []
        for ref in references:
            for language in (1,2):
                canvas = scene(ref["indices"], ref["palette"])
                hero = ref["state"]["hero"]
                hp, charge = hero["health_energy"]
                lives, score = hero["lives_score"]
                model = ui.draw_hud(canvas, MeasuredPalette(assets,ref["palette"]), language, ref["level"], dict(health=hp,charge=charge,lives=lives,score=score))
                if any(canvas[y*256+x] != ref["palette"][ref["indices"][((2*y+1)*160//308)*320+(2*x+1)*320//512]] for y in range(154) for x in range(256)):
                    raise AssertionError("HUD altered scene")
                pending.append((f"level{ref['level']}-{'ru' if language==1 else 'en'}",canvas,ui.draw_lower_map_layout(assets,language),dict(kind="measured_ru_world", level=ref["level"],language=language,hud=model,dac_sha256=ref["dac_sha256"],pose=ref["state"]["camera_xyz"])))
                scenes.append((ref,language,canvas))
        for language in (1,2):
            bottom = [0]*49152
            ui.text(bottom,assets,language,"A: Далее" if language==1 else "A: Next",96,88,16)
            pending.append((f"title-lang{language}",ui.static_frame(assets,language,"ЗАСТАВКА_СПР"),bottom,dict(kind="original_title_asset",language=language,palette="N04 resource RGB555; DOS observed title DAC is separately recorded")))
        for language in (1,2):
            ref = references[0]
            canvas = scene(ref["indices"],ref["palette"])
            model = ui.draw_hud(canvas,MeasuredPalette(assets,ref["palette"]),language,1,dict(health=-32768,charge=32767,health_max=32767,charge_max=32767,lives=-32768,score=32767,keys=7,weapon=3))
            pending.append((f"hud-stress-lang{language}",canvas,ui.draw_lower_map_layout(assets,language),dict(kind="synthetic_signed16_hud_stress",language=language,hud=model)))
        for language in (1,2):
            first, plan = ui.render_text_page(assets,language,"BINTRO1")
            last = len(plan["pages"])-1
            for page in (0,last):
                bottom, _ = ui.render_text_page(assets,language,"BINTRO1",page)
                pending.append((f"bintro1-lang{language}-p{page+1}",ui.static_frame(assets,language,"BSPR1"),bottom,dict(kind="full_text_page",language=language,name="BINTRO1",page=page,pages=len(plan["pages"]),source_sha256=plan["source_sha256"])))
        top,bottom = aspect_control(assets)
        pending.append(("aspect-rgb5-control",top,bottom,dict(kind="synthetic_physical_aspect_and_rgb5",ideal_radius_xy=[40,40.104166666666664],vertical_rounding_anisotropy_percent=0.2604166666666667)))
        top,bottom,height_pin = height_control(assets)
        report["sources"].append(height_pin)
        pending.append(("height-control",top,bottom,dict(kind="synthetic_asm_derived_height",fixture=height_pin)))
        # Validate every destination before writing any frame of the new set.
        for ordinal, (_,top,bottom,_) in enumerate(pending):
            if any(len(c)!=49152 or any(not 0<=word<=32767 for word in c) for c in (top,bottom)):
                raise ValueError("Target canvas RGB555 range")
            for suffix in ("top","bot"):
                for extension in (".png",".rgb555",".bin"):
                    ui.safe_output(OUT/f"v{ordinal:02}{suffix}{extension}",OUT)
        for name in ("index.dat","manifest.json"):
            ui.safe_output(OUT/name,OUT)
        index = bytearray(b"N06FRM1\0"+struct.pack("<2I",len(pending),68))
        for ordinal,(name,top,bottom,metadata) in enumerate(pending):
            pair = dict(ordinal=ordinal,case_id=ordinal+1,name=name,metadata=metadata)
            index.extend(struct.pack("<I",ordinal+1))
            for suffix, canvas in (("top",top),("bot",bottom)):
                images = ui.save_frame(OUT/f"v{ordinal:02}{suffix}",canvas,root=OUT)
                raw = (ROOT/images[".rgb555"]["path"]).read_bytes()
                pair[suffix] = dict(images=images, viewer=write(OUT/f"v{ordinal:02}{suffix}.bin",raw))
                index.extend(hashlib.sha256(raw).digest())
            report["pairs"].append(pair)
        report["index"] = write(OUT/"index.dat",index)
        font = assets.get(2,"FONT8",11)["payload"]
        for ref,language,canvas in scenes:
            contact = bytearray(656*260*3)
            label(contact,656,font,f"DOS LEVEL{ref['level']} - 4:3",0,4)
            label(contact,656,font,f"DS TARGET {'RU' if language==1 else 'EN'}",336,4)
            blit_scaled(contact,656,ref["rgb"],320,200,0,20,320,240)
            blit_scaled(contact,656,rgb555(canvas),256,192,336,20,320,240)
            report["contacts"].append(write(EVIDENCE/f"compare-level{ref['level']}-lang{language}.png",png(656,260,contact),EVIDENCE))
        # Every story page is shown at native pixel size in review contact sheets.
        pages = []
        for language in (1,2):
            for name in ui.TEXT_NAMES:
                _, plan = ui.render_text_page(assets,language,name)
                for page in range(len(plan["pages"])):
                    canvas,_ = ui.render_text_page(assets,language,name,page)
                    pages.append((f"L{language} {name} {page+1}/{len(plan['pages'])}",canvas))
        for start in range(0,len(pages),12):
            batch = pages[start:start+12]
            rows = (len(batch)+3)//4
            contact = bytearray(1024*rows*208*3)
            for ordinal,(name,canvas) in enumerate(batch):
                x,y = (ordinal%4)*256,(ordinal//4)*208
                label(contact,1024,font,name,x,y+4)
                blit_scaled(contact,1024,rgb555(canvas),256,192,x,y+16,256,192)
            report["contacts"].append(write(EVIDENCE/f"text-pages-{start//12+1}.png",png(1024,rows*208,contact),EVIDENCE))
        if len(pages)!=34 or len(pending)!=16:
            raise ValueError("Unexpected original sample page/fixture count")
        # Verify the shared RU scene byte-for-byte independently of HUD labels.
        for ordinal in (0,2,4):
            a = (ROOT/report["pairs"][ordinal]["top"]["viewer"]["path"]).read_bytes()[:154*512]
            b = (ROOT/report["pairs"][ordinal+1]["top"]["viewer"]["path"]).read_bytes()[:154*512]
            if a != b:
                raise AssertionError("Language changed RU world image")
        for pin in report["sources"]:
            if artifact(ROOT/pin["path"]) != pin:
                raise ValueError("Input changed during composition")
        report.update(passed=True,pair_count=len(pending),full_text_pages=len(pages),
                      sampling=dict(x="floor((2*x+1)*320/512)",y_scene="floor((2*y+1)*160/308)",palette="DAC6 channels >>1; RGB555"))
        report["manifest"] = write_json(OUT/"manifest.json",{k:v for k,v in report.items() if k!="passed"})
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    write_json(report_path,report,EVIDENCE)
    print(json.dumps(dict(passed=report["passed"],pairs=len(report["pairs"]),error=report.get("error"))))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
