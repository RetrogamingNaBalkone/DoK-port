#!/usr/bin/env python3
"""N07 strict host/original-data/ARM9 build gate; emulator is separate.

Never reconvert assets or rebuild/retest accepted N02-N06 targets. All prior
artifact and available source-manifest pins are verified before and after.
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
OUT=ROOT/"build/nds/n07"
EVIDENCE=ROOT/"research/evidence/nds/n07"
PACKAGE_SHA="2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def safe_output(path: Path, root: Path) -> Path:
    path=path.absolute()
    if ".." in path.parts or path==root or not path.is_relative_to(root):
        raise ValueError("N07 output escapes its fixed root")
    for ancestor in (path,*path.parents):
        if ancestor==ROOT:break
        if ancestor.is_symlink() or ancestor.is_junction():
            raise ValueError(f"N07 output crosses alias: {ancestor}")
    if path.exists() and (not path.is_file() or path.stat().st_nlink!=1):
        raise ValueError(f"N07 output is not an unaliased regular file: {path}")
    path.parent.mkdir(parents=True,exist_ok=True)
    return path


def artifact(path: Path) -> dict:
    return dict(path=relative(path),bytes=path.stat().st_size,sha256=digest(path))


def main() -> int:
    local=Path(os.environ.get("LOCALAPPDATA",Path.home()/"AppData/Local"))
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig",type=Path,default=local/"Programs/Zig/0.13.0/zig.exe")
    parser.add_argument("--host-only",action="store_true",help="skip only ARM9 SDK build")
    args=parser.parse_args()
    target=safe_output(EVIDENCE/"host-check.json",EVIDENCE)
    report=dict(schema_version=1,task="N07",passed=False,python=sys.version,
        platform=platform.platform(),commands=[],sources=[],artifacts=[],preserved=[],
        scope="host_only" if args.host_only else "host_original_data_arm9_build",
        limitations=["Linux x86_64-musl executables are compiled and linked, not executed.",
            "Emulator renderer execution is a separate gate; physical DS/DS Lite/FPS remain N24.",
            "N02 has no accepted source manifest; its artifacts/reproducibility report and N03-pinned transport are verified."])
    target.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")

    def run(argv: list[str],timeout: int=600) -> str:
        command=dict(argv=argv,cwd=str(ROOT));report["commands"].append(command)
        print("Running:",subprocess.list2cmdline(argv),flush=True)
        start=time.monotonic()
        try:
            result=subprocess.run(argv,cwd=ROOT,capture_output=True,text=True,
                encoding="utf8",errors="replace",timeout=timeout)
        except subprocess.TimeoutExpired:
            command.update(timed_out=True,seconds=time.monotonic()-start)
            raise
        command.update(exit_code=result.returncode,seconds=time.monotonic()-start,
            stdout=result.stdout,stderr=result.stderr)
        if result.stdout:print(result.stdout,end="",flush=True)
        if result.stderr:print(result.stderr,end="",file=sys.stderr,flush=True)
        if result.returncode:raise RuntimeError(f"Command failed: {argv[0]} exit{result.returncode}")
        return result.stdout

    preserved: dict[str,dict]={}

    def pin(row: dict) -> None:
        path=ROOT/row["path"]
        actual=artifact(path)
        if actual["sha256"]!=row["sha256"] or ("bytes" in row and actual["bytes"]!=row["bytes"]):
            raise ValueError(f"Accepted previous input changed: {row['path']}")
        preserved[actual["path"]]=actual

    try:
        pin(dict(path="build/nds/assets/dok-assets.ndspak",sha256=PACKAGE_SHA))
        for task in ("n02-platform","n03-architecture","n04-assets","n05-balance","n06-visual"):
            summary_path=ROOT/f"analysis/nds-{task}.json"
            preserved[relative(summary_path)]=artifact(summary_path)
            summary=json.loads(summary_path.read_text("utf8"))
            for row in summary.get("artifacts",[]):pin(row)
            manifest=summary.get("source_manifest")
            if isinstance(manifest,str):
                manifest=dict(path=manifest,sha256=summary["source_manifest_sha256"])
            if isinstance(manifest,dict):
                pin(manifest);content=json.loads((ROOT/manifest["path"]).read_text("utf8"))
                rows=content if isinstance(content,list) else content.get("files",content.get("sources",[]))
                if not rows:raise ValueError(f"Empty accepted source manifest: {task}")
                for row in rows:pin(row)
            if task=="n02-platform":
                proof=summary["reproducibility"]
                pin(dict(path=proof["report"],sha256=proof["report_sha256"]))
            if task=="n05-balance":
                proof=summary["emulator"]["report"];pin(proof)
                for golden in json.loads((ROOT/proof["path"]).read_text("utf8"))["goldens"]:
                    path=Path(golden["path"])
                    pin(dict(path=relative(path),sha256=golden["sha256"]))
        report["preserved"]=list(preserved.values())
        source_paths=[]
        for directory in ("ports/nds/render","ports/nds/platform/render_probe"):
            source_paths.extend(p for p in (ROOT/directory).glob("*") if p.suffix in (".c",".h"))
        names=["ports/nds/Makefile.n07","ports/nds/tools/build_renderer.ps1",
            "ports/nds/tools/check_renderer.py","ports/nds/tools/prepare_renderer.py",
            "ports/nds/tools/audit_gpu_sdk.py","ports/nds/tools/audit_geometry.py",
            "ports/nds/tools/check_render_host.py","ports/nds/tools/run_renderer_emulator.py",
            "ports/nds/tools/check_merge.py",
            "ports/nds/tests/geometry_dump.c","ports/nds/tests/test_geometry.c",
            "ports/nds/tests/render_dump.c","ports/nds/tests/test_render_scene.c",
            "ports/nds/tests/render_compile.c","ports/nds/core/world.c",
            "ports/nds/core/balance.c","ports/nds/core/assets.c","ports/nds/core/memory.c",
            "ports/nds/core/sha256.c","ports/nds/platform/assets/file_reader.c"]
        source_paths.extend(ROOT/name for name in names)
        source_paths.extend((ROOT/"ports/nds/include/dok").glob("render*.h"))
        source_paths.append(ROOT/"ports/nds/include/dok/geometry.h")
        report["sources"]=[artifact(p) for p in sorted(set(source_paths))]
        version=run([str(args.zig),"version"]).strip()
        if version!="0.13.0":raise ValueError(f"Shared Zig version differs: {version}")
        report["zig"]=dict(path=str(args.zig),version=version,sha256=digest(args.zig))
        run([sys.executable,"-B","-X","utf8","ports/nds/tools/prepare_renderer.py","--provisional"])
        flags=["-std=c11","-pedantic-errors","-Wall","-Wextra","-Werror","-O2","-g",
            "-I","ports/nds/include","-I","build/nds/n07/generated"]
        base=["ports/nds/render/geometry.c","ports/nds/render/scene.c","ports/nds/render/materials.c",
            "ports/nds/core/world.c","ports/nds/core/balance.c","ports/nds/core/assets.c",
            "ports/nds/core/memory.c","ports/nds/core/sha256.c","ports/nds/platform/assets/file_reader.c"]
        for name in ("geometry_dump","test_geometry","render_dump","test_render_scene"):
            native=safe_output(OUT/f"{name}.exe",OUT)
            linux=safe_output(OUT/f"{name}-linux-x86_64",OUT)
            sources=[*base,f"ports/nds/tests/{name}.c"]
            for path,extra in ((native,[]),(linux,["-target","x86_64-linux-musl","-static"])):
                path.unlink(missing_ok=True)
                run([str(args.zig),"cc",*flags,*extra,*sources,"-o",str(path)])
                report["artifacts"].append(artifact(path))
            if name.startswith("test_"):
                report[name]=run([str(native)])
        for label,extra in (("native",[]),("linux-x86_64",["-target","x86_64-linux-musl"])):
            output=safe_output(OUT/f"render-compile-{label}.o",OUT)
            output.unlink(missing_ok=True)
            run([str(args.zig),"cc",*flags,*extra,"-c","ports/nds/tests/render_compile.c","-o",str(output)])
            report["artifacts"].append(artifact(output))
        run([sys.executable,"-B","-X","utf8","ports/nds/tools/audit_geometry.py","--dump",str(OUT/"geometry_dump.exe")])
        run([sys.executable,"-B","-X","utf8","ports/nds/tools/check_render_host.py","--dump",str(OUT/"render_dump.exe"),"--step","1"])
        run([sys.executable,"-B","-X","utf8","ports/nds/tools/prepare_renderer.py"])
        run([sys.executable,"-B","-X","utf8","ports/nds/tools/audit_gpu_sdk.py"])
        for name in ("geometry-native-report.json","scene-host-report.json","sdk-gpu-sources.json"):
            path=EVIDENCE/name
            if not json.loads(path.read_text("utf8")).get("passed"):
                raise ValueError(f"N07 evidence did not pass: {name}")
            report[name.removesuffix(".json").replace("-","_")]=artifact(path)
        manifest=json.loads((OUT/"scene-manifest.json").read_text("utf8"))
        if not manifest.get("passed") or manifest.get("provisional"):
            raise ValueError("Final renderer fixtures did not pass or remain provisional")
        for name in ("scene.dat","scene-manifest.json","generated/render_trig.inc"):
            report["artifacts"].append(artifact(OUT/name))
        if not args.host_only:
            run(["powershell","-NoProfile","-ExecutionPolicy","Bypass","-File",
                "ports/nds/tools/build_renderer.ps1","-Rebuild"])
            build_path=OUT/"build-report.json";build=json.loads(build_path.read_text("utf-8-sig"))
            if not build.get("toolchain_lock_verified") or build.get("toolchain_lock_sha256")!=digest(ROOT/"ports/nds/toolchain_versions.json"):
                raise ValueError("N07 SDK build did not verify the accepted package pins")
            expected={f"build/nds/n07/dok_nds_renderer.{ext}" for ext in ("nds","elf","map")}
            if {row["path"] for row in build.get("artifacts",[])}!=expected:
                raise ValueError("Unexpected N07 ARM9 artifact set")
            for row in build["artifacts"]:
                if artifact(ROOT/row["path"])!=row:raise ValueError("ARM9 build artifact changed")
            report["arm9_build"]=dict(report=artifact(build_path),artifacts=build["artifacts"])
        for row in report["sources"]+report["preserved"]:
            if artifact(ROOT/row["path"])!=row:raise ValueError(f"Input changed during N07 gate: {row['path']}")
        source_manifest=safe_output(EVIDENCE/"source-manifest.json",EVIDENCE)
        source_manifest.write_text(json.dumps(dict(task="N07",files=report["sources"]),indent=2)+"\n",encoding="utf8")
        report["source_manifest"]=artifact(source_manifest)
        build_manifest=safe_output(EVIDENCE/"build-manifest.json",EVIDENCE)
        all_artifacts=report["artifacts"]+report.get("arm9_build",{}).get("artifacts",[])
        build_manifest.write_text(json.dumps(dict(task="N07",artifacts=all_artifacts),indent=2)+"\n",encoding="utf8")
        report["build_manifest"]=artifact(build_manifest)
        report["passed"]=True
    except Exception as exc:
        report["error"]=f"{type(exc).__name__}: {exc}"
        print(report["error"],file=sys.stderr,flush=True)
    finally:
        target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
        print(f"N07 host/build passed={report['passed']}; {target}",flush=True)
    return 0 if report["passed"] else 1


if __name__=="__main__":
    raise SystemExit(main())
