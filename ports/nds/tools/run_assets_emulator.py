"""Execute N04's FAT/DLDI resource reader in the real diagnostic ARM9 ROM.

Inputs are regular FAT16 image files; observation reads main RAM only.
Each boot scenario uses a fresh process and isolated libretro directories.
"""

import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import traceback

import make_assets_fixture as fixtures
import run_diag_emulator as transport

ROOT = Path(__file__).resolve().parents[3]
HEADER = ROOT / "ports/nds/platform/assets/assets_probe.h"
SCENARIOS = ("happy", "missing-media", "missing-package")


def fields_from_header():
    text = HEADER.read_text(encoding="utf-8")
    body = re.search(r"typedef struct NdsAssetsProbe\s*\{(.*?)\}", text, re.S).group(1)
    fields = []
    for declaration in re.findall(r"uint32_t\s+([^;]+);", body):
        fields.extend(name.strip() for name in declaration.split(","))
    if len(fields) != 36 or any(not name.isidentifier() for name in fields):
        raise ValueError("Unexpected N04 observation ABI")
    return fields


def read_observation(f, address, fields):
    ram, size = f.core.retro_get_memory_data(2), f.core.retro_get_memory_size(2)
    offset, length = address - 0x02000000, len(fields) * 4
    if not ram or not 0 <= offset <= size - length:
        raise RuntimeError("Probe is outside exposed main RAM")
    first = C.c_uint32.from_address(ram + offset + 12).value
    raw = C.string_at(ram + offset, length)
    last = C.c_uint32.from_address(ram + offset + 12).value
    state = dict(zip(fields, struct.unpack("<" + "I" * len(fields), raw)))
    if first != last or first != state["sequence"] or first & 1:
        return None
    return state


def verify_cached_static(f, state, package):
    data = package.read_bytes()
    count = struct.unpack_from("<I", data, 16)[0]
    record = None
    for i in range(count):
        row = data[160 + i * 128:160 + (i + 1) * 128]
        if struct.unpack_from("<I", row)[0] == state["cached_id"]:
            record = row
            break
    f.check("cached ID exists in independent package catalog", record is not None)
    offset, length = struct.unpack_from("<2I", record, 8)
    representation = struct.unpack_from("<H", record, 16)[0]
    width, height = struct.unpack_from("<2H", record, 24)
    f.check("cached item is RU aspect-correct static", representation == 6 and record[18] == 1 and
            (width, height, length, state["cached_bytes"]) == (256, 192, 49152, 49152))
    relative = state["cached_base"] - 0x02000000
    f.check("cached bytes lie inside declared pool and main RAM",
            state["asset_pool_base"] <= state["cached_base"] and
            state["cached_base"] + length <= state["asset_pool_base"] + state["asset_pool_bytes"] and
            0 <= relative <= f.core.retro_get_memory_size(2) - length)
    actual = C.string_at(f.core.retro_get_memory_data(2) + relative, length)
    expected = data[offset:offset + length]
    f.check("all cached bytes independently match actual package payload",
            actual == expected and hashlib.sha256(actual).digest() == record[40:72],
            {"checked_bytes": length, "sha256": hashlib.sha256(actual).hexdigest()})


def verify(f, state, scenario, package):
    f.check("normal DS main RAM", f.core.retro_get_memory_size(2) == 4194304)
    f.check("no unexpected ARM9 errors", state["error_bits"] == 0, state)
    f.check("640 KiB reserve inside accepted ARM9 policy", state["reserve_bytes"] == 655360 and
            state["heap_limit_before"] == 0x023F4000 and state["heap_limit_final"] == 0x022E0000)
    f.check("SDK heap growth headroom retained", state["sdk_heap_available"] >= 196608)
    f.check("asset pool and workspaces respect N03 ceilings",
            state["asset_pool_bytes"] == 1048576 and
            state["asset_highwater"] <= state["asset_pool_bytes"] and
            state["workspace_bytes"] == 114688 <= 131072 and state["static_bytes"] <= 524288)
    if scenario == "missing-media":
        f.check("no-media boot handled", state["status"] == 3 and
                state["tests_pass_mask"] == 0x2000 and state["entry_count"] == 0)
    elif scenario == "missing-package":
        f.check("mounted media without package handled", state["status"] == 4 and
                state["open_status"] == 11 and state["tests_pass_mask"] == 0x2000 and
                state["entry_count"] == 0)
    else:
        f.check("complete ARM9 self-test groups", state["status"] == 1 and
                state["tests_pass_mask"] == 0x3FFF and state["open_status"] == 0)
        f.check("all sixteen representations of both languages", state["type_mask"] == 0xFFFF and
                state["entry_count"] == 1466 and state["raw_count"] == 292)
        f.check("actual prepared package size", state["file_bytes"] == package.stat().st_size == 9230016)
        f.check("bounded real FAT reads", state["read_calls"] > 0 and state["read_bytes"] > 0 and
                0 < state["max_request"] <= 65536)
        f.check("candidate plus published backing fit one pool",
                state["asset_highwater"] == 262144 <= state["asset_pool_bytes"])
        f.check("pinned replacement refused without generation wrap",
                state["pinned_refusal"] == 13 and state["generation"] > 0)
        f.check("actual long title clip streamed in eight checked chunks",
                (state["long_sample_count"], state["long_chunks"], state["long_read_samples"]) ==
                (474361, 8, 474361))
        f.check("actual largest GPU texture loaded", state["largest_gpu_bytes"] == 131072)
        f.check("all eight negative/control cases", state["negative_pass_mask"] == 0xFF)
        verify_cached_static(f, state, package)


def worker(args):
    directory = args.out / args.scenario
    report_path = fixtures.safe_output(directory / "result.json")
    directory.mkdir(parents=True, exist_ok=True)
    transport.write_json(report_path, {"task": "N04", "passed": False, "status": "starting"})
    report = {"schema_version": 1, "task": "N04", "scenario": args.scenario,
              "passed": False, "python": sys.version.split()[0], "command": sys.argv,
              "rom_sha256": transport.sha(args.rom), "elf_sha256": transport.sha(args.elf),
              "package_sha256": transport.sha(args.package), "core_sha256": transport.sha(args.core),
              "harness_sha256": transport.sha(Path(__file__)),
              "fixture_tool_sha256": transport.sha(Path(fixtures.__file__)),
              "transport_sha256": transport.sha(Path(transport.__file__)),
              "abi_header_sha256": transport.sha(HEADER), "observations": [],
              "limits": ["Emulator functional evidence; physical DS/DS Lite remains N24.",
                         "Actual long PCM bytes are streamed, not played; final audio is N18.",
                         "Synthetic negative files test reader faults, not original game assets.",
                         "No renderer, full-game working-set peak, FPS or physical microSD latency claim."]}
    f = None
    try:
        fields = fields_from_header()
        address, size = transport.elf_symbol(args.elf, "g_nds_assets_probe")
        if size != 144:
            raise ValueError("ELF probe size differs from expected ABI")
        report["observation_symbol"] = {"address": address, "bytes": size, "access": "read-only"}
        for subdir in ("system", "save"):
            fixtures.safe_output(directory / subdir / "reserved-name")
        f = transport.Frontend(args.core, directory, args.scenario)
        if args.scenario != "missing-media":
            report["fixture"] = fixtures.make_fixture(args.package,
                f.save / "melonDS DS/dldi_sd_card.bin", include_package=args.scenario != "missing-package")
        f.load(args.rom)
        report["core_name"] = f.info.library_name.decode()
        report["core_version"] = f.info.library_version.decode()
        if "fixture" in report:
            report["fixture"]["host_stdio"] = transport.flush_native_stdio()
            report["fixture"]["sha256_after_load"] = transport.sha(Path(report["fixture"]["image"]))
            report["fixture"]["load_mutation_scope"] = "Core direct boot injects ROM into FAT before ARM9 execution."
        for _ in range(9000):
            if f.frame_time and f.frame_time.callback:
                f.frame_time.callback(round(1000000 / f.av.timing.fps))
            f.core.retro_run()
            f.frames += 1
            if f.callback_error or f.shutdown:
                raise RuntimeError(f.callback_error or "Core shutdown")
            state = read_observation(f, address, fields)
            if state and state["magic"] == 0x34414B44:
                report["last_progress"] = state
                if state["status"]:
                    if state["abi_version"] != 1 or state["size_bytes"] != size:
                        raise AssertionError("Runtime observation ABI differs")
                    if state["status"] == 2:
                        raise AssertionError(f"ARM9 self-test failure: {state}")
                    state["frontend_frame"] = f.frames
                    report["observations"].append(state)
                    if len(report["observations"]) >= 30:
                        break
        f.check("thirty coherent completed observations", len(report["observations"]) == 30)
        first, last = report["observations"][0], report["observations"][-1]
        f.check("probe continues after package handling", last["frame"] > first["frame"] and
                last["sequence"] > first["sequence"])
        verify(f, last, args.scenario, args.package)
        f.check("two native screen callbacks", f.last_video and f.last_video[:2] == (256, 384))
        report["last_observation"] = last
        report["images"] = f.capture("final")
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    finally:
        if f:
            report["checks"] = f.checks
            report["frames_run"] = f.frames
            report["core_options"] = {key: value.decode() for key, value in f.variables.items()}
            if not report["passed"]:
                report["failure_images"] = f.capture("failure")
            try:
                if "fixture" in report:
                    transport.flush_native_stdio()
                    report["fixture"]["sha256_after_run"] = transport.sha(Path(report["fixture"]["image"]))
                f.close()
                if "fixture" in report:
                    transport.flush_native_stdio()
                    item = report["fixture"]
                    item["sha256_after_unload"] = transport.sha(Path(item["image"]))
                    item["runtime_unchanged"] = item["sha256_after_load"] == item["sha256_after_run"] == item["sha256_after_unload"]
                    if not item["runtime_unchanged"]:
                        report["passed"] = False
            except Exception:
                report["cleanup_error"] = traceback.format_exc()
                report["passed"] = False
        transport.write_json(report_path, report)
    print(f"N04 {args.scenario}: {'PASS' if report['passed'] else 'FAIL'}; {report_path}")
    return 0 if report["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=ROOT / "build/nds/n04/dok_nds_assets.nds")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/nds/n04/dok_nds_assets.elf")
    parser.add_argument("--package", type=Path, default=ROOT / "build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--out", type=Path, default=ROOT / "build/nds/n04/emulator")
    parser.add_argument("--scenario", choices=SCENARIOS, help=argparse.SUPPRESS)
    args = parser.parse_args()
    for name in ("core", "rom", "elf", "package", "out"):
        setattr(args, name, getattr(args, name).absolute())
    fixtures.safe_output(args.out / "result.json")
    if args.scenario:
        return worker(args)
    args.out.mkdir(parents=True, exist_ok=True)
    transport.write_json(args.out / "result.json", {"task": "N04", "passed": False, "status": "starting"})
    cases = []
    for scenario in SCENARIOS:
        command = [sys.executable, "-X", "utf8", str(Path(__file__).resolve()),
                   "--core", str(args.core), "--rom", str(args.rom), "--elf", str(args.elf),
                   "--package", str(args.package), "--out", str(args.out), "--scenario", scenario]
        log = fixtures.safe_output(args.out / f"{scenario}.log")
        with log.open("w", encoding="utf-8") as stream:
            try:
                completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=240)
                code = completed.returncode
            except subprocess.TimeoutExpired:
                code = -1
        path = args.out / scenario / "result.json"
        value = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {}
        cases.append({"scenario": scenario, "passed": code == 0 and value.get("passed", False),
                      "exit_code": code, "report": str(path), "log": str(log)})
        print(f"N04 {scenario}: {'PASS' if cases[-1]['passed'] else 'FAIL'}", flush=True)
    report = {"schema_version": 1, "task": "N04", "passed": all(c["passed"] for c in cases),
              "scenarios": cases, "rom_sha256": transport.sha(args.rom),
              "elf_sha256": transport.sha(args.elf), "package_sha256": transport.sha(args.package),
              "harness_sha256": transport.sha(Path(__file__)), "hardware": "deferred N24"}
    transport.write_json(args.out / "result.json", report)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
