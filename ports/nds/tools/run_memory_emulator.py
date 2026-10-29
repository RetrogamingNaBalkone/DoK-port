"""Execute N03's real ARM9 memory probe, observing RAM without writing it.

Reuse the unchanged N02 libretro frontend for transport; the probe ABI,
budget assertions and managed-memory byte checks here are N03-specific.
"""

import argparse
import ctypes as C
import json
from pathlib import Path
import re
import struct
import sys
import traceback

import run_diag_emulator as transport

ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "ports/nds/platform/memory/memory_probe.h"
CAPACITIES = (393216, 65536, 131072, 262144, 1048576, 262144, 131072)


def abi_fields():
    text = HEADER.read_text(encoding="utf-8")
    body = re.search(r"typedef struct NdsMemoryProbe \{(.*?)\} NdsMemoryProbe;",
                     text, re.S).group(1)
    fields = re.findall(r"uint32_t\s+(\w+)\s*;", body)
    tail = re.search(r"typedef struct NdsMemoryArenaProbe \{(.*?)\}", text, re.S).group(1)
    arena_fields = re.findall(r"uint32_t\s+(\w+)\s*;", tail)
    return fields + [f"arena_{i}_{field}" for i in range(7) for field in arena_fields]


def read_observation(frontend, address, fields):
    pointer = frontend.core.retro_get_memory_data(2)
    ram_size = frontend.core.retro_get_memory_size(2)
    length = len(fields) * 4
    offset = address - 0x02000000
    if not pointer or not 0 <= offset <= ram_size - length:
        raise RuntimeError("Probe is outside exposed main RAM")
    first = C.c_uint32.from_address(pointer + offset + 12).value
    raw = C.string_at(pointer + offset, length)
    last = C.c_uint32.from_address(pointer + offset + 12).value
    state = dict(zip(fields, struct.unpack("<" + "I" * len(fields), raw)))
    if first != last or first != state["sequence"] or first & 1:
        return None
    return state


def verify(f, state, elf):
    check = f.check
    check("all nine ARM9 self-test groups passed", state["status"] == 1 and
          state["error_bits"] == 0 and state["tests_pass_mask"] == 0x1FF, state)
    check("DS mode and physical main RAM", state["is_dsi"] == 0 and
          f.core.retro_get_memory_size(2) == 4194304)
    check("ARM9 linker region", (state["region_base"], state["region_limit"],
          state["region_bytes"]) == (0x02000000, 0x02380000, 3670016))
    for field, symbol in (("static_end", "__end__"), ("region_limit", "__eheap_end"),
                          ("itcm_base", "__itcm_start"), ("itcm_end", "__itcm_end"),
                          ("dtcm_base", "__dtcm_start"), ("dtcm_top", "__dtcm_top"),
                          ("dtcm_sbss_end", "__sbss_end"), ("user_stack_top", "__sp_usr")):
        expected, _size = transport.elf_symbol(elf, symbol)
        check(f"runtime agrees with ELF symbol {symbol}", state[field] == expected,
              {"runtime": state[field], "elf": expected})
    check("static bounds and ceiling", state["static_bytes"] == state["static_end"] -
          state["region_base"] and state["static_ceiling"] == 524288 and
          state["static_end"] <= state["heap_start"])
    check("655360-byte reserve removed from heap", state["reserve_bytes"] == 655360 and
          state["sdk_limit_exclusion_bytes"] == state["heap_limit_before"] - state["region_limit"] and
          state["heap_limit_policy"] == state["region_limit"] and
          state["heap_limit_policy"] - state["heap_limit_reduced"] == state["reserve_bytes"] and
          state["heap_limit_final"] == state["heap_limit_reduced"] and
          state["heap_end_before"] == state["heap_end_reduced"])
    check("reserve at least ten percent of ARM9 region", state["reserve_bytes"] * 10 >=
          state["region_bytes"])
    check("live aligned malloc backing", state["slab_bytes"] == sum(CAPACITIES) and
          state["malloc_request_bytes"] == state["slab_bytes"] + 31 and
          state["slab_base"] % 32 == 0 and state["heap_start"] <= state["malloc_raw"] <=
          state["slab_base"] and state["slab_base"] + state["slab_bytes"] <=
          state["malloc_raw"] + state["malloc_request_bytes"] <= state["heap_end_allocated"])
    check("heap end remains within reduced limit", state["heap_start"] <=
          state["heap_end_before"] <= state["heap_end_allocated"] <=
          state["heap_end_final"] <= state["heap_limit_final"])
    check("measured startup overhead accounted", state["startup_heap_overhead"] ==
          state["heap_end_final"] - state["heap_start"] - state["slab_bytes"] and
          state["static_and_overhead"] == state["static_bytes"] + state["startup_heap_overhead"] and
          state["static_and_overhead"] + state["code_envelope_headroom"] == state["static_ceiling"])
    check("at least 192 KiB SDK heap growth headroom", state["sdk_heap_min"] == 196608 and
          state["sdk_heap_available"] == state["heap_limit_final"] - state["heap_end_final"] and
          state["sdk_heap_available"] >= state["sdk_heap_min"])
    check("entire ARM9 byte budget closes", state["static_and_overhead"] +
          state["slab_bytes"] + state["sdk_heap_available"] + state["reserve_bytes"] ==
          state["region_bytes"])
    expected = bytearray(b"\xa5" * sum(CAPACITIES))
    offset = 0
    for i, capacity in enumerate(CAPACITIES):
        check(f"arena {i} exhausted safely and reset", state[f"arena_{i}_capacity"] == capacity and
              state[f"arena_{i}_used"] == 0 and state[f"arena_{i}_high_water"] == capacity and
              state[f"arena_{i}_failures"] == 1)
        expected[offset] = 0x10 + i
        expected[offset + capacity - 1] = 0xE0 + i
        if i == 2:
            expected[offset:offset + 32] = struct.pack("<8I", *range(1, 9))
        offset += capacity
    ram = f.core.retro_get_memory_data(2)
    check("managed byte observation lies inside exposed main RAM",
          0 <= state["slab_base"] - state["region_base"] <=
          f.core.retro_get_memory_size(2) - len(expected))
    actual = C.string_at(ram + state["slab_base"] - 0x02000000, len(expected))
    check("whole managed allocation contains ARM9-written bytes", actual == expected and
          state["touched_bytes"] == len(expected), {"checked_bytes": len(expected)})
    check("bounded work stack high-water and rejection", (state["work_stack_capacity"],
          state["work_stack_depth"], state["work_stack_high_water"],
          state["work_stack_failures"]) == (8, 0, 8, 1))
    check("ITCM and DTCM are separate from main RAM", state["itcm_base"] == 0x01000000 and
          state["itcm_used"] == state["itcm_end"] - state["itcm_base"] <= 32768 and
          state["dtcm_base"] == 0x02FF4000 and state["dtcm_top"] == 0x02FF8000)
    check("CP15 confirms DTCM base and 16 KiB size", state["cp15_dtcm"] & 0xFFFFF000 ==
          state["dtcm_base"] and (state["cp15_dtcm"] >> 1) & 31 == 5)
    check("paint excludes DTCM data and active stack", state["user_stack_bytes"] == 12288 and
          state["dtcm_sbss_end"] <= state["paint_lo"] == state["user_stack_top"] - 12288 and
          state["paint_lo"] < state["paint_hi"] <= state["observed_sp"] - 256 and
          state["observed_sp"] <= state["user_stack_top"] < state["dtcm_top"])
    check("bounded real user-stack exercise changes watermark without guard damage",
          state["paint_guard_ok"] == 1 and 0 <= state["paint_watermark_before"] <
          state["paint_watermark_after"] < state["paint_hi"] - state["paint_lo"])
    check("ARM9 executes initialized shell lifecycle", state["game_state_bytes"] > 0 and
          state["game_lifecycle_generation"] == 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=ROOT / "build/nds/n03/dok_nds_memory.nds")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/nds/n03/dok_nds_memory.elf")
    parser.add_argument("--out", type=Path, default=ROOT / "build/nds/n03/emulator")
    args = parser.parse_args()
    for name in ("core", "rom", "elf", "out"):
        setattr(args, name, getattr(args, name).resolve())
    if not args.out.is_relative_to((ROOT / "build/nds/n03").resolve()):
        parser.error("--out must stay inside build/nds/n03")
    args.out.mkdir(parents=True, exist_ok=True)
    # A crash or invalid input must not leave an earlier success as this run.
    transport.write_json(args.out / "result.json", {
        "schema_version": 1, "task": "N03", "passed": False, "status": "starting"})
    report = {"schema_version": 1, "task": "N03", "passed": False,
              "python": sys.version.split()[0],
              "command": sys.argv, "rom_sha256": transport.sha(args.rom),
              "elf_sha256": transport.sha(args.elf), "core_sha256": transport.sha(args.core),
              "harness_sha256": transport.sha(Path(__file__)),
              "transport_sha256": transport.sha(Path(transport.__file__)),
              "abi_header_sha256": transport.sha(HEADER), "observations": [],
              "limits": ["Emulator only; physical hardware acceptance is N24.",
                         "Pattern watermark covers a bounded user-stack exercise, not all C/IRQ/SVC stacks.",
                         "SDK heap headroom is growth to its break limit, not malloc free-chunk statistics.",
                         "Planning ceilings are not full-game measured memory peaks."]}
    f = None
    try:
        fields = abi_fields()
        address, size = transport.elf_symbol(args.elf, "g_nds_memory_probe")
        if len(fields) * 4 != size:
            raise AssertionError("Header/ELF probe ABI size differs")
        report["observation_symbol"] = {"address": address, "bytes": size, "access": "read-only"}
        f = transport.Frontend(args.core, args.out, "missing-media")
        f.load(args.rom)
        report["core_name"] = f.info.library_name.decode()
        report["core_version"] = f.info.library_version.decode()
        for _ in range(600):
            if f.frame_time and f.frame_time.callback:
                f.frame_time.callback(round(1000000 / f.av.timing.fps))
            f.core.retro_run()
            f.frames += 1
            if f.callback_error or f.shutdown:
                raise RuntimeError(f.callback_error or "Core shutdown")
            state = read_observation(f, address, fields)
            if state and state["magic"] == 0x334D4B44 and state["status"]:
                if state["abi_version"] != 1 or state["size_bytes"] != size:
                    raise AssertionError("Runtime ABI header differs")
                state["frontend_frame"] = f.frames
                report["observations"].append(state)
                if len(report["observations"]) >= 120:
                    break
        f.check("120 coherent observations from actual ARM9 execution",
                len(report["observations"]) == 120)
        first, last = report["observations"][0], report["observations"][-1]
        f.check("probe VBlank loop progresses", last["frame"] > first["frame"] and
                last["sequence"] > first["sequence"])
        f.check("stable results throughout observation", all(s["status"] == 1 and
                s["error_bits"] == 0 for s in report["observations"]))
        verify(f, last, args.elf)
        f.check("two native screens produced", f.last_video and f.last_video[:2] == (256, 384))
        report["images"] = f.capture("final")
        report["budget"] = {"arm9_region_bytes": last["region_bytes"],
                            "static_and_startup_bytes": last["static_and_overhead"],
                            "managed_bytes": last["slab_bytes"],
                            "sdk_heap_growth_bytes": last["sdk_heap_available"],
                            "reserve_bytes": last["reserve_bytes"],
                            "reserve_percent_arm9": last["reserve_bytes"] * 100 / last["region_bytes"]}
        report["last_observation"] = last
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    finally:
        if f:
            report["checks"] = f.checks
            report["core_options"] = {key: value.decode() for key, value in f.variables.items()}
            report["frames_run"] = f.frames
            if not report["passed"]:
                report["failure_images"] = f.capture("failure")
            try:
                f.close()
            except Exception:
                report["cleanup_error"] = traceback.format_exc()
                report["passed"] = False
        transport.write_json(args.out / "result.json", report)
    print(f"N03 emulator: {'PASS' if report['passed'] else 'FAIL'}; {args.out / 'result.json'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
