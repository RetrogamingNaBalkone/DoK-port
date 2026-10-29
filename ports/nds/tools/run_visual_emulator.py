"""N06 external reference bitmap presentation on actual emulated ARM9.

Each frame is independently compared as all RAM bytes and all displayed RGB555
pixels. This viewer does not execute N07 geometry, sampling, LIGHT or gameplay.
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import traceback

import make_assets_fixture as fat
import run_diag_emulator as transport

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/"build/nds/n06"
HEADER=ROOT/"ports/nds/platform/visual/visual_probe.h"


def safe_output(path):
    path=Path(path)
    if ".." in path.parts:
        raise ValueError("Parent traversal in N06 output")
    path=path.absolute()
    if not path.is_relative_to(OUT) or path==OUT:
        raise ValueError("N06 output escapes build/nds/n06")
    parent=ROOT
    for component in path.parent.relative_to(ROOT).parts:
        parent/=component
        if parent.is_symlink() or (hasattr(parent,"is_junction") and parent.is_junction()):
            raise ValueError("N06 output directory alias")
    if path.is_symlink() or (path.exists() and (not path.is_file() or path.stat().st_nlink!=1)):
        raise ValueError("N06 output is not an unaliased regular file")
    path.parent.mkdir(parents=True,exist_ok=True)
    return path


def names():
    body=re.search(r"typedef struct NdsVisualProbe\s*\{(.*?)\}",HEADER.read_text(),re.S).group(1)
    fields=[n.strip() for row in re.findall(r"uint32_t\s+([^;]+);",body) for n in row.split(",")]
    if len(fields)!=26 or not all(f.isidentifier() for f in fields):
        raise ValueError("N06 observation ABI changed")
    return fields


def ram(frontend,address,count):
    base=frontend.core.retro_get_memory_data(2)
    size=frontend.core.retro_get_memory_size(2)
    offset=address-0x02000000
    if not base or not 0<=offset<=size-count:
        raise ValueError("N06 read outside main RAM")
    return C.string_at(base+offset,count)


def observe(frontend,address,fields):
    first=ram(frontend,address+12,4)
    data=ram(frontend,address,len(fields)*4)
    last=ram(frontend,address+12,4)
    state=dict(zip(fields,struct.unpack("<"+"I"*len(fields),data)))
    if first!=last or struct.unpack("<I",first)[0]!=state["sequence"] or state["sequence"]&1:
        return None
    if state["magic"]!=0x36564b44:
        return None
    if state["abi_version"]!=1 or state["size_bytes"]!=len(fields)*4:
        raise ValueError("N06 ABI mismatch")
    return state


def display_equal(frontend,top,bottom):
    width,height,rgb=frontend.rgb()
    if (width,height)!=(256,384):
        raise AssertionError("Actual display must be 256x384 top-bottom")
    expected=struct.unpack("<98304H",top+bottom)
    maps=[{} for _ in range(3)]
    for i,word in enumerate(expected):
        expected_channels=(word&31,(word>>5)&31,(word>>10)&31)
        for channel,value in enumerate(expected_channels):
            actual=rgb[3*i+channel]
            if actual>>3!=value:
                raise AssertionError(f"Displayed pixel differs at ({i%256},{i//256}) channel{channel}: RGB5 {actual>>3} != {value}")
            previous=maps[channel].setdefault(value,actual)
            if previous!=actual:
                raise AssertionError("Core uses inconsistent RGB5 to RGB8 expansion")
    # Primary upstream GPU2D_Soft::DrawPixel: c6=c5<<1.
    # GPU_Soft::ExpandColor: c8=(c6<<2)|(c6>>4), hence (c5<<3)|(c5>>3).
    # See rgb-expansion-provenance.json. The formula is fixed before this run;
    # observed maps are evidence only, never an adaptive expected lookup table.
    if any(any(actual!=((value<<3)|(value>>3)) for value,actual in channel.items()) for channel in maps):
        raise AssertionError("Core RGB8 differs from fixed upstream RGB5->RGB6->RGB8 formula")
    return dict(pixels=98304,exact_rgb5=True,exact_rgb8=True,rgb8_conversion="(c5<<3)|(c5>>3)",
                observed_channel_values=[len(channel) for channel in maps],
                observed_channel_maps=[sorted(channel.items()) for channel in maps],rgb8_sha256=hashlib.sha256(rgb).hexdigest())


def capture(frontend,stem):
    for suffix in (".png","-top.png","-bottom.png"):
        safe_output(frontend.directory/(stem+suffix))
    return frontend.capture(stem)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core",type=Path,required=True)
    parser.add_argument("--frames",type=Path,default=OUT/"frames")
    parser.add_argument("--rom",type=Path,default=OUT/"dok_nds_visual.nds")
    parser.add_argument("--elf",type=Path,default=OUT/"dok_nds_visual.elf")
    parser.add_argument("--scenario",choices=("happy","bad-index","bad-hash","short-frame","bad-pixel"),default="happy")
    args=parser.parse_args()
    directory=OUT/"emulator"/args.scenario
    result_path=safe_output(directory/"result.json")
    for subdir in ("system","save"):
        safe_output(directory/subdir/"reserved")
    transport.write_json(result_path,dict(task="N06",passed=False,status="starting"))
    report=dict(task="N06",passed=False,scenario=args.scenario,cases=[],python=sys.version,command=sys.argv,
        rom_sha256=transport.sha(args.rom),elf_sha256=transport.sha(args.elf),core_sha256=transport.sha(args.core),
        harness_sha256=transport.sha(Path(__file__)),abi_sha256=transport.sha(HEADER),
        transport_sha256=transport.sha(Path(transport.__file__)),fat_tool_sha256=transport.sha(Path(fat.__file__)))
    frontend=None
    try:
        index=(args.frames/"index.dat").read_bytes()
        if index[:8]!=b"N06FRM1\0" or len(index)<16:
            raise ValueError("Invalid fixture index")
        count,record_size=struct.unpack_from("<2I",index,8)
        if not 2<=count<=30 or record_size!=68 or len(index)!=16+count*68:
            raise ValueError("FAT fixture profile needs 2..30 pairs,68-byte records")
        manifest_path=args.frames/"manifest.json"
        manifest=json.loads(manifest_path.read_text(encoding="utf8"))
        ramp_ids={p["case_id"] for p in manifest["pairs"] if p["name"]=="aspect-rgb5-control"}
        if len(ramp_ids)!=1:
            raise ValueError("N06 manifest needs exactly one independently generated full RGB5 ramp control")
        report["frame_manifest_sha256"]=transport.sha(manifest_path)
        pairs=[]
        files=[]
        for ordinal in range(count):
            record=index[16+ordinal*68:84+ordinal*68]
            pair=dict(case_id=struct.unpack_from("<I",record)[0],ordinal=ordinal)
            for suffix,offset in (("top",4),("bot",36)):
                name=f"v{ordinal:02}{suffix}.bin"
                data=(args.frames/name).read_bytes()
                if len(data)!=98304 or hashlib.sha256(data).digest()!=record[offset:offset+32]:
                    raise ValueError("Fixture index digest/range mismatch")
                pair[suffix]=data
                mutant=data
                if ordinal==1 and suffix=="bot":
                    if args.scenario=="bad-hash":
                        mutant=bytes([data[0]^1])+data[1:]
                    elif args.scenario=="short-frame":
                        mutant=data[:-1]
                    elif args.scenario=="bad-pixel":
                        mutant=data[:1]+bytes([data[1]&127])+data[2:]
                        index=index[:16+68+36]+hashlib.sha256(mutant).digest()+index[16+68+68:]
                files.append((name,name.split(".")[0].upper().ljust(8).encode()+b"BIN",mutant))
            pairs.append(pair)
        if args.scenario=="bad-index":
            index=index[:16+68]+index[16:20]+index[16+68+4:]
        files.insert(0,("index.dat",b"INDEX   DAT",index))
        report["index_sha256"]=hashlib.sha256(index).hexdigest()
        fields=names()
        address,size=transport.elf_symbol(args.elf,"g_nds_visual_probe")
        if size!=len(fields)*4:
            raise ValueError("ELF probe size")
        report["observation_symbol"]=dict(address=address,bytes=size,access="read-only")
        frontend=transport.Frontend(args.core,directory,"happy")
        image_path=safe_output(frontend.save/"melonDS DS/dldi_sd_card.bin")
        fat.write_image(image_path,files)
        report["fixture"]=dict(path=str(image_path),before_load_sha256=transport.sha(image_path))
        frontend.load(args.rom)
        report["core_name"]=frontend.info.library_name.decode()
        report["core_version"]=frontend.info.library_version.decode()
        transport.flush_native_stdio()
        report["fixture"]["after_load_sha256"]=transport.sha(image_path)

        def step(button=0):
            frontend.joypad=button
            if frontend.frame_time and frontend.frame_time.callback:
                frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run();frontend.frames+=1
            if frontend.callback_error or frontend.shutdown:
                raise RuntimeError(frontend.callback_error or "Core shutdown")
            state=observe(frontend,address,fields)
            if state:
                report["last_observation"]=state
            return state

        def stable(status,ordinal=None):
            states=[]
            for _ in range(12000):
                state=step()
                if state and state["status"]==2 and status!=2:
                    raise AssertionError("ARM9 failure: "+repr(state))
                if state and state["status"]==status and (ordinal is None or state["ordinal"]==ordinal):
                    states.append(state)
                    if len(states)==8:
                        return states[-1]
            raise AssertionError("No eight coherent states for requested case")

        if args.scenario=="bad-index":
            state=stable(2)
            frontend.check("duplicate case ID rejected before presentation",state["error"]==3 and state["displayed"]==0)
        else:
            for ordinal,pair in enumerate(pairs if args.scenario=="happy" else pairs[:1]):
                state=stable(1,ordinal)
                frontend.check(f"case{ordinal}: identity",state["case_id"]==pair["case_id"] and state["case_count"]==count)
                frontend.check(f"case{ordinal}: all RAM bytes",ram(frontend,state["top_base"],98304)==pair["top"] and ram(frontend,state["bottom_base"],98304)==pair["bot"])
                display=display_equal(frontend,pair["top"],pair["bot"])
                frontend.check(f"case{ordinal}: all display pixels",display["exact_rgb5"])
                if pair["case_id"] in ramp_ids:
                    frontend.check("all 32 channel values match fixed expansion",display["observed_channel_values"]==[32,32,32] and display["exact_rgb8"])
                    report["full_rgb5_ramp"]=display
                images=capture(frontend,f"case-{ordinal:02}")
                report["cases"].append(dict(ordinal=ordinal,case_id=pair["case_id"],passed=True,observation=state,display=display,images=images))
                for _ in range(4): step(1<<8)
                for _ in range(4): step()
            if args.scenario!="happy":
                state=stable(2)
                frontend.check("bad frame rejected without changing published display",state["error"]==4 and state["displayed"]==1 and state["ordinal"]==0)
                report["preserved_display"]=display_equal(frontend,pairs[0]["top"],pairs[0]["bot"])
            else:
                state=stable(3)
                frontend.check("all pairs advanced only by A",state["displayed"]==count and state["ordinal"]==count-1)
            frontend.check("bounded staging and reads",state["asset_pool_bytes"]==1048576 and state["staging_bytes"]==196608 and state["max_request"]==65536)
            frontend.check("N03 memory policy",state["heap_limit_before"]==0x023f4000 and state["heap_limit_final"]==0x022e0000 and state["reserve_bytes"]==655360 and state["static_bytes"]+state["startup_overhead"]<=524288 and state["sdk_heap_available"]>=196608)
        report["final_images"]=capture(frontend,"final")
        report["passed"]=True
    except Exception:
        report["error"]=traceback.format_exc()
    finally:
        if frontend:
            report["checks"]=frontend.checks;report["frames_run"]=frontend.frames
            report["core_options"]={k:v.decode() for k,v in frontend.variables.items()}
            if not report["passed"]:
                report["failure_images"]=capture(frontend,"failure")
            try:
                transport.flush_native_stdio()
                report["fixture"]["after_run_sha256"]=transport.sha(image_path)
                frontend.close();transport.flush_native_stdio()
                report["fixture"]["after_unload_sha256"]=transport.sha(image_path)
                fixture=report["fixture"]
                fixture["runtime_unchanged"]=fixture["after_load_sha256"]==fixture["after_run_sha256"]==fixture["after_unload_sha256"]
                report["passed"]=report["passed"] and fixture["runtime_unchanged"]
            except Exception:
                report["cleanup_error"]=traceback.format_exc();report["passed"]=False
        transport.write_json(result_path,report)
    print(f"N06 {args.scenario}: {'PASS' if report['passed'] else 'FAIL'}; {result_path}")
    return 0 if report["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
