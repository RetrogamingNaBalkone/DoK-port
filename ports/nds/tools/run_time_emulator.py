"""Observe N08 ARM9 clocks/events and real Start pause via libretro input.

The accepted N02 frontend supplies transport only. This harness never writes
emulated RAM, CPU state, counter values, test results or savestates.
"""
from __future__ import annotations

import argparse
import ctypes as C
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import traceback

import run_diag_emulator as transport

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n08"
CORE_SHA = "c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef"


def safe_output(path: Path) -> Path:
    path = path.absolute()
    if ".." in path.parts or path == OUT or not path.is_relative_to(OUT):
        raise ValueError("N08 emulator output escapes build/nds/n08")
    for item in (path, *path.parents):
        if item == ROOT: break
        if item.is_symlink() or item.is_junction():
            raise ValueError(f"N08 emulator output crosses alias: {item}")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError("N08 emulator output is not a regular unaliased file")
    return path


def directory(path: Path) -> None:
    safe_output(path/"reserved")
    if path.exists():
        pending = [path]
        while pending:
            for item in pending.pop().iterdir():
                if item.is_symlink() or item.is_junction(): raise ValueError("N08 core directory alias")
                if item.is_dir(): pending.append(item)
                elif not item.is_file() or item.stat().st_nlink != 1: raise ValueError("N08 core file alias")
    path.mkdir(parents=True, exist_ok=True)


def fields(header: Path, name: str) -> list[str]:
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", header.read_text("utf8"), flags=re.S)
    match = re.search(r"typedef\s+struct\s+"+re.escape(name)+r"\s*\{(.*?)\}",source,re.S)
    if not match: raise ValueError("N08 missing observation type")
    result = []
    for declaration in match.group(1).split(";"):
        declaration = declaration.strip()
        if not declaration: continue
        if not declaration.startswith("uint32_t "): raise ValueError("N08 ABI requires scalar uint32 fields")
        for name in declaration[len("uint32_t "):].split(","):
            name = name.strip()
            if not name.isidentifier() or name in result: raise ValueError("N08 invalid ABI scalar")
            result.append(name)
    if result[:4] != ["magic","abi_version","size_bytes","sequence"]: raise ValueError("N08 seqlock header changed")
    return result


def ram(frontend, address: int, count: int) -> bytes:
    pointer = frontend.core.retro_get_memory_data(2)
    size = frontend.core.retro_get_memory_size(2)
    offset = address - 0x02000000
    if not pointer or count < 0 or not 0 <= offset <= size-count: raise ValueError("N08 observation outside RAM")
    return C.string_at(pointer+offset,count)


def observe(frontend, address: int, names: list[str], magic: int) -> dict | None:
    first = ram(frontend,address+12,4)
    data = ram(frontend,address,len(names)*4)
    last = ram(frontend,address+12,4)
    value = dict(zip(names,struct.unpack("<"+"I"*len(names),data)))
    if first != last or struct.unpack("<I",first)[0] != value["sequence"] or value["sequence"] & 1: return None
    if value["magic"] != magic: return None
    if value["abi_version"] != 1 or value["size_bytes"] != len(data): raise ValueError("N08 observation ABI mismatch")
    return value


def u64(row: dict, prefix: str) -> int:
    return row[prefix+"_low"] | (row[prefix+"_high"] << 32)


def expected_synthetic(fps: int) -> dict:
    deadlines = {1:416,2:1040,3:1664}
    intervals = deadlines.copy()
    callbacks, renders = [], []
    previous_render = -1
    for tick in range(0,8001,5):
        for owner in (3,1,2):
            if tick >= deadlines[owner]:
                callbacks.append((tick,owner,owner,deadlines[owner],intervals[owner]))
                deadlines[owner] = tick+intervals[owner]
        bucket = tick*fps*119//1193182
        if bucket != previous_render:
            renders.append(tick)
            previous_render = bucket
    return dict(callbacks=callbacks,renders=renders)


def verify_traces(records: list[dict]) -> dict:
    synthetic = []
    for scenario,fps in ((1,15),(2,30),(3,60)):
        rows = [row for row in records if row["scenario_id"] == scenario]
        actual = [(row["native_now"],row["owner"],row["callback"],row["native_deadline"],row["native_interval"])
                  for row in rows if row["action"] == 2]
        renders = [row["native_now"] for row in rows if row["action"] == 5]
        expected = expected_synthetic(fps)
        if actual != expected["callbacks"]: raise AssertionError(f"N08 synthetic{scenario} callback sequence differs from independent polling model")
        if renders != expected["renders"]: raise AssertionError(f"N08 synthetic{scenario} render read sequence differs")
        if len([row for row in rows if row["action"] == 6 and row["native_now"] == 8000]) != 1: raise AssertionError("N08 synthetic scenario did not end exactly once")
        for row in rows:
            if (u64(row,"rate_numerator"),u64(row,"rate_denominator"),u64(row,"fraction"),row["paused"]) != (1,1,0,0):
                raise AssertionError("N08 synthetic native tick frequency/snapshot changed")
        synthetic.append(dict(scenario=scenario,fps=fps,callbacks=len(actual),render_reads=len(renders),passed=True))
    live = [row for row in records if row["scenario_id"] == 4]
    if not live or live[0]["action"] != 7: raise AssertionError("N08 live clock has no initial boundary")
    ratio = Fraction(1193182*64,119*33513982)
    transitions = 0
    for previous,current in zip(live,live[1:]):
        last,current_counter = u64(previous,"clock_last_counter"),u64(current,"clock_last_counter")
        if current_counter < last: raise AssertionError("N08 live clock regressed")
        ticks,fraction = previous["native_now"],u64(previous,"fraction")
        if not previous["paused"]:
            whole,fraction = divmod((current_counter-last)*ratio.numerator+fraction,ratio.denominator)
            ticks = (ticks+whole)&0xFFFFFFFF
        if (current["native_now"],u64(current,"fraction")) != (ticks,fraction):
            raise AssertionError("N08 ARM9 live clock differs from exact rational model")
        transitions += 1
    for row in live:
        if (u64(row,"rate_numerator"),u64(row,"rate_denominator")) != (ratio.numerator,ratio.denominator): raise AssertionError("N08 live reduced rate changed")
        if u64(row,"raw_counter") < u64(row,"clock_last_counter"): raise AssertionError("N08 later raw sample predates world clock")
    pauses = [row for row in live if row["action"] == 3]
    resumes = [row for row in live if row["action"] == 4]
    if len(pauses) != 1 or len(resumes) != 1: raise AssertionError("N08 live pause/resume boundary count")
    pause,resume = pauses[0],resumes[0]
    if resume["vblank_count"]-pause["vblank_count"] < 120: raise AssertionError("N08 menu interval shorter than120 VBlank")
    if (pause["native_now"],u64(pause,"fraction"),pause["native_deadline"]) != (resume["native_now"],u64(resume,"fraction"),resume["native_deadline"]): raise AssertionError("N08 pause advanced world or changed pending deadline")
    callbacks = [row for row in live if row["action"] == 2]
    if len(callbacks) < 10: raise AssertionError("N08 insufficient actual live scheduler callbacks")
    for row in callbacks:
        if row["paused"] or pause["ordinal"] < row["ordinal"] < resume["ordinal"]: raise AssertionError("N08 callback executed in pause")
        if row["native_now"] < row["native_deadline"] or row["native_interval"] != 416: raise AssertionError("N08 live callback fired before source deadline")
    for a,b in zip(callbacks,callbacks[1:]):
        if b["native_now"]-a["native_now"] < 416 or b["vblank_count"] == a["vblank_count"]: raise AssertionError("N08 repeated callback burst/catchup")
    after = [row for row in callbacks if row["ordinal"] > resume["ordinal"]]
    if not after or after[0]["native_deadline"] != resume["native_deadline"]: raise AssertionError("N08 first resumed callback lost pending deadline")
    return dict(passed=True,synthetic=synthetic,live_callbacks=len(callbacks),
                exact_clock_transitions=transitions,pause_vblanks=resume["vblank_count"]-pause["vblank_count"],
                paused_world_ticks=pause["native_now"],resume_world_ticks=resume["native_now"],
                first_resumed_callback=after[0])


def verify_memory(probe: dict, elf: Path) -> dict:
    static_end,_ = transport.elf_symbol(elf,"__end__")
    linker_end,_ = transport.elf_symbol(elf,"__eheap_end")
    if (probe["region_base"],probe["region_limit"]) != (0x02000000,0x02380000): raise AssertionError("N08 ARM9 region policy changed")
    if static_end != probe["static_end"] or linker_end != probe["region_limit"]: raise AssertionError("N08 runtime ELF memory bounds differ")
    if not static_end <= probe["heap_start"] <= probe["heap_end"] <= probe["heap_limit_final"]: raise AssertionError("N08 heap ordering")
    if probe["heap_limit_before"] < probe["region_limit"] or probe["heap_limit_policy"] != probe["region_limit"]: raise AssertionError("N08 heap policy clamp omitted")
    if probe["reserve_bytes"] != 655360 or probe["heap_limit_final"] != probe["region_limit"]-655360: raise AssertionError("N08 emergency reserve missing")
    if probe["sdk_heap_available"] != probe["heap_limit_final"]-probe["heap_end"] or probe["sdk_heap_available"] < 196608 or probe["sdk_heap_min"] != 196608: raise AssertionError("N08 SDK heap minimum violated")
    if probe["managed_bytes"] != 2293760 or probe["managed_base"]%32 or not probe["heap_start"] <= probe["managed_base"] <= probe["heap_end"]-probe["managed_bytes"]: raise AssertionError("N08 accepted typed pool allocation changed")
    startup = probe["heap_end"]-probe["region_base"]-probe["managed_bytes"]
    if startup != probe["static_startup_bytes"] or startup > 524288: raise AssertionError("N08 static/startup envelope exceeded")
    if probe["events_bank_bytes"] != 36864 or probe["events_bank_bytes"] > 65536: raise AssertionError("N08 events bank exceeded slab")
    closure = startup+probe["managed_bytes"]+probe["sdk_heap_available"]+probe["reserve_bytes"]
    if closure != probe["region_limit"]-probe["region_base"]: raise AssertionError("N08 memory byte closure differs")
    return dict(passed=True,static_startup_bytes=startup,managed_bytes=probe["managed_bytes"],
                sdk_heap_available=probe["sdk_heap_available"],reserve_bytes=probe["reserve_bytes"],
                events_bank_bytes=probe["events_bank_bytes"],closure_bytes=closure)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core",type=Path,required=True)
    parser.add_argument("--rom",type=Path,default=OUT/"dok_nds_time.nds")
    parser.add_argument("--elf",type=Path,default=OUT/"dok_nds_time.elf")
    args = parser.parse_args()
    path = OUT/"emulator"
    directory(path)
    target = safe_output(path/"result.json")
    report = dict(task="N08",passed=False,python=sys.version,command=sys.argv,
                  limitations=["Emulator only; physical DS/DS Lite and crystal accuracy remain N24.",
                               "Fixed dispatch samples are a test input, not a new fixed logic update grid."])
    frontend = None
    try:
        if transport.sha(args.core) != CORE_SHA: raise ValueError("N08 core differs from accepted melonDS DS1.4.0 binary")
        header = ROOT/"ports/nds/platform/time_probe/time_probe.h"
        probe_fields,trace_fields = fields(header,"NdsTimeProbe"),fields(header,"NdsTimeTraceRecord")
        address,size = transport.elf_symbol(args.elf,"g_nds_time_probe")
        if size != len(probe_fields)*4: raise ValueError("N08 header/ELF probe size differs")
        report.update(rom_sha256=transport.sha(args.rom),elf_sha256=transport.sha(args.elf),
                      core_sha256=transport.sha(args.core),harness_sha256=transport.sha(Path(__file__)),
                      abi_sha256=transport.sha(header),transport_sha256=transport.sha(Path(transport.__file__)),
                      observation_symbol=dict(address=address,bytes=size,access="read-only"))
        host_path = ROOT/"research/evidence/nds/n08/host-check.json"
        host = json.loads(host_path.read_text("utf8"))
        if not host.get("passed") or "arm9_build" not in host: raise ValueError("N08 emulator requires a passed matching host/original/ARM9 build gate")
        for row in host["sources"]+host["arm9_build"]["artifacts"]:
            if transport.sha(ROOT/row["path"]) != row["sha256"]: raise ValueError(f"N08 host gate input/artifact changed: {row['path']}")
        report["host_gate_sha256"] = transport.sha(host_path)
        frontend = transport.Frontend(args.core,path,"missing-media")
        frontend.load(args.rom)
        records,observations,inputs = [],[],[]
        live_start,pause_start,resume_start = None,None,None
        press_pause,press_resume,completed = False,False,0
        final = None
        for frame in range(2400):
            joypad = 0
            if press_pause and pause_start is None: joypad = 1<<3
            if pause_start is not None and resume_start is None:
                if frame-pause_start < 120: joypad = 1<<3
                elif frame-pause_start < 125: joypad = 0
                else: joypad = 1<<3; press_resume = True
            if resume_start is not None: joypad = 0
            frontend.joypad = joypad
            if not inputs or inputs[-1]["joypad"] != joypad: inputs.append(dict(frontend_frame=frame,joypad=joypad))
            if frontend.frame_time and frontend.frame_time.callback:
                frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run(); frontend.frames += 1
            if frontend.callback_error or frontend.shutdown: raise RuntimeError(frontend.callback_error or "Core shutdown")
            probe = observe(frontend,address,probe_fields,0x38544B44)
            if probe is None: continue
            report["last_observation"] = probe
            if probe["status"] == 6 or probe["error_bits"] or probe["failures"]: raise AssertionError("N08 ARM9 probe reports failure")
            if probe["trace_capacity"] != 512 or probe["trace_record_bytes"] != len(trace_fields)*4: raise AssertionError("N08 trace ring ABI differs")
            while len(records) < probe["trace_total"]:
                ordinal = len(records)+1
                record = observe(frontend,probe["trace_address"]+((ordinal-1)%512)*len(trace_fields)*4,trace_fields,0x38564B44)
                if record is None: break
                if record["ordinal"] != ordinal: raise AssertionError("N08 trace overwritten before observation")
                records.append(record)
            if probe["status"] >= 2:
                if (probe["counter_hz_numerator"],probe["counter_hz_denominator"]) != (33513982,64): raise AssertionError("N08 DS counter frequency descriptor changed")
                if frontend.core.retro_get_memory_size(2) != 4194304 or probe["is_dsi"]: raise AssertionError("N08 did not execute ordinary DS mode")
                observations.append(probe)
            if probe["status"] == 2:
                if live_start is None: live_start = frame
                if frame-live_start >= 90: press_pause = True
            if probe["status"] == 3:
                if pause_start is None:
                    pause_start = frame
                elif not press_resume and (probe["native_now"],probe["callback_total"]) != (observations[-2]["native_now"],observations[-2]["callback_total"]): raise AssertionError("N08 held Start pause advanced world")
                if frame-pause_start >= 3 and "paused_images" not in report:
                    report["paused_images"] = frontend.capture("paused")
            if probe["status"] == 4 and resume_start is None:
                if not press_resume: raise AssertionError("N08 held Start resumed without a new press")
                resume_start = frame
            if probe["status"] == 5:
                final = probe; completed += 1
                if completed == 3: break
        if final is None or pause_start is None or resume_start is None: raise AssertionError("N08 interactive pause/resume did not complete")
        if len(records) != final["trace_total"]: raise AssertionError("N08 missing published trace records")
        report["traces"] = records
        report["trace_checks"] = verify_traces(records)
        report["memory"] = verify_memory(final,args.elf)
        report["observations"] = observations
        report["inputs"] = inputs
        report["images"] = frontend.capture("complete")
        if final["checks"] < 1000 or final["clock_cases"] < 3 or final["event_cases"] < 5: raise AssertionError("N08 shared ARM9 suite coverage missing")
        for field in ("checks","failures","first_failed_line","clock_cases","event_cases"):
            if final[field] != host["shared_suite"][field]: raise AssertionError("N08 ARM9 shared suite differs from passed host suite")
        raw = [u64(row,"raw_counter") for row in observations]
        if any(b<a for a,b in zip(raw,raw[1:])): raise AssertionError("N08 observed platform counter regressed")
        # VBlank is 560190 bus ticks in the core's DS model. Take a long span
        # so the publication position within a frame cannot dominate this
        # independent divider/configuration check. This is not a DS quartz test.
        first,last = observations[0],observations[-1]
        vblanks = last["vblank_count"]-first["vblank_count"]
        expected_counts = Fraction(vblanks*560190,64)
        actual_counts = u64(last,"raw_counter")-u64(first,"raw_counter")
        if vblanks<120 or abs(actual_counts-expected_counts)>expected_counts/50: raise AssertionError("N08 counter divider differs from DS model VBlank cadence")
        report["counter_cadence"] = dict(vblanks=vblanks,counts=actual_counts,
                                          expected_counts_numerator=expected_counts.numerator,
                                          expected_counts_denominator=expected_counts.denominator,
                                          relative_tolerance="1/50")
        for row in host["sources"]+host["arm9_build"]["artifacts"]:
            if transport.sha(ROOT/row["path"]) != row["sha256"]: raise ValueError("N08 input/artifact changed during emulator run")
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    finally:
        if frontend:
            report["core_options"] = {key:value.decode() for key,value in frontend.variables.items()}
            report["frontend_frames"] = frontend.frames
            if not report["passed"] and frontend.last_video: report["failure_images"] = frontend.capture("failure")
            try: frontend.close()
            except Exception: report["cleanup_error"] = traceback.format_exc(); report["passed"] = False
        target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
    print(f"N08 emulator: {'PASS' if report['passed'] else 'FAIL'}; {target}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
