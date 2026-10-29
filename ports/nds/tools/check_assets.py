#!/usr/bin/env python3
"""Build/run N04 host tests, independent RES oracle, and ARM9 build.

Linux is compiled and linked only. The ARM9 ROM is executed separately by
run_assets_emulator.py; a successful build is not a runtime assertion.
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

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/"build/nds/n04/host"
EVIDENCE=ROOT/"research/evidence/nds/n04"
SOURCES=["ports/nds/core/assets.c","ports/nds/core/sha256.c","ports/nds/tests/test_assets.c"]
HEADER_GATE="ports/nds/tests/assets_compile.c"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_directory(path: Path) -> None:
    current=ROOT
    for component in path.relative_to(ROOT).parts:
        current/=component
        if current.is_symlink() or (hasattr(current,"is_junction") and current.is_junction()):
            raise RuntimeError(f"Output path contains a link: {current}")
        if current.exists() and not current.is_dir():
            raise RuntimeError(f"Output directory is a file: {current}")
        current.mkdir(exist_ok=True)


def regular_output(path: Path) -> None:
    if path.is_symlink() or (hasattr(path,"is_junction") and path.is_junction()):
        raise RuntimeError(f"Output file is a link: {path}")
    if path.exists() and not path.is_file():
        raise RuntimeError(f"Output is not a regular file: {path}")


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    local=Path(os.environ.get("LOCALAPPDATA",Path.home()/"AppData/Local"))
    parser.add_argument("--zig",type=Path,default=local/"Programs/Zig/0.13.0/zig.exe")
    parser.add_argument("--package",type=Path,default=ROOT/"build/nds/assets/dok-assets.ndspak")
    parser.add_argument("--host-only",action="store_true",help="Skip ARM9 builder; not full N04 validation")
    parser.add_argument("--skip-oracle",action="store_true",help="Build/test only; previous oracle report is not claimed fresh")
    args=parser.parse_args()
    safe_directory(OUT);safe_directory(EVIDENCE)
    report_path=EVIDENCE/"host-check.json"
    regular_output(report_path);report_path.unlink(missing_ok=True)
    report=dict(schema_version=1,task="N04",passed=False,
                scope="host_only" if args.host_only else "host_and_arm9_build",
                python=sys.version,host=platform.platform(),commands=[],sources=[],artifacts=[],limitations=[
                    "Linux x86_64 binaries are compiled and linked, not executed.",
                    "ARM9 emulator execution and visual acceptance have separate evidence.",
                    "Host/ARM9 agreement is not proof of original DOS behavior or physical DS performance."])

    def run(command: list[str],timeout: int=300) -> str:
        record=dict(argv=command,cwd=str(ROOT));report["commands"].append(record)
        print("Running:",subprocess.list2cmdline(command),flush=True)
        start=time.monotonic()
        try:
            completed=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,
                                     encoding="utf-8",errors="replace",timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            record.update(timed_out=True,seconds=time.monotonic()-start)
            raise RuntimeError(f"Command exceeded {timeout}s: {command[0]}") from exc
        record.update(exit_code=completed.returncode,seconds=time.monotonic()-start,
                      stdout=completed.stdout,stderr=completed.stderr)
        if completed.stdout:print(completed.stdout,end="",flush=True)
        if completed.stderr:print(completed.stderr,end="",file=sys.stderr,flush=True)
        if completed.returncode:raise RuntimeError(f"Command failed: exit {completed.returncode}")
        return completed.stdout

    try:
        version=run([str(args.zig),"version"]).strip()
        if version!="0.13.0":raise RuntimeError(f"Expected shared Zig 0.13.0, got {version!r}")
        report["zig"]=dict(path=str(args.zig.resolve()),version=version,sha256=digest(args.zig))
        report["python_executable"]=dict(path=sys.executable,sha256=digest(Path(sys.executable)))
        if not args.skip_oracle:
            run([sys.executable,"-X","utf8","ports/nds/tools/audit_assets.py","--package",str(args.package),"--rebuild-twice"])
            oracle_path=EVIDENCE/"audit-report.json"
            oracle=json.loads(oracle_path.read_text(encoding="utf-8"))
            if not oracle.get("passed") or not oracle.get("reproducibility",{}).get("full_byte_compare"):
                raise RuntimeError("Independent oracle/reproducibility gate did not pass")
            report["oracle"]=dict(path=str(oracle_path.relative_to(ROOT)).replace("\\","/"),sha256=digest(oracle_path),package=oracle["package"])
        package_before=digest(args.package)
        report["package"]=dict(path=str(args.package.resolve().relative_to(ROOT)).replace("\\","/"),sha256=package_before,bytes=args.package.stat().st_size)
        source_paths=set(SOURCES+[HEADER_GATE,"ports/nds/tools/check_assets.py"])
        source_paths.update(str(p.relative_to(ROOT)).replace("\\","/") for p in (ROOT/"ports/nds/include").rglob("*.h"))
        for relative in sorted(source_paths):
            path=ROOT/relative
            report["sources"].append(dict(path=relative,bytes=path.stat().st_size,sha256=digest(path)))
        flags=["-std=c11","-pedantic-errors","-Wall","-Wextra","-Werror","-O2","-g","-I","ports/nds/include"]
        native=OUT/("test_assets.exe" if os.name=="nt" else "test_assets")
        linux=OUT/"test_assets-linux-x86_64"
        for output in (native,linux):regular_output(output);output.unlink(missing_ok=True)
        run([str(args.zig),"cc",*flags,*SOURCES,"-o",str(native)])
        stdout=run([str(native),str(args.package.resolve())])
        try:
            parsed=json.loads(stdout)
            report["host_assertions"]=parsed
            if parsed.get("passed") is False:raise RuntimeError("Native assertions reported false")
        except json.JSONDecodeError:
            report["host_stdout_format"]="text; success is checked by process exit status"
        run([str(args.zig),"cc",*flags,"-target","x86_64-linux-musl","-static",*SOURCES,"-o",str(linux)])
        for output in (native,linux):
            report["artifacts"].append(dict(path=str(output.relative_to(ROOT)).replace("\\","/"),bytes=output.stat().st_size,sha256=digest(output)))
        for name,extra in (("native",[]),("linux-x86_64",["-target","x86_64-linux-musl"])):
            output=OUT/f"assets-compile-{name}.o"
            regular_output(output);output.unlink(missing_ok=True)
            run([str(args.zig),"cc",*flags,*extra,"-c",HEADER_GATE,"-o",str(output)])
            report["artifacts"].append(dict(path=str(output.relative_to(ROOT)).replace("\\","/"),bytes=output.stat().st_size,sha256=digest(output)))
        if not args.host_only:
            run(["powershell","-NoProfile","-ExecutionPolicy","Bypass","-File","ports/nds/tools/build_assets.ps1","-Rebuild"])
            build_path=ROOT/"build/nds/n04/build-report.json"
            build=json.loads(build_path.read_text(encoding="utf-8-sig"))
            if not build.get("toolchain_lock_verified"):raise RuntimeError("ARM9 builder did not verify SDK pin")
            expected={f"build/nds/n04/dok_nds_assets.{ext}" for ext in ("nds","elf","map")}
            if {row["path"] for row in build.get("artifacts",[])}!=expected:
                raise RuntimeError("ARM9 builder returned unexpected artifacts")
            pin=ROOT/"ports/nds/toolchain_versions.json"
            if build["toolchain_lock_sha256"]!=digest(pin):raise RuntimeError("ARM9 toolchain lock hash mismatch")
            for row in build["artifacts"]:
                path=ROOT/row["path"]
                if path.stat().st_size!=row["bytes"] or digest(path)!=row["sha256"]:
                    raise RuntimeError(f"ARM9 artifact differs from report: {row['path']}")
            report["arm9_build"]=dict(report=str(build_path.relative_to(ROOT)).replace("\\","/"),
                                      report_sha256=digest(build_path),toolchain_lock_sha256=build["toolchain_lock_sha256"],artifacts=build["artifacts"])
        if digest(args.package)!=package_before:raise RuntimeError("Package changed during host/ARM9 check")
        for row in report["sources"]:
            if digest(ROOT/row["path"])!=row["sha256"]:raise RuntimeError(f"Source changed during validation: {row['path']}")
        report["passed"]=True
    except Exception as exc:
        report["error"]=f"{type(exc).__name__}: {exc}"
        print(report["error"],file=sys.stderr,flush=True)
    finally:
        report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(f"N04 host/build passed={report['passed']}; report={report_path}",flush=True)
    return 0 if report["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
