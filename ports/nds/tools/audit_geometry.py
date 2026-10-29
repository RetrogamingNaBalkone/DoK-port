"""N07 isolated original x86 static-constructor oracle, not a DOS session.

Reads preserved Ghidra segment exports; never disassembles or changes EXEs.
Getter, renderer-constructor and allocator boundaries are controlled fixtures.
Original instructions choose all neighbour branches and constructor arguments.
"""
from __future__ import annotations
import hashlib
import argparse
import collections
import json
import math
from pathlib import Path
import struct
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "research/evidence/nds/n07"
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

SOURCES = {
 "originals/pkr/RUS/PKR.EXE": "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28",
 "originals/pkr/RUS/PKR.RES": "af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816",
 "originals/pkr/ENG/PKE.EXE": "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b",
 "originals/pkr/ENG/PKE.RES": "865da340621fa6f90548a84e9ae9bf0a85071befb32a40ca1d359e488f74d458",
}
MODELS = {1:"5a0f254d2969a4c289ac301bc121cb75bd073e9f30403c8740c89959fa0c7df8",
 2:"5602d3461f6b0b6304b14cc4497249deb2b8e6e949f0c3294f126a1092382f3c",
 3:"2b37e532f4ce2b49396b0675ba164f40a4d3a4e15040a72fcabf98ac56087ebb"}

def digest(data):
 return hashlib.sha256(data).hexdigest()

def safe_output(path):
 path=Path(path).absolute()
 roots=[ROOT/"research/evidence/nds/n07",ROOT/"build/nds/n07"]
 if ".." in path.parts: raise ValueError("Parent traversal in N07 output")
 root=next((r for r in roots if path.is_relative_to(r)),None)
 if root is None or path==root: raise ValueError("Output must remain in fixed N07 roots")
 for ancestor in [path,*path.parents]:
  if ancestor==ROOT.parent: break
  if ancestor.is_symlink() or (ancestor.exists() and ancestor.is_junction()):
   raise ValueError("N07 output alias rejected")
 if path.exists() and (not path.is_file() or path.stat().st_nlink!=1):
  raise ValueError("N07 output must be unaliased regular file")
 if path.resolve()!=path: raise ValueError("N07 resolved output differs")
 return path

def source_hashes():
 result = {name: digest((ROOT/name).read_bytes()) for name in SOURCES}
 if result != SOURCES:
  raise ValueError("Unexpected original sample")
 return result

def catalogue():
 data = (ROOT/"originals/pkr/RUS/PKR.RES").read_bytes()
 result = {}
 for i in range(146):
  row = data[2+i*30:32+i*30]
  size, offset = struct.unpack_from("<II",row,16)
  result[row[:16].split(b"\0",1)[0]] = (i, data[offset:offset+size])
 return result

class NativeCellOracle:
 def __init__(self, level):
  model_bytes=(ROOT/f"research/evidence/nds/n05/oracle/level{level}-lang1.json").read_bytes()
  if digest(model_bytes)!=MODELS[level]: raise ValueError("Accepted N05 canonical model changed")
  self.model = json.loads(model_bytes)
  self.base, self.world = self.model["base"], self.model["world"]
  self.resources = catalogue()
  self.source = self.resources[f"LEVEL{level}".encode()][1]
  self.cpu = c = Uc(UC_ARCH_X86,UC_MODE_16)
  c.mem_map(0,0x100000)
  self.segment = ROOT/"research/evidence/flow/ru/1098.bin"
  raw = self.segment.read_bytes()
  if digest(raw) != "44937d68d6e03f426afd77c6e0427af1f7cfcc42e6946d23d0e00618701f91f9":
   raise ValueError("Ghidra world export changed")
  c.mem_write(0x50000,raw)
  self.images, self.image_names = {}, {}
  self.bindings = {}
  c.mem_write(0x81000,struct.pack("<3H",64,1,64))
  c.mem_write(0x81742,struct.pack("<I",64))
  for plane, offset in [(0,0x608a),(1,0x718e),(2,0x8292)]:
   c.mem_write(0x30000+offset,bytes.fromhex(self.world["maps"][plane]))
  for obj in self.base["objects"]:
   glyph = obj["glyph"]
   pointer = 0x90000+glyph*256
   c.mem_write(0x8130e+glyph*4,struct.pack("<HH",glyph*256,0x9000))
   c.mem_write(pointer+0x10,struct.pack("<HH",0xe000, obj["class_id"]))
   c.mem_write(pointer+0x15,struct.pack("<Hhh",obj["flags"],obj["width_parameter"],obj["height_parameter"]))
   for field in obj["fields"]:
    if field[0] == 0x54:
     c.mem_write(pointer+0x54,struct.pack("<h",field[2]))
   bindings = self.bindings[glyph] = {}
   for slot, span in [(8,obj["sprite"])] + [(s, next((f[3:5] for f in obj["fields"] if f[0] == native and f[1] == 3),obj["sprite"]))
       for s,native in [(0x18,0x56),(0x1c,0x5a),(0x20,0x5e)]]:
    if slot == 0x1c and obj["class_id"] == 12:
     span = obj["sprite"]
    name = self.source[span[0]:span[0]+span[1]]
    if name not in self.images:
     off = 0x100+len(self.images)*32
     resource_id,payload = self.resources[name]
     width,height = struct.unpack_from("<HH",payload)
     self.images[name] = (off,width,height,resource_id)
     self.image_names[off] = name
     c.mem_write(0xa0000+off,struct.pack("<HH",width,height))
    bindings[slot] = self.images[name][0]
   c.mem_write(pointer+0x44,struct.pack("<HH",bindings[8],0xa000))
  for slot in [8,0x18,0x1c,0x20]:
   c.mem_write(0x3e000+slot,struct.pack("<HH",slot,0x6000))
  shared={}
  for obj in self.base["objects"]:
   start,size=obj["sprite"]
   shared[self.source[start:start+size]]=obj["sprite_flags"]&65535
  self.binding_flags={}
  for obj in self.base["objects"]:
   glyph=obj["glyph"]
   flags=self.binding_flags[glyph]={}
   for slot,offset in self.bindings[glyph].items():
    name=self.image_names[offset]
    stored=struct.unpack_from("<H",self.resources[name][1],4)[0]
    flags[slot]=(obj["sprite_flags"]&65535) if slot==8 and obj["class_id"]==7 else shared.get(name,stored)
  c.mem_write(0xb0006,b"\x01")
  c.mem_write(0x3aa36,struct.pack("<HHHH",128,0,0x100,0xb000))
  c.hook_add(UC_HOOK_CODE,self.hook)
  self.records = []
  self.mode="cell"

 def ret(self,ax,dx):
  c = self.cpu
  sp = c.reg_read(R.UC_X86_REG_SP)
  ip,cs = struct.unpack("<HH",c.mem_read(0x30000+sp,4))
  for reg,value in [(R.UC_X86_REG_AX,ax),(R.UC_X86_REG_DX,dx),(R.UC_X86_REG_SP,sp+4),
    (R.UC_X86_REG_CS,cs),(R.UC_X86_REG_IP,ip)]:
   c.reg_write(reg,value)

 def hook(self,c,address,size,unused):
  if self.mode=="aabb" and address in (0x532c5,0x533ab):
   bp=c.reg_read(R.UC_X86_REG_BP)
   def get(offset): return struct.unpack("<h",c.mem_read(0x30000+bp+offset,2))[0]
   if address==0x532c5:
    self.points=[[get(a),get(b)] for a,b in [(-0x14,-0x1c),(-0x16,-0x1e),(-0x18,-0x20),(-0x1a,-0x22)]]
   else:
    self.bounds=[get(k) for k in (-0x16,-0x1e,-0x18,-0x20)]
    self.stopped=True
    c.emu_stop()
   return
  if address in (0x54f7b,0x551c1):
   self.stopped = True
   c.emu_stop()
   return
  sp = c.reg_read(R.UC_X86_REG_SP)
  if 0x60000 <= address < 0x60100:
   offset,segment = struct.unpack("<HH",c.mem_read(0x30000+sp+4,4))
   assert segment == 0x9000 and offset%256 == 0
   self.ret(self.bindings[offset//256][address-0x60000],0xa000)
   return
  if address in (0x10b00,0x111aa,0x11b07,0x120f4,0x117cb):
   # receiver, XYZ pointer precede constructor-specific scalar arguments.
   args = bytes(c.mem_read(0x30000+sp+4,28))
   xyzoff,xyzseg = struct.unpack_from("<HH",args,4)
   xyz = list(struct.unpack("<iii",c.mem_read(xyzseg*16+xyzoff,12)))
   if address == 0x111aa:
    height,width,orient,flags,imgoff,imgseg,tag,v0,v1 = struct.unpack_from("<4h5H",args,8)
    kind = "edge"
    details = dict(height=height,width=width,orientation=orient,flags=flags,tag=tag,v0=v0,v1=v1)
   elif address == 0x10b00:
    height,width,orient,flags,imgoff,imgseg,tag,mirror = struct.unpack_from("<4h4H",args,8)
    kind = "center"
    details = dict(height=height,width=width,orientation=orient,flags=flags,tag=tag,mirror=mirror)
   elif address == 0x11b07:
    height,width,flags,imgoff,imgseg,imgoff2,imgseg2,tag = struct.unpack_from("<3h5H",args,8)
    kind = "horizontal2"
    details = dict(height=height,width=width,flags=flags,tag=tag,
      image2=self.image_names[imgoff2].hex())
   elif address == 0x120f4:
    width,flags,imgoff,imgseg,tag = struct.unpack_from("<2h3H",args,8)
    kind = "horizontal1"
    details = dict(width=width,flags=flags,tag=tag)
   else:
    self.ret(0,0xb000)
    return
   assert imgseg == 0xa000
   return_ip, = struct.unpack("<H",c.mem_read(0x30000+sp,2))
   self.records.append(dict(kind=kind,xyz=xyz,image=self.image_names[imgoff].hex(),return_ip=return_ip,**details))
   self.ret(0,0xb000)

 def cell(self,x,z):
  c = self.cpu
  self.mode="cell"
  for reg,value in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_CS,0x5000),
     (R.UC_X86_REG_SP,0xf400),(R.UC_X86_REG_BP,0xf800),(R.UC_X86_REG_DI,x)]:
   c.reg_write(reg,value)
  # Enter after camera/AABB setup with precisely initialized stack locals.
  for offset,fmt,value in [(6,"HH",(0x1000,0x8000)),(-0x12,"i",480),(-0xe,"h",960),
    (-0x56,"i",z*480),(-0x52,"i",x*480),(-0x58,"h",z),(-0x62,"h",z-1),
    (-0x64,"h",z+1),(-0x5e,"h",x-1),(-0x60,"h",x+1)]:
   values = value if isinstance(value,tuple) else (value,)
   c.mem_write(0x3f800+offset,struct.pack("<"+fmt,*values))
  c.mem_write(0x3aa38,bytes(2))
  self.records,self.stopped = [],False
  c.emu_start(0x534de,0x551d0,count=20000)
  if not self.stopped:
   raise ValueError(f"Original cell did not reach static exit: {x},{z}")
  return self.records

 def aabb(self,x,z,heading):
  c=self.cpu
  self.mode="aabb"
  for reg,value in [(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_DS,0x3000),
    (R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:
   c.reg_write(reg,value)
  for angle in range(361):
   radians=angle*3.14159265359/180.0
   c.mem_write(0x11800+angle*4,struct.pack("<i",int(math.sin(radians)*32768)))
   c.mem_write(0x11800+0x5a4+angle*4,struct.pack("<i",int(math.cos(radians)*32768)))
  c.mem_write(0x81738,struct.pack("<hh",480,960))
  c.mem_write(0xb4000,struct.pack("<iiih",x,-300,z,heading))
  c.mem_write(0x3f000,struct.pack("<8H",0xfff0,0x5000,0x1000,0x8000,0x4000,0xb000,0x5000,0xb000))
  self.stopped=False
  c.emu_start(0x530c1,0x533ac,count=3000)
  if not self.stopped: raise ValueError("AABB original-instruction execution failed")
  return dict(camera=[x,-300,z,heading],points=self.points,bounds=self.bounds)

def word(value):
 return (value+32768)%65536-32768

def expected_quads(original,cell):
 result=[]
 x,z=cell["x"],cell["z"]
 definitions={o["glyph"]:o for o in original.base["objects"]}
 maps=[bytes.fromhex(p) for p in original.world["maps"]]
 for rec in cell["records"]:
  if rec["kind"]=="edge":
   kind=4 if rec["return_ip"]<0x3bae else 0
  elif rec["kind"]=="center": kind=1
  else: kind=2
  glyph=maps[1 if kind in (0,1) else 2][z*64+x]
  obj=definitions[glyph]
  def emit(kind,name,vertices,orientation=0):
   rid,payload=original.resources[bytes.fromhex(name)]
   slot={0:8,1:8,2:0x1c,3:0x18,4:0x20}[kind]
   row=[x,z,len(result),kind,orientation,glyph,rid+1,original.binding_flags[glyph][slot],obj["flags"]]
   for vx,vy,vz,u,v in vertices: row.extend([vx,vy,vz,u*65536,v*65536])
   result.append(row)
  rid,payload=original.resources[bytes.fromhex(rec["image"])]
  iw,ih=struct.unpack_from("<HH",payload)
  xx,yy,zz=rec["xyz"]
  w=rec["width"]
  if rec["kind"].startswith("horizontal"):
   # Native constructor arg image1 is ceiling, image2 floor.
   floorname=rec.get("image2",rec["image"])
   _,floorpayload=original.resources[bytes.fromhex(floorname)]
   fw,fh=struct.unpack_from("<HH",floorpayload)
   emit(2,floorname,[(xx,yy,zz+w,0,0),(xx+w,yy,zz+w,fw-1,0),
    (xx+w,yy,zz,fw-1,fh-1),(xx,yy,zz,0,fh-1)])
   if rec["kind"]=="horizontal2":
    y=yy-rec["height"]
    emit(3,rec["image"],[(xx,y,zz,0,0),(xx+w,y,zz,iw-1,0),
     (xx+w,y,zz+w,iw-1,ih-1),(xx,y,zz+w,0,ih-1)])
  else:
   orient=rec["orientation"]
   if rec["kind"]=="center":
    endpoints= ((xx,zz+w//2),(xx+w,zz+w//2)) if orient==0 else ((xx+w//2,zz),(xx+w//2,zz+w))
   else:
    endpoints={0:((xx,zz),(xx+w,zz)),90:((xx,zz+w),(xx,zz)),
     180:((xx+w,zz+w),(xx,zz+w)),270:((xx+w,zz),(xx+w,zz+w))}[orient]
   (ax,az),(bx,bz)=endpoints
   v0=word(rec.get("v0",0));v1=word(rec.get("v1",0)) or ih-1
   u0,u1=(iw-1,0) if rec.get("mirror",0) else (0,iw-1)
   emit(kind,rec["image"],[(ax,yy-rec["height"],az,u0,v0),(bx,yy-rec["height"],bz,u1,v0),
     (bx,yy,bz,u1,v1),(ax,yy,az,u0,v1)],orient)
 return result

def compare(original,cells,level,executable):
 build=ROOT/"build/nds/n07"
 build.mkdir(parents=True,exist_ok=True)
 level_path=safe_output(build/f"geometry-level{level}.cp866")
 binding_path=safe_output(build/f"geometry-bindings{level}.bin")
 level_path.write_bytes(original.source)
 raw=bytearray(8192)
 for glyph,slots in original.bindings.items():
  for index,slot in enumerate((8,0x1c,0x18,0x20)):
   name=original.image_names[slots[slot]]
   _,iw,ih,rid=original.images[name]
   struct.pack_into("<4H",raw,(glyph*4+index)*8,rid+1,iw,ih,original.binding_flags[glyph][slot])
 binding_path.write_bytes(raw)
 expected=[row for cell in cells for row in expected_quads(original,cell)]
 output=subprocess.check_output([executable,level_path,binding_path],text=True)
 actual=[list(map(int,line.split())) for line in output.splitlines()]
 if actual!=expected:
  for i,(a,b) in enumerate(zip(actual,expected)):
   if a!=b: raise AssertionError(f"Native geometry mismatch level{level} row{i}: C{a} native{b}")
  raise AssertionError(f"Native geometry count mismatch: {len(actual)} / {len(expected)}")
 return dict(quads=len(expected),scalar_values=sum(map(len,expected)),
  kinds=dict(collections.Counter(row[3] for row in expected)),
  max_cell_quads=max(len(expected_quads(original,cell)) for cell in cells),
  executable_sha256=digest(Path(executable).read_bytes()))

def synthetic_native():
 original=NativeCellOracle(1)
 c=original.cpu
 positions=[1,64,129,66]  # runtime N/W/S/E neighbours of (1,1).
 glyphs=[32,219,108,97]
 verified=0
 for door in [108,97]:
  for pattern in range(256):
   n=pattern
   selected=[]
   for position in positions:
    glyph=glyphs[n%4];n//=4
    c.mem_write(0x3718e+position,bytes([glyph])); selected.append(glyph)
   c.mem_write(0x3718e+65,bytes([door]))
   for position in [65,*positions]: c.mem_write(0x38292+position,bytes([32]))
   records=[r for r in original.cell(1,1) if r["kind"]=="center"]
   expected=[]
   if selected[0]==32 and selected[2]==32: expected.append((0,int(selected[1]==219)))
   if selected[1]==32 and selected[3]==32: expected.append((90,int(selected[0]==219)))
   if [(r["orientation"],r["mirror"]) for r in records]!=expected:
    raise AssertionError("Original door neighbour synthetic mismatch")
   verified+=1
 c.mem_write(0x3718e+65,b" ")
 c.mem_write(0x90000+32*256+0x19,struct.pack("<h",-32768))
 c.mem_write(0x90000+32*256+0x54,struct.pack("<h",-1))
 records=original.cell(1,1)
 if len(records)!=1 or records[0]["kind"]!="horizontal1" or records[0]["xyz"]!=[480,-32768,480]:
  raise AssertionError("Original single floor/NEG16 branch mismatch")
 return dict(door_neighbour_combinations=verified,single_floor_neg16_wrap=records,
  scope="controlled synthetic map/object memory in isolated original builder, not an original DOS level session")

def main():
 parser=argparse.ArgumentParser()
 parser.add_argument("--dump",type=Path,help="strict C11 geometry_dump executable to compare")
 parser.add_argument("--reuse-native",action="store_true",help="read archived records instead of rerunning x86")
 args=parser.parse_args()
 before = source_hashes()
 # Validate every output before publishing any generated fixture/report.
 for level in range(1,4):
  for p in [OUT/f"native-geometry-level{level}.json",ROOT/f"build/nds/n07/geometry-level{level}.cp866",
    ROOT/f"build/nds/n07/geometry-bindings{level}.bin"]:
   safe_output(p)
 for name in ["geometry-native-report.json","native-geometry-aabb.json"]: safe_output(OUT/name)
 OUT.mkdir(parents=True,exist_ok=True)
 report = dict(task="N07",passed=False,method="isolated original x86 static builder branches",
  scope="original per-cell branch/constructor arguments; controlled far getters; canonical XYZ/UV expanded from reviewed constructor ASM; no DOS raster or DPMI session",
  originals_before=before,levels=[])
 (OUT/"geometry-native-report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
 for level in range(1,4):
  original = NativeCellOracle(level)
  path = OUT/f"native-geometry-level{level}.json"
  if args.reuse_native:
   cells=json.loads(path.read_text())
  else:
   cells = [dict(x=x,z=z,records=original.cell(x,z)) for z in range(64) for x in range(64)]
   path.write_text(json.dumps(cells,separators=(",",":"))+"\n",encoding="utf8")
  item=dict(level=level,cells=len(cells),records=sum(len(c["records"]) for c in cells),
   file=str(path.relative_to(ROOT)),sha256=digest(path.read_bytes()),n05_model_sha256=MODELS[level],
   image_flags_rule="LEVEL1048:07B2 assigns shared cached flags; class7 owns constructor-time image-header copy",
   initial_bindings=[dict(glyph=g,slots=[dict(slot=s,image=original.image_names[p].hex(),flags=original.binding_flags[g][s])
    for s,p in b.items()]) for g,b in original.bindings.items()])
  if args.dump:
   item["comparison"]=compare(original,cells,level,args.dump.resolve())
  report["levels"].append(item)
 original=NativeCellOracle(1)
 fixtures=[original.aabb(x,z,a) for x,z in [(15120,22800),(1,1),(30719,30719),(0,0)]
  for a in range(361)]
 for f in fixtures:
  # Declarative formula from the instruction listing, checked against x86.
  x,_,z,a=f["camera"]
  trig=lambda angle,cosine: int((math.cos if cosine else math.sin)(angle*3.14159265359/180.0)*32768)
  s,c,r,t=trig(a,False),trig(a,True),trig(360-a,True),trig(360-a,False)
  bx=x+((960*s)>>15);bz=z-((960*c)>>15)
  trunc=lambda value: abs(value)//480*(-1 if value<0 else 1)
  points=[[trunc(xx),trunc(zz)] for xx,zz in [(bx+((480*r)>>15),bz+((-480*t)>>15)),
   (bx+((-480*r)>>15),bz+((480*t)>>15)),
   (bx+((2880*r+4320*t)>>15),bz+((4320*r-2880*t)>>15)),
   (bx+((-2880*r+4320*t)>>15),bz+((4320*r+2880*t)>>15))]]
  bounds=[max(0,min(p[0] for p in points)),max(0,min(p[1] for p in points)),
   min(63,max(p[0] for i,p in enumerate(points) if i!=1)),
   min(63,max(p[1] for i,p in enumerate(points) if i!=1))]
  if points!=f["points"] or bounds!=f["bounds"]: raise AssertionError(f"AABB mismatch: {f}, {points}, {bounds}")
  f["conservative_bounds"]=[bounds[0],bounds[1],min(63,max(p[0] for p in points)),min(63,max(p[1] for p in points))]
 path=OUT/"native-geometry-aabb.json"
 path.write_text(json.dumps(fixtures,separators=(",",":"))+"\n",encoding="utf8")
 report["aabb"]=dict(cases=len(fixtures),file=str(path.relative_to(ROOT)),sha256=digest(path.read_bytes()),
  scope="all361 headings, four map-boundary poses; original low32/SAR/IDIV instructions, no DOS raster")
 report["synthetic_original_branches"]=synthetic_native()
 report["originals_after"] = source_hashes()
 report["sources"]={str(p.relative_to(ROOT)).replace("\\","/"):digest(p.read_bytes()) for p in [Path(__file__),
  ROOT/"ports/nds/render/geometry.c",ROOT/"ports/nds/include/dok/geometry.h",
  ROOT/"ports/nds/tests/geometry_dump.c",ROOT/"ports/nds/tests/test_geometry.c",
  ROOT/"research/evidence/flow/ru/1098.bin",ROOT/"research/evidence/flow/ru/analysis-manifest.json",
  ROOT/"research/evidence/surfaces/ru/evidence.asm",ROOT/"research/evidence/surfaces/ru/data.bin",
  ROOT/"research/evidence/stage5/ru/evidence.asm",ROOT/"research/evidence/interactions/ru/evidence.asm"]}
 report["passed"] = report["originals_after"] == before
 (OUT/"geometry-native-report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
 print(json.dumps(dict(passed=report["passed"],report=str((OUT/"geometry-native-report.json").relative_to(ROOT)),
  levels=[dict(level=i["level"],cells=i["cells"],comparison=i.get("comparison")) for i in report["levels"]],
  aabb_cases=report["aabb"]["cases"],synthetic_door_cases=report["synthetic_original_branches"]["door_neighbour_combinations"]),indent=2))

if __name__ == "__main__":
 main()
