"""Independent Python/C verification and lossless frame export for both originals."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[2]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import csv
import html
import io
import json
import math
import struct
import subprocess
import zlib
from pathlib import Path
from analyze_resources import ROOT, fnv, sha


def image(data, offset=0):
    if offset + 18 > len(data):
        raise ValueError("Truncated image header")
    w, h, flags, crop_a, crop_b, size, _ = struct.unpack_from("<5HII", data, offset)
    if not w or not h or size != w*h or size > len(data)-offset-18:
        raise ValueError("Invalid image dimensions or payload")
    pixels = data[offset+18:offset+18+size]
    return dict(width=w, height=h, flags=flags, crop_divisor=crop_a, crop_index=crop_b,
                pixel_bytes=size, fnv1a32=fnv(pixels), sha256=sha(pixels), image_offset=offset), pixels


def animation(data):
    if len(data) < 8:
        raise ValueError("Truncated animation")
    count, = struct.unpack_from("<H", data)
    if not 0 < count <= 32767 or 8+count*10 > len(data):
        raise ValueError("Invalid frame count")
    offset = 8+count*10
    frames, pixels = [], []
    for index in range(count):
        bits, a, b = struct.unpack_from("<Hhh", data, 8+index*10)
        frame, raw = image(data, offset)
        frame.update(index=index, interval_bits=bits, interval_ticks=bits if bits < 32768 else bits-65536,
                     parameter_a=a, parameter_b=b)
        frames.append(frame); pixels.append(raw)
        offset += 18+len(raw)
    if offset != len(data):
        raise ValueError("Animation has trailing bytes")
    return frames, pixels


def png(path, width, height, pixels):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload))+kind+payload+struct.pack(">I", zlib.crc32(kind+payload))
    rows = b"".join(b"\0"+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR", struct.pack(">IIBBBBB",width,height,8,2,0,0,0))+
                     chunk(b"IDAT",zlib.compress(rows))+chunk(b"IEND",b""))


def contact_sheet(path, frames, pixels, palette):
    columns = min(4,len(frames)); rows = math.ceil(len(frames)/columns)
    cw = max(f["width"] for f in frames)+4; ch = max(f["height"] for f in frames)+4
    w,h = cw*columns,ch*rows
    output = bytearray([24])*(w*h*3)
    for index,(frame,raw) in enumerate(zip(frames,pixels)):
        left = (index%columns)*cw+2; top=(index//columns)*ch+2
        for y in range(frame["height"]):
            for x in range(frame["width"]):
                color = raw[y*frame["width"]+x]
                at = ((top+y)*w+left+x)*3
                output[at:at+3] = bytes(palette[color*3+k]*255//63 for k in range(3))
    png(path,w,h,output)


def run(tool):
    reports = {}; gallery = []
    for lang in ("ru","en"):
        catalog = json.loads((ROOT/"analysis/originals"/f"resources-{lang}.json").read_text(encoding="utf-8"))
        data = (ROOT/catalog["file"]).read_bytes()
        if sha(data) != catalog["sha256"]:
            raise ValueError("Original archive changed")
        palette_entry = next(e for e in catalog["entries"] if e["name"]=="GAMEP1")
        palette = data[palette_entry["offset"]:palette_entry["offset"]+palette_entry["size"]]
        results = []; images_checked = 0
        for entry in catalog["entries"]:
            raw = data[entry["offset"]:entry["offset"]+entry["size"]]
            source = ROOT/"research/evidence/resources"/lang/f"{entry['index']:04d}.bin"
            if sha(source.read_bytes()) != entry["sha256"] or sha(raw) != entry["sha256"]:
                raise ValueError("Source extraction differs")
            if entry["kind"] == "indexed_image18":
                expected,_ = image(raw)
                completed = subprocess.run([str(tool),"--image",str(source)],check=True,capture_output=True,text=True)
                actual = list(csv.DictReader(io.StringIO(completed.stdout),delimiter="\t"))
                if len(actual)!=1 or any(str(expected[k])!=v for k,v in actual[0].items()):
                    raise ValueError(f"Standalone image mismatch: {lang}/{entry['index']}")
                images_checked += 1
                continue
            if entry["kind"] != "unparsed":
                continue
            frames, pixels = animation(raw)
            output = ROOT/"research/evidence/animations"/lang/f"{entry['index']:04d}"
            output.mkdir(parents=True,exist_ok=True)
            completed = subprocess.run([str(tool),"--export",str(source),str(output)],check=True,capture_output=True,text=True)
            actual = list(csv.DictReader(io.StringIO(completed.stdout),delimiter="\t"))
            if len(actual)!=len(frames):
                raise ValueError("C/Python frame counts differ")
            for expected,other,pixel_data in zip(frames,actual,pixels):
                if any(str(expected[k])!=v for k,v in other.items()):
                    raise ValueError(f"C/Python mismatch: {lang}/{entry['index']} frame {expected['index']}")
                pgm = (output/f"frame_{expected['index']:04d}.pgm").read_bytes()
                header = f"P5\n{expected['width']} {expected['height']}\n255\n".encode()
                if pgm != header+pixel_data:
                    raise ValueError("C-exported pixels differ")
            contact_sheet(output/"sheet.png",frames,pixels,palette)
            results.append(dict(resource_index=entry["index"],name=entry["name"],sha256=sha(raw),frames=frames))
            if lang=="ru":
                gallery.append(f'<figure><figcaption>{entry["index"]}: {html.escape(entry["name"])} — {len(frames)} frames</figcaption>'+
                               f'<img src="ru/{entry["index"]:04d}/sheet.png" alt="frames in row order"></figure>')
        report = dict(archive_sha256=sha(data),animations=len(results),frames=sum(len(r["frames"]) for r in results),
                      standalone_images_checked=images_checked,entries=results,
                      negative_intervals=[dict(resource_index=r["resource_index"],frame=f["index"],bits=f["interval_bits"],signed=f["interval_ticks"])
                                          for r in results for f in r["frames"] if f["interval_ticks"]<0])
        reports[lang]=report
        (ROOT/"analysis/originals"/f"animations-{lang}.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(lang,report["animations"],"animations",report["frames"],"frames;",images_checked,"standalone images checked",flush=True)
    comparisons=[]
    for ru,en in zip(reports["ru"]["entries"],reports["en"]["entries"]):
        if ru["resource_index"]!=en["resource_index"]:
            raise ValueError("Animation indices differ")
        differences=[]
        for a,b in zip(ru["frames"],en["frames"]):
            keys=("width","height","interval_bits","parameter_a","parameter_b","flags","crop_divisor","crop_index","sha256")
            differences.extend(dict(frame=a["index"],field=k,ru=a[k],en=b[k]) for k in keys if a[k]!=b[k])
        pixel_differences=[]
        if any(d["field"]=="sha256" for d in differences):
            raw_frames={lang:animation((ROOT/"research/evidence/resources"/lang/f"{ru['resource_index']:04d}.bin").read_bytes())[1] for lang in ("ru","en")}
            for a,b in zip(ru["frames"],en["frames"]):
                if a["sha256"]==b["sha256"]:
                    continue
                if (a["width"],a["height"])!=(b["width"],b["height"]):
                    pixel_differences.append(dict(frame=a["index"],different_geometry=True))
                    continue
                cells=[dict(x=i%a["width"],y=i//a["width"],ru=x,en=y)
                       for i,(x,y) in enumerate(zip(raw_frames["ru"][a["index"]],raw_frames["en"][b["index"]])) if x!=y]
                pixel_differences.append(dict(frame=a["index"],count=len(cells),first_cells=cells[:32]))
        comparisons.append(dict(resource_index=ru["resource_index"],name=ru["name"],
                                raw_identical=ru["sha256"]==en["sha256"],frame_counts_equal=len(ru["frames"])==len(en["frames"]),
                                decoded_differences=differences,pixel_differences=pixel_differences))
    (ROOT/"analysis/originals/animations-comparison.json").write_text(json.dumps(comparisons,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (ROOT/"research/evidence/animations/index.html").write_text('<!doctype html><meta charset="utf-8"><title>DoK animation research</title>'+
        '<style>body{background:#222;color:#eee;font:16px sans-serif}img{image-rendering:pixelated;max-width:100%}figure{margin:24px 0}</style>'+
        '<h1>RUS animation frames</h1><p>Palette GAMEP1, opaque indices, no playback timing or world transforms.</p>'+"\n".join(gallery),encoding="utf-8")
    print("C and Python agree on",sum(r["frames"] for r in reports.values()),"animation frames and",
          sum(r["standalone_images_checked"] for r in reports.values()),"standalone images; PGM bytes verified")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool",type=Path,default=ROOT/"build/gba/host/dok-anim.exe")
    run(parser.parse_args().tool.resolve())
