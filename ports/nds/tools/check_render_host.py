"""N07 independent host material/scene packet acceptance; no SDK or GBA imports."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/"research/evidence/nds/n07"
PACKAGE=ROOT/"build/nds/assets/dok-assets.ndspak"
PACKAGE_SHA="2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511"

def sha(data): return hashlib.sha256(data).hexdigest()
def require(value,message):
 if not value: raise AssertionError(message)
def write(path,data):
 path=Path(path).absolute()
 require(path.is_relative_to(OUT) and path!=OUT and ".." not in path.parts,"N07 report path")
 for ancestor in [path,*path.parents]:
  if ancestor==ROOT.parent:break
  require(not ancestor.is_symlink() and not (ancestor.exists() and ancestor.is_junction()),"Report alias")
 require(not path.exists() or path.is_file() and path.stat().st_nlink==1,"Report file alias")
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")

def entries():
 data=PACKAGE.read_bytes();require(sha(data)==PACKAGE_SHA,"Accepted N04 package SHA")
 result={}
 for i in range(struct.unpack_from("<I",data,16)[0]):
  record=data[160+128*i:288+128*i]
  rid,_,offset,size=struct.unpack_from("<4I",record)
  rep,lang,_,index,subindex=struct.unpack_from("<HBBHH",record,16)
  w,h,sw,sh=struct.unpack_from("<4H",record,24)
  require(sha(data[offset:offset+size])==record[40:72].hex(),"Package payload SHA")
  result[rid]=dict(id=rid,name=record[104:120].split(b"\0",1)[0].hex(),rep=rep,lang=lang,index=index,
   subindex=subindex,width=w,height=h,storage_width=sw,storage_height=sh,bytes=size,sha256=record[40:72].hex())
 return result

def run(exe,level,*arguments,input=None):
 result=subprocess.run([exe,PACKAGE,str(level),*map(str,arguments)],input=input,text=True,capture_output=True)
 require(result.returncode==0,f"render_dump level{level} {' '.join(map(str,arguments))}: exit{result.returncode} {result.stderr}")
 return result.stdout

def check_bindings(case,native,model,catalog):
 world=case["world"];materials={m["material"]:m for m in world["materials"]}
 for material in materials.values():
  actual=material["entry"];expected=catalog[actual["id"]]
  require(expected["rep"]==5 and expected["lang"]==1,"GPU RU material")
  for field in ("width","height","storage_width","storage_height","bytes","sha256"):
   require(actual[field]==expected[field],f"Full texture metadata {field}")
  require(material["pixels_sha256"]==expected["sha256"],"All resident GPU pixels SHA")
  require(bytes.fromhex(actual["name_cp866"]).split(b"\0",1)[0].hex()==expected["name"],"Material name")
 expected={}
 objects={o["glyph"]:o for o in model["base"]["objects"]}
 maps=[bytes.fromhex(p) for p in model["world"]["maps"]]
 native_slots={b["glyph"]:{s["slot"]:s for s in b["slots"]} for b in native["initial_bindings"]}
 for glyph,obj in objects.items():
  slots=[]
  if glyph in maps[1] and obj["class_id"] in (2,4,7):slots.append((0,8))
  if glyph in maps[2]:slots.extend([(1,0x1c),(2,0x18),(3,0x20)])
  for output_slot,input_slot in slots:
   s=native_slots[glyph][input_slot]
   image=next(e for e in catalog.values() if e["rep"]==2 and e["lang"]==1 and e["name"]==s["image"])
   expected[glyph,output_slot]=(image,s["flags"])
 actual={(b["glyph"],b["slot"]):b for b in world["bindings"]}
 require(actual.keys()==expected.keys(),"Complete active binding set; no missing or invented slot")
 for key,(image,flags) in expected.items():
  b=actual[key];material=materials[b["material"]]
  require((b["width"],b["height"],b["flags"])==(image["width"],image["height"],flags),f"Original binding metadata {key}")
  require(material["entry"]["source_index"]==image["index"],f"Original source image {key}")
  transparent=int(key[1] in (0,3) and flags&8!=0)
  require(material["color0_transparent"]==transparent,f"Original vertical-only transparency {key}")
 unique={(m["entry"]["id"],m["color0_transparent"]) for m in materials.values()}
 require(len(unique)==len(materials),"No redundant texture/transparent material")
 return dict(bindings=len(actual),materials=len(materials),gpu_texture_bytes=sum(m["entry"]["bytes"] for m in materials.values()),
  asset_bytes=world["asset_bytes"],lifecycle_checks=case["lifecycle_checks"],max_read=case["max_request"],
  world_bytes=world["resident_world_bytes"],workspace_bytes=world["workspace_bytes"],scene_bytes=world["scene_bytes"])

def canonical_geometry(exe,level,native):
 require(sha(exe.read_bytes())==native["comparison"]["executable_sha256"],"Accepted native-compared geometry_dump SHA")
 output=subprocess.check_output([exe,ROOT/f"build/nds/n07/geometry-level{level}.cp866",ROOT/f"build/nds/n07/geometry-bindings{level}.bin"],text=True)
 result={}
 for line in output.splitlines():
  row=list(map(int,line.split()));x,z,index=row[:3]
  result[(z*64+x)*12+index+1]=dict(cell=[x,z],kind=row[3],material=row[6],flags=row[7],vertices=[row[9+i*5:14+i*5] for i in range(4)])
 require(len(result)==native["comparison"]["quads"],"Full canonical source geometry count")
 return result

def verify_scene(case,canonical,camera):
 scene=case["scene"];counts=scene["counts"];materials={m["material"]:m for m in case["world"]["materials"]}
 actual={}
 for cell in scene["cells"]:
  ox,oy,oz=cell["origin"]
  require(ox%512==0 and oy==0 and oz%512==0,"512-unit origin lattice")
  for q in cell["quads"]:
   sid=q["source_surface_id"];require(sid not in actual,"Unique source surface")
   actual[sid]=q
   src=canonical[sid]
   require(materials[q["texture_slot"]+1]["entry"]["source_index"]+1==src["material"],"Source material after packet localization")
   worldvertices=[[v[0]+ox,v[1]+oy,v[2]+oz,*v[3:]] for v in q["vertices"]]
   require(worldvertices==src["vertices"],"All world vertices/UV preserved in packet")
 # Independent rational frustum guard-band policy; no producer imports.
 import math
 x,y,z,heading=camera
 sine=int(math.sin(heading*3.14159265359/180.0)*32768)
 cosine=int(math.cos(heading*3.14159265359/180.0)*32768)
 left,top,right,bottom=counts["bounds"]
 gathered={sid:q for sid,q in canonical.items() if left<=q["cell"][0]<=right and top<=q["cell"][1]<=bottom}
 visible=set()
 for sid,q in gathered.items():
  tests=[]
  for vx,vy,vz,*_ in q["vertices"]:
   lateral=(vx-x)*cosine+(vz-z)*sine
   depth=(vz-z)*cosine-(vx-x)*sine
   eye=depth+200*32768;dy=(vy-y)*32768
   tests.append([depth+32768,400*lateral+324*eye,326*eye-400*lateral,164*eye+400*dy,166*eye-400*dy])
  if not any(all(t[p]<0 for t in tests) for p in range(5)):visible.add(sid)
 require(set(actual)==visible,"Every frustum-retained source quad published exactly once")
 require(counts["source_quads"]==len(gathered) and counts["published_quads"]==len(visible),"Source/frustum counts")
 require(counts["frustum_rejected"]==len(gathered)-len(visible),"Rejected count")
 return dict(camera=camera,source=len(gathered),published=len(visible),cells=len(scene["cells"]))

def main():
 parser=argparse.ArgumentParser();parser.add_argument("--dump",type=Path,default=ROOT/"build/nds/n07/render_dump.exe")
 parser.add_argument("--step",type=int,default=4);args=parser.parse_args()
 require(1<=args.step<=64,"Scan step")
 report=dict(task="N07",passed=False,scan_scope=f"non-wall runtime cell centers on step{args.step} lattice, eight headings; measured candidate ranking, not exhaustive continuous worst case",levels=[],heavy=[])
 write(OUT/"scene-host-report.json",report)
 catalog=entries();native=json.loads((OUT/"geometry-native-report.json").read_text());require(native["passed"],"Native geometry gate")
 fixtures=json.loads((OUT/"native-geometry-aabb.json").read_text())
 for level in range(1,4):
  model=json.loads((ROOT/f"research/evidence/nds/n05/oracle/level{level}-lang1.json").read_text())
  case=json.loads(run(args.dump,level,"--scan",args.step));write(OUT/f"scene-host-level{level}.json",case)
  item=dict(level=level,**check_bindings(case,native["levels"][level-1],model,catalog),scan=case["scan"])
  canonical=canonical_geometry(ROOT/"build/nds/n07/geometry_dump.exe",level,native["levels"][level-1])
  poses=[[[15120,-300,22800,90],[20880,-300,3120,90],[17520,-300,28560,90]][level-1]]
  poses.extend(r["camera"] for r in case["scan"]["top"])
  if level==1: poses.extend([[19440,-300,1680,0],[19440,-300,2160,180]])
  if level==2: poses.extend([[19920,-300,1200,0],[19920,-300,1680,180]])
  item["scene_checks"]=[]
  for i,pose in enumerate(poses):
   frame=json.loads(run(args.dump,level,"--scene",*pose))
   write(OUT/f"scene-host-level{level}-pose{i}.json",frame)
   item["scene_checks"].append(verify_scene(frame,canonical,pose))
  best=case["scan"]["top"][0]["camera"]
  report["heavy"].append(dict(level=level,x=best[0],z=best[2],heading=best[3],published_quads=case["scan"]["top"][0]["published_quads"]))
  report["levels"].append(item)
 text="".join(f"{f['camera'][0]} {f['camera'][2]} {f['camera'][3]}\n" for f in fixtures)
 actual=[list(map(int,line.split())) for line in run(args.dump,1,"--bounds",input=text).splitlines()]
 require(actual==[f["conservative_bounds"] for f in fixtures],"All1444 original-point conservative gather bounds")
 report["aabb_cases"]=len(fixtures)
 report["gpu_texture_capacity_bytes"]=393216
 report["full_level_texture_sets_fit_selected_vram"]=all(i["gpu_texture_bytes"]<=393216 for i in report["levels"])
 report["sources"]={str(p.relative_to(ROOT)).replace("\\","/"):sha(p.read_bytes()) for p in [Path(__file__),
  ROOT/"ports/nds/tests/render_dump.c",ROOT/"ports/nds/render/materials.c",ROOT/"ports/nds/render/scene.c",
  ROOT/"ports/nds/include/dok/render_materials.h",ROOT/"ports/nds/include/dok/render_scene.h",ROOT/"ports/nds/include/dok/render_packet.h",
  ROOT/"build/nds/n07/generated/render_trig.inc",args.dump.resolve()]}
 report["evidence_inputs"]={str(p.relative_to(ROOT)).replace("\\","/"):sha(p.read_bytes()) for p in [OUT/"geometry-native-report.json",OUT/"native-geometry-aabb.json"]}
 report["package_sha256"]=PACKAGE_SHA;report["passed"]=True;write(OUT/"scene-host-report.json",report)
 print(json.dumps(dict(passed=True,heavy=report["heavy"],levels=[dict(level=i["level"],bindings=i["bindings"],gpu_texture_bytes=i["gpu_texture_bytes"],poses=i["scan"]["poses"]) for i in report["levels"]],aabb_cases=len(fixtures)),indent=2))

if __name__=="__main__":main()
