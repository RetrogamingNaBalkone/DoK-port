#!/usr/bin/env python3
"""Build and execute the independent N03 strict-C11 host checks.

The full command also invokes the ARM9 probe builder. Linux outputs are cross
compiled, not executed. Hardware and emulator execution are separate gates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n03/host"
EVIDENCE = ROOT / "research/evidence/nds/n03"
TARGETS = {
    "contracts": ["ports/nds/core/contracts.c", "ports/nds/tests/test_contracts.c"],
    "memory": ["ports/nds/core/memory.c", "ports/nds/tests/test_memory.c"],
}
HEADER_GATE = "ports/nds/tests/contracts_compile.c"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_directory(path: Path) -> None:
    """Refuse links at every repository-relative output path component."""
    relative = path.relative_to(ROOT)
    current = ROOT
    for component in relative.parts:
        current /= component
        if current.is_symlink() or (hasattr(current, "is_junction") and current.is_junction()):
            raise RuntimeError(f"Output path contains a link: {current}")
        if current.exists() and not current.is_dir():
            raise RuntimeError(f"Output directory is a file: {current}")
        current.mkdir(exist_ok=True)


def regular_output(path: Path) -> None:
    if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
        raise RuntimeError(f"Output file is a link: {path}")
    if path.exists() and not path.is_file():
        raise RuntimeError(f"Output is not a regular file: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    local = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    parser.add_argument("--zig", type=Path, default=local / "Programs/Zig/0.13.0/zig.exe")
    parser.add_argument("--host-only", action="store_true", help="Skip the ARM9 builder; not full N03 validation")
    args = parser.parse_args()
    safe_directory(OUT)
    safe_directory(EVIDENCE)
    report_path = EVIDENCE / "host-check.json"
    regular_output(report_path)
    report_path.unlink(missing_ok=True)
    report: dict = {
        "schema_version": 1,
        "task": "N03",
        "passed": False,
        "scope": "host_only" if args.host_only else "host_and_arm9_build",
        "python": sys.version,
        "host": platform.platform(),
        "zig": str(args.zig.resolve()),
        "commands": [],
        "artifacts": [],
        "sources": [],
        "limitations": [
            "Linux x86_64 outputs are compiled and linked, not executed.",
            "This driver does not execute ARM9 code; emulator execution has a separate report.",
            "Work-stack high-water is not CPU-stack usage or DOS behavioral equivalence.",
        ],
    }

    def run(command: list[str], timeout: int = 300) -> str:
        record: dict = {"argv": command, "cwd": str(ROOT)}
        report["commands"].append(record)
        print("Running:", subprocess.list2cmdline(command), flush=True)
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            record.update({"timed_out": True, "seconds": time.monotonic() - started})
            raise RuntimeError(f"Command exceeded {timeout}s: {command[0]}") from exc
        record.update({"exit_code": result.returncode, "seconds": time.monotonic() - started,
                       "stdout": result.stdout, "stderr": result.stderr})
        if result.stdout:
            print(result.stdout, end="", flush=True)
        if result.stderr:
            print(result.stderr, end="", file=sys.stderr, flush=True)
        if result.returncode:
            raise RuntimeError(f"Command failed with exit {result.returncode}: {command[0]}")
        return result.stdout

    try:
        version = run([str(args.zig), "version"]).strip()
        if version != "0.13.0":
            raise RuntimeError(f"Expected shared Zig 0.13.0, got {version!r}")
        report["zig_version"] = version
        report["zig_sha256"] = digest(args.zig)
        source_paths = set(p for sources in TARGETS.values() for p in sources)
        source_paths.add(HEADER_GATE)
        source_paths.add("ports/nds/tools/check_core.py")
        source_paths.update(str(p.relative_to(ROOT)).replace("\\", "/")
                            for p in (ROOT / "ports/nds/include").rglob("*.h"))
        for relative in sorted(source_paths):
            path = ROOT / relative
            report["sources"].append({"path": relative, "bytes": path.stat().st_size,
                                      "sha256": digest(path)})
        flags = ["-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
                 "-O2", "-g", "-I", "ports/nds/include"]
        for target, sources in TARGETS.items():
            native = OUT / (f"test_{target}.exe" if os.name == "nt" else f"test_{target}")
            linux = OUT / f"test_{target}-linux-x86_64"
            for output in (native, linux):
                regular_output(output)
                output.unlink(missing_ok=True)
            run([str(args.zig), "cc", *flags, *sources, "-o", str(native)])
            run([str(native)])
            run([str(args.zig), "cc", *flags, "-target", "x86_64-linux-musl",
                 "-static", *sources, "-o", str(linux)])
            for output in (native, linux):
                report["artifacts"].append({"path": str(output.relative_to(ROOT)).replace("\\", "/"),
                                            "bytes": output.stat().st_size, "sha256": digest(output)})
        for name, extra in (("native", []), ("linux-x86_64", ["-target", "x86_64-linux-musl"])):
            output = OUT / f"contracts-compile-{name}.o"
            regular_output(output)
            output.unlink(missing_ok=True)
            run([str(args.zig), "cc", *flags, *extra, "-c", HEADER_GATE, "-o", str(output)])
            report["artifacts"].append({"path": str(output.relative_to(ROOT)).replace("\\", "/"),
                                        "bytes": output.stat().st_size, "sha256": digest(output)})
        if not args.host_only:
            arm9_driver = ROOT / "ports/nds/tools/build_memory.ps1"
            run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                 str(arm9_driver), "-Rebuild"])
            arm9_report_path = ROOT / "build/nds/n03/build-report.json"
            arm9_report = json.loads(arm9_report_path.read_text(encoding="utf-8-sig"))
            if not arm9_report.get("toolchain_lock_verified"):
                raise RuntimeError("ARM9 builder did not verify its toolchain pin")
            expected = {f"build/nds/n03/dok_nds_memory.{suffix}" for suffix in ("nds", "elf", "map")}
            if {item["path"] for item in arm9_report.get("artifacts", [])} != expected:
                raise RuntimeError("ARM9 report does not contain the three expected probe artifacts")
            for item in arm9_report["artifacts"]:
                path = ROOT / item["path"]
                if path.stat().st_size != item["bytes"] or digest(path) != item["sha256"]:
                    raise RuntimeError(f"ARM9 report differs from actual artifact: {path}")
            report["arm9_build"] = {
                "report": "build/nds/n03/build-report.json",
                "report_sha256": digest(arm9_report_path),
                "toolchain_lock_sha256": arm9_report["toolchain_lock_sha256"],
                "artifacts": arm9_report["artifacts"],
            }
        report["passed"] = True
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
        print(report["error"], file=sys.stderr, flush=True)
    finally:
        report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"Report: {report_path}", flush=True)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
