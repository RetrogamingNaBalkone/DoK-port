"""N07 meaningful pure merge checks, strict native execution and Linux link."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/"research/evidence/nds/n07/merge-tests.json"

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(report):
 for p in [OUT,*OUT.parents]:
  if p==ROOT.parent:break
  if p.is_symlink() or p.exists() and p.is_junction():raise ValueError("Report alias")
 if OUT.exists() and (not OUT.is_file() or OUT.stat().st_nlink!=1):raise ValueError("Report alias")
 OUT.parent.mkdir(parents=True,exist_ok=True)
 OUT.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")

def main():
 zig=Path(os.environ["LOCALAPPDATA"])/"Programs/Zig/0.13.0/zig.exe"
 sources=["ports/nds/tests/test_render_scene.c","ports/nds/render/scene.c","ports/nds/render/geometry.c",
  "ports/nds/core/world.c","ports/nds/core/sha256.c"]
 common=[str(zig),"cc","-std=c11","-Wall","-Wextra","-Werror","-pedantic","-O2",
  "-I","ports/nds/include","-I","build/nds/n07/generated",*sources]
 targets=[("native",[],"build/nds/n07/test_render_scene.exe"),
  ("linux",["-target","x86_64-linux-musl"],"build/nds/n07/test_render_scene-linux")]
 inputs=[ROOT/p for p in [*sources,"ports/nds/include/dok/render_scene.h","ports/nds/include/dok/render_packet.h",
  "build/nds/n07/generated/render_trig.inc","ports/nds/tools/check_merge.py"]]
 before={str(p.relative_to(ROOT)).replace("\\","/"):sha(p) for p in inputs}
 report=dict(task="N07",passed=False,scope="pure new merge adaptation; no original raster, no DS timing",sources=before,builds=[])
 save(report)
 for name,flags,output in targets:
  target=(ROOT/output)
  for ancestor in target.parents:
   if ancestor==ROOT.parent:break
   if ancestor.is_symlink() or ancestor.exists() and ancestor.is_junction():raise ValueError("Executable ancestor alias")
  if target.is_symlink() or target.exists() and (not target.is_file() or target.stat().st_nlink!=1):raise ValueError("Executable alias")
  command=[*common,*flags,"-o",output]
  process=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
  if process.returncode:raise RuntimeError(process.stdout+process.stderr)
  report["builds"].append(dict(name=name,command=command,exit=process.returncode,file=output,sha256=sha(target)))
 result=subprocess.run([ROOT/targets[0][2]],cwd=ROOT,text=True,capture_output=True)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 report["native_command"]=[str(ROOT/targets[0][2])]
 report["native_result"]=json.loads(result.stdout)
 report["linux_executed"]=False
 after={str(p.relative_to(ROOT)).replace("\\","/"):sha(p) for p in inputs}
 if after!=before:raise RuntimeError("Producer/test inputs changed during merge gate")
 if not report["native_result"]["passed"]:raise AssertionError("Merge tests failed")
 report["passed"]=True;save(report)
 print(json.dumps(dict(**report["native_result"],report=str(OUT.relative_to(ROOT))),indent=2))

if __name__=="__main__":main()
