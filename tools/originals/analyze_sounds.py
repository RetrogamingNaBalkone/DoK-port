"""Verify original SVM sounds against C output, including full PCM and RIFF bytes."""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[2]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import argparse
import collections
import csv
import html
import io
import json
import math
import struct
import subprocess
import wave
from pathlib import Path
from analyze_resources import ROOT, fnv, sha


def decode(data):
    if len(data)<36:
        raise ValueError("Truncated SVM header")
    version,=struct.unpack_from("<H",data,5)
    bits,mode,c,d,rate,count=struct.unpack_from("<4HII",data,8)
    if (data[2:5],version,data[7],bits,mode)!=(b"SVM",1,9,8,0):
        raise ValueError("Unknown sound profile")
    if not 0<rate<=65535 or count!=len(data)-36:
        raise ValueError("Invalid rate or length")
    samples=data[36:]
    signed8=bytes(value^0x80 for value in samples)
    signed16=b"".join(struct.pack("<h",(value-128)*256) for value in samples)
    return dict(sample_rate=rate,sample_count=count,parameter_c=c,parameter_d=d,
                fnv_u8=fnv(samples),fnv_s8=fnv(signed8),fnv_s16le=fnv(signed16)),samples,signed8,signed16


def waveform(path,data):
    width,height=600,96
    lines=[]
    for x in range(min(width,len(data))):
        start=x*len(data)//min(width,len(data));end=(x+1)*len(data)//min(width,len(data))
        segment=data[start:end]
        top=height-1-max(segment)*(height-1)/255
        bottom=height-1-min(segment)*(height-1)/255
        lines.append(f'M{x},{top:.2f}V{bottom:.2f}')
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" width="600" height="96" viewBox="0 0 600 96">'
                    '<rect width="600" height="96" fill="#20252a"/>'
                    '<path d="M0 48H600" stroke="#677078"/>'
                    f'<path d="{" ".join(lines)}" stroke="#6dd3b2" stroke-width="1"/></svg>',encoding="utf-8")


def run(tool):
    reports={};gallery=[]
    for lang in ("ru","en"):
        catalog=json.loads((ROOT/"analysis/originals"/f"resources-{lang}.json").read_text(encoding="utf-8"))
        archive=(ROOT/catalog["file"]).read_bytes()
        if sha(archive)!=catalog["sha256"]:
            raise ValueError("Original archive changed")
        sounds=[]
        for entry in catalog["entries"]:
            if entry["kind"]!="sound_signature_unparsed":
                continue
            raw=archive[entry["offset"]:entry["offset"]+entry["size"]]
            source=ROOT/"research/evidence/resources"/lang/f"{entry['index']:04d}.bin"
            if sha(raw)!=entry["sha256"] or sha(source.read_bytes())!=entry["sha256"]:
                raise ValueError("Sound source bytes differ")
            expected,pcm,s8,s16=decode(raw)
            output=ROOT/"research/evidence/sounds"/lang/f"{entry['index']:04d}"
            output.mkdir(parents=True,exist_ok=True)
            completed=subprocess.run([str(tool),"--export",str(source),str(output)],check=True,capture_output=True,text=True)
            actual=list(csv.DictReader(io.StringIO(completed.stdout),delimiter="\t"))
            if len(actual)!=1 or any(str(expected[key])!=value for key,value in actual[0].items()):
                raise ValueError(f"C/Python sound mismatch: {lang}/{entry['index']}")
            if (output/"pcm_s8.raw").read_bytes()!=s8 or (output/"pcm_s16le.raw").read_bytes()!=s16:
                raise ValueError("Decoded samples differ")
            wav=(output/"pcm8.wav").read_bytes()
            if len(wav)!=44+len(pcm)+(len(pcm)&1) or struct.unpack_from("<I",wav,4)[0]!=len(wav)-8:
                raise ValueError("RIFF length mismatch")
            if len(pcm)&1 and wav[-1]!=0:
                raise ValueError("RIFF padding missing")
            with wave.open(io.BytesIO(wav),"rb") as w:
                if (w.getnchannels(),w.getsampwidth(),w.getframerate(),w.getnframes(),w.getcomptype())!=(1,1,expected["sample_rate"],len(pcm),"NONE"):
                    raise ValueError("WAVE format mismatch")
                if w.readframes(len(pcm)+1)!=pcm:
                    raise ValueError("WAVE samples differ")
            waveform(output/"waveform.svg",pcm)
            sound=dict(resource_index=entry["index"],name=entry["name"],source_sha256=sha(raw),**expected,
                       pcm_u8_sha256=sha(pcm),pcm_s8_sha256=sha(s8),pcm_s16le_sha256=sha(s16),
                       wav_sha256=sha(wav),duration_seconds=len(pcm)/expected["sample_rate"],
                       sample_min=min(pcm) if pcm else None,sample_max=max(pcm) if pcm else None,
                       rms_unsigned_centered=math.sqrt(sum((x-128)**2 for x in pcm)/len(pcm)) if pcm else 0)
            sounds.append(sound)
            if lang=="ru":
                prefix=f"ru/{entry['index']:04d}"
                gallery.append(f'<section><h2>{entry["index"]}: {html.escape(entry["name"])}</h2>'+
                    f'<p>{expected["sample_rate"]} Hz, {len(pcm)} samples, {sound["duration_seconds"]:.3f} s</p>'+
                    f'<audio controls preload="none" src="{prefix}/pcm8.wav"></audio><br>'+
                    f'<img src="{prefix}/waveform.svg" alt="sample amplitude envelope"></section>')
        report=dict(archive_sha256=sha(archive),sounds=len(sounds),sample_count=sum(s["sample_count"] for s in sounds),
                    rate_counts=dict(collections.Counter(s["sample_rate"] for s in sounds)),
                    odd_sample_counts=sum(s["sample_count"]%2 for s in sounds),entries=sounds)
        reports[lang]=report
        (ROOT/"analysis/originals"/f"sounds-{lang}.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(lang,report["sounds"],"sounds,",report["sample_count"],"samples; PCM8/S8/S16 and WAV verified",flush=True)
    comparisons=[]
    if len(reports["ru"]["entries"])!=len(reports["en"]["entries"]):
        raise ValueError("Language sound counts differ")
    for ru,en in zip(reports["ru"]["entries"],reports["en"]["entries"]):
        if ru["resource_index"]!=en["resource_index"] or ru["name"]!=en["name"]:
            raise ValueError("Sound identity differs")
        comparisons.append(dict(resource_index=ru["resource_index"],name=ru["name"],
                                raw_identical=ru["source_sha256"]==en["source_sha256"],
                                pcm_identical=ru["pcm_u8_sha256"]==en["pcm_u8_sha256"]))
    (ROOT/"analysis/originals/sounds-comparison.json").write_text(json.dumps(comparisons,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    (ROOT/"research/evidence/sounds/index.html").write_text('<!doctype html><meta charset="utf-8"><title>DoK sound research</title>'+
        '<style>body{background:#20252a;color:#eee;font:16px sans-serif}section{margin:32px 0}h2{font-size:18px}img{max-width:100%}</style>'+
        '<h1>Original sound resources</h1><p>RUS files; compare language data in analysis/originals/sounds-comparison.json. No gain, effects or resampling.</p>'+"\n".join(gallery),encoding="utf-8")
    print("Identical RUS/ENG sounds:",sum(x["raw_identical"] for x in comparisons),"of",len(comparisons))


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool",type=Path,default=ROOT/"build/gba/host/dok-sound.exe")
    run(parser.parse_args().tool.resolve())
