"""N05: read all six complete initial-world exports from actual ARM9 RAM.

Only standard libretro A-button input advances a case; no emulated RAM writes.
The independent original-data oracle must already have produced six goldens.
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

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n05"
HEADER = ROOT / "ports/nds/platform/world/world_probe.h"


def safe_output(path):
    path = Path(path)
    if ".." in path.parts:
        raise ValueError("Parent traversal in N05 output")
    path = path.absolute()
    if not path.is_relative_to(OUT) or path == OUT:
        raise ValueError("N05 output must stay inside build/nds/n05")
    current = ROOT
    for component in path.parent.relative_to(ROOT).parts:
        current /= component
        if current.is_symlink() or (hasattr(current, "is_junction") and current.is_junction()):
            raise ValueError("N05 output directory alias")
        if current.exists() and not current.is_dir():
            raise ValueError("N05 output parent is not a directory")
    if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()) or (
            path.exists() and (not path.is_file() or path.stat().st_nlink != 1)):
        raise ValueError("N05 output must be a regular unlinked file")
    return path


def field_names():
    body = re.search(r"typedef struct NdsWorldProbe\s*\{(.*?)\}",
                     HEADER.read_text(encoding="utf-8"), re.S).group(1)
    fields = [name.strip() for declaration in re.findall(r"uint32_t\s+([^;]+);", body)
              for name in declaration.split(",")]
    if len(fields) != 37 or any(not name.isidentifier() for name in fields):
        raise ValueError("Unexpected N05 observation ABI")
    return fields


def ram_bytes(frontend, address, length):
    ram = frontend.core.retro_get_memory_data(2)
    size = frontend.core.retro_get_memory_size(2)
    offset = address - 0x02000000
    if not ram or length < 0 or not 0 <= offset <= size - length:
        raise ValueError("N05 observation outside exposed main RAM")
    return C.string_at(ram + offset, length)


def observe(frontend, address, fields):
    first = ram_bytes(frontend, address + 12, 4)
    raw = ram_bytes(frontend, address, len(fields) * 4)
    last = ram_bytes(frontend, address + 12, 4)
    state = dict(zip(fields, struct.unpack("<" + "I" * len(fields), raw)))
    if first != last or struct.unpack("<I", first)[0] != state["sequence"] or state["sequence"] & 1:
        return None
    if state["magic"] != 0x35574B44:
        return None
    if (state["abi_version"], state["size_bytes"]) != (1, len(fields) * 4):
        raise ValueError("N05 runtime ABI mismatch")
    return state


def difference(actual, expected, path="root"):
    if type(actual) is not type(expected):
        return path + ": type differs"
    if isinstance(actual, dict):
        if actual.keys() != expected.keys():
            return path + ": keys differ"
        for key in expected:
            found = difference(actual[key], expected[key], path + "." + key)
            if found:
                return found
    elif isinstance(actual, list):
        if len(actual) != len(expected):
            return path + ": length differs"
        for index, (left, right) in enumerate(zip(actual, expected)):
            found = difference(left, right, f"{path}[{index}]")
            if found:
                return found
    elif actual != expected:
        return path + ": " + repr(actual)[:120] + " != " + repr(expected)[:120]
    return None


def capture(frontend, name):
    for suffix in (".png", "-top.png", "-bottom.png"):
        safe_output(frontend.directory / (name + suffix))
    return frontend.capture(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=OUT / "dok_nds_world.nds")
    parser.add_argument("--elf", type=Path, default=OUT / "dok_nds_world.elf")
    parser.add_argument("--package", type=Path, default=ROOT / "build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--out", type=Path, default=OUT / "emulator")
    parser.add_argument("--oracle", type=Path, default=ROOT / "research/evidence/nds/n05/oracle")
    args = parser.parse_args()
    report_path = safe_output(args.out / "result.json")
    args.out.mkdir(parents=True, exist_ok=True)
    for subdir in ("system", "save"):
        safe_output(args.out / subdir / "reserved")
    transport.write_json(report_path, {"task": "N05", "passed": False, "status": "starting"})
    report = {"schema_version": 1, "task": "N05", "passed": False, "cases": [],
              "python": sys.version, "command": sys.argv,
              "rom_sha256": transport.sha(args.rom), "elf_sha256": transport.sha(args.elf),
              "package_sha256": transport.sha(args.package), "core_sha256": transport.sha(args.core),
              "harness_sha256": transport.sha(Path(__file__)), "abi_sha256": transport.sha(HEADER),
              "transport_sha256": transport.sha(Path(transport.__file__)),
              "fat_tool_sha256": transport.sha(Path(fat.__file__)),
              "limits": ["Initial data/seeds and localization, not a playable world or renderer.",
                         "No emulated RAM writes, only standard libretro input.",
                         "Physical DS/DS Lite, microSD latency and FPS remain N24."]}
    frontend = None
    try:
        names = field_names()
        address, size = transport.elf_symbol(args.elf, "g_nds_world_probe")
        if size != len(names) * 4:
            raise ValueError("ELF observation size differs from header")
        report["observation_symbol"] = {"address": address, "bytes": size, "access": "read-only"}
        expected = {}
        golden_pins = []
        for level in range(1, 4):
            for language in (1, 2):
                path = args.oracle / f"level{level}-lang{language}.json"
                expected[level, language] = json.loads(path.read_text(encoding="utf-8"))
                golden_pins.append({"path": str(path), "sha256": transport.sha(path)})
        report["goldens"] = golden_pins
        frontend = transport.Frontend(args.core, args.out, "happy")
        image_path = safe_output(frontend.save / "melonDS DS/dldi_sd_card.bin")
        image_path.parent.mkdir(parents=True, exist_ok=True)
        package = args.package.read_bytes()
        fat.write_image(image_path, [("assets.ndp", b"ASSETS  NDP", package)])
        report["fixture"] = {"path": str(image_path), "bytes": image_path.stat().st_size,
                             "before_load_sha256": transport.sha(image_path),
                             "format": "Regular 32MiB MBR/FAT16 image, /dok/assets.ndp"}
        frontend.load(args.rom)
        report["core_name"] = frontend.info.library_name.decode()
        report["core_version"] = frontend.info.library_version.decode()
        transport.flush_native_stdio()
        report["fixture"]["after_load_sha256"] = transport.sha(image_path)
        report["fixture"]["load_mutation_scope"] = "Core injects ROM before ARM9 direct boot."

        def step(button=0):
            frontend.joypad = button
            if frontend.frame_time and frontend.frame_time.callback:
                frontend.frame_time.callback(round(1000000 / frontend.av.timing.fps))
            frontend.core.retro_run()
            frontend.frames += 1
            if frontend.callback_error or frontend.shutdown:
                raise RuntimeError(frontend.callback_error or "Core shutdown")
            state = observe(frontend, address, names)
            if state and state["status"] == 2:
                raise AssertionError("ARM9 self-test failure: " + repr(state))
            return state

        previous = {}
        for case_id in range(1, 7):
            states = []
            for _ in range(12000):
                state = step()
                if state:
                    report["last_progress"] = state
                if state and state["status"] == 1 and state["case_id"] == case_id:
                    states.append(state)
                    if len(states) == 8:
                        break
            frontend.check(f"case {case_id}: eight coherent ready states", len(states) == 8)
            state = states[-1]
            level, language = (case_id - 1) // 2 + 1, (case_id - 1) % 2 + 1
            frontend.check(f"case {case_id}: identity", (state["level"], state["language"], state["case_count"]) == (level, language, 6))
            frontend.check(f"case {case_id}: bounded export", 0 < state["json_bytes"] <= 524288)
            raw = ram_bytes(frontend, state["json_base"], state["json_bytes"])
            actual = json.loads(raw)
            error = difference(actual, expected[level, language])
            frontend.check(f"case {case_id}: full independent original-data equality", error is None, error)
            if language == 1:
                previous[level] = {key: actual[key] for key in ("base", "effective", "world")}
            else:
                frontend.check(f"LEVEL{level}: locale leaves all world data unchanged",
                               previous[level] == {key: actual[key] for key in previous[level]})
            report["cases"].append({"case_id": case_id, "level": level, "language": language,
                                    "passed": True, "json_bytes": len(raw),
                                    "json_sha256": hashlib.sha256(raw).hexdigest(),
                                    "observation": state, "coherent_states": len(states)})
            if case_id in (1, 6):
                report.setdefault("images", []).extend(capture(frontend, f"case-{case_id}"))
            for _ in range(4):
                step(1 << 8)  # libretro A; no CPU/RAM patching
            for _ in range(4):
                step()
        final_states = []
        for _ in range(1200):
            state = step()
            if state and state["status"] == 3:
                final_states.append(state)
                if len(final_states) == 30:
                    break
        frontend.check("thirty coherent completed observations", len(final_states) == 30)
        state = final_states[-1]
        report["last_observation"] = state
        report["completed_observations"] = final_states
        frontend.check("all ARM9 groups and negative controls", state["tests_pass_mask"] == 127 and
                       state["negative_pass_mask"] == 63 and state["error_bits"] == 0)
        frontend.check("world and scratch ceilings", 0 < state["world_bytes"] <= 393216 and
                       0 < state["scratch_bytes"] <= 131072)
        frontend.check("I/O and asset ceilings", state["asset_pool_bytes"] == 1048576 and
                       0 < state["io_bytes"] <= 131072 and 0 < state["max_request"] <= 65536)
        frontend.check("SDK heap policy and emergency reserve", state["heap_limit_before"] == 0x023F4000 and
                       state["heap_limit_final"] == 0x022E0000 and state["reserve_bytes"] == 655360 and
                       state["sdk_heap_available"] >= 196608 and
                       state["static_bytes"] + state["startup_overhead"] <= 524288 and
                       state["allocated_slabs"] == 1703936)
        frontend.check("actual external FAT package", state["entry_count"] == 1466 and state["read_calls"] > 0)
        frontend.check("program continues after all cases", final_states[-1]["frame"] > final_states[0]["frame"])
        report["images"].extend(capture(frontend, "final"))
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    finally:
        if frontend:
            report["checks"] = frontend.checks
            report["frames_run"] = frontend.frames
            report["core_options"] = {k: v.decode() for k, v in frontend.variables.items()}
            if not report["passed"]:
                report["failure_images"] = capture(frontend, "failure")
            try:
                transport.flush_native_stdio()
                if "fixture" in report:
                    image = Path(report["fixture"]["path"])
                    report["fixture"]["after_run_sha256"] = transport.sha(image)
                frontend.close()
                transport.flush_native_stdio()
                if "fixture" in report:
                    item = report["fixture"]
                    item["after_unload_sha256"] = transport.sha(Path(item["path"]))
                    item["runtime_unchanged"] = item["after_load_sha256"] == item["after_run_sha256"] == item["after_unload_sha256"]
                    report["passed"] = report["passed"] and item["runtime_unchanged"]
            except Exception:
                report["cleanup_error"] = traceback.format_exc()
                report["passed"] = False
        transport.write_json(report_path, report)
    print(f"N05 emulator: {'PASS' if report['passed'] else 'FAIL'}; {report_path}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
