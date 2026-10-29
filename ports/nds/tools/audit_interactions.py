"""N11 PKR/PKE original instruction oracle; not a complete DOS session.

Fresh read-only Ghidra exports supply all code. Only queue bookkeeping,
sound/messages and ABI runtime copy services are intercepted for door vectors.
Native actor, door, sensor and crop instructions produce expected results.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "research/evidence/nds/n11"
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

SAMPLES = {
    "ru": ("originals/pkr/RUS/PKR.EXE", "12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28"),
    "en": ("originals/pkr/ENG/PKE.EXE", "cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b"),
}
LAYOUT = {
    "ru": dict(actor="1068", world="1098", image="10f8", center="10b0", data="1190", entries=(0x347e,0x3262,0x334e,0x3427,0x521), sensor=0x91e,
               scheduler=0x10380, schedule=0x108, cancel=0x214, interval=0x265, worldptr=0x266a,
               services=(0x11277,0x112b2,0x112ed,0x11339,0x11e78), crop=0x5c9, delta=0, focal=0x934, helper=0x50),
    "en": dict(actor="1020", world="1030", image="1060", center="1040", data="10d0", entries=(0x3b33,0x3917,0x3a03,0x3adc,0xa01), sensor=0xe1c,
               scheduler=0x10100, schedule=0x14be, cancel=0x15ca, interval=0x161b, worldptr=0x271a,
               services=(0x11d3d,0x11d78,0x11db3,0x11dff,0x11adb), crop=0x6d9, delta=0xb0, focal=0xa44, helper=0x56),
}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=sha(path.read_bytes()))


def sources():
    result = {lang: sha((ROOT / path).read_bytes()) for lang, (path, _) in SAMPLES.items()}
    assert all(result[lang] == expected for lang, (_, expected) in SAMPLES.items()), "EXE sample SHA differs"
    return result


def evidence(language):
    folder = OUT / "original" / language
    manifest_path = folder / "analysis-manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    assert manifest["read_only"] is True
    assert manifest["sha256_before"] == manifest["sha256_after"] == SAMPLES[language][1]
    for role, path in (("script", ROOT / "tools/ghidra/ExportNdsInteractions.java"), ("driver", ROOT / "ports/nds/tools/export_interaction_evidence.ps1")):
        assert manifest[role+"_sha256"] == sha(path.read_bytes()), role
    for segment, expected in manifest["binary_sha256"].items():
        assert sha((folder / (segment+".bin")).read_bytes()) == expected, segment
    return [pin(p) for p in sorted(folder.iterdir()) if p.is_file()]


def cpu_for(language, role):
    p = LAYOUT[language]
    cpu = Uc(UC_ARCH_X86, UC_MODE_16)
    cpu.mem_map(0, 0x100000)
    cpu.mem_write(0x50000, (OUT / "original" / language / (p[role]+".bin")).read_bytes())
    return cpu


class OriginalDoor:
    """Execute one source callback; collect requested queue operations exactly."""
    def __init__(self, language):
        self.language, self.p = language, LAYOUT[language]
        self.cpu = cpu_for(language, "actor")
        self.cpu.mem_write(0x30000, (OUT / "original" / language / (self.p["data"]+".bin")).read_bytes())
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)

    def reg(self, name, value=None):
        r = getattr(R, "UC_X86_REG_"+name)
        if value is None:
            return self.cpu.reg_read(r)
        self.cpu.reg_write(r, value)

    def ret(self):
        sp = self.reg("SP")
        ip, cs = struct.unpack("<HH", self.cpu.mem_read(0x30000+sp,4))
        self.reg("SP",sp+4); self.reg("CS",cs); self.reg("IP",ip)

    def hook(self,c,address,size,context):
        if 0x50000 <= address < 0x56000:
            return
        sp = self.reg("SP")
        if address == 0x15cd5:
            ip,cs=struct.unpack("<HH",c.mem_read(0x30000+sp,4))
            ptr=self.reg("DX")*16+self.reg("AX")
            c.mem_write(0x30000+sp-4,bytes(c.mem_read(ptr,8)))
            self.reg("SP",sp-4);self.reg("CS",cs);self.reg("IP",ip)
            return
        p=self.p
        if address == p["scheduler"]+p["schedule"]:
            cb,segment,param,delay=struct.unpack("<HHII",c.mem_read(0x30000+sp+8,12))
            assert segment==int(p["actor"],16)
            assert param==0, (hex(param),hex(cb))
            self.queue.append([p["entries"].index(cb),delay])
        elif address == p["scheduler"]+p["cancel"]:
            self.keep=False
        elif address == p["scheduler"]+p["interval"]:
            assert self.queue
            self.queue[0][1]=struct.unpack("<I",c.mem_read(0x30000+sp+8,4))[0]
        elif address in p["services"]:
            self.reg("AX",0)
        else:
            raise AssertionError(("unexpected source call",hex(address),hex(sp)))
        self.ret()

    def run(self, vector):
        # op flags frame state ready occupancy extent lock mode delay interval
        # event keys source_class fps marker original_flags
        op,flags,frame,state,ready,occupancy,extent,lock,mode,delay,interval,event,keys,source,fps,marker,originalflags=vector
        obj=bytearray(0x100)
        struct.pack_into("<HH",obj,0x10,0x900,7)
        struct.pack_into("<H",obj,0x15,flags)
        obj[0x1b]=marker
        struct.pack_into("<hh",obj,0x50,frame,interval)
        struct.pack_into("<BBhHHHh",obj,0x54,state,ready,occupancy,0,0,lock,mode)
        struct.pack_into("<h",obj,0x60,extent)
        struct.pack_into("<IH",obj,0x66,delay & 0xffffffff,originalflags)
        self.cpu.mem_write(0x40000,bytes(obj))
        src=bytearray(0x100)
        struct.pack_into("<H",src,0x12,source)
        struct.pack_into("<H",src,0x72,keys)
        sensor=op==5
        if sensor:
            src[0x66]=ord("h")
            struct.pack_into("<I",src,0x67,event)
            self.cpu.mem_write(0x30000+self.p["worldptr"],struct.pack("<HH",0x1000,0x4000))
            self.cpu.mem_write(0x41000+0x30e+ord("h")*4,struct.pack("<HH",0,0x4000))
            self.cpu.mem_write(0x30914,struct.pack("<HH",self.p["entries"][0],0x5000))
        self.cpu.mem_write(0x40200,bytes(src))
        self.cpu.mem_write(0x40400,struct.pack("<I",event))
        self.cpu.mem_write(0x300c2,struct.pack("<I",fps & 0xffffffff))
        self.queue=[] if op in (0,4,5) else [[op,0]]
        self.keep=True
        for name,value in (("SS",0x3000),("DS",0x3000),("CS",0x5000),("SP",0xf000),("BP",0xf800)):
            self.reg(name,value)
        self.cpu.mem_write(0x3f000,struct.pack("<8H",0xfff0,0x5000,0x200 if sensor else 0,0x4000,0x400,0x4000,0x200,0x4000))
        entry=self.p["sensor"] if sensor else self.p["entries"][op]
        self.cpu.emu_start(0x50000+entry,0x5fff0,count=20000)
        assert self.reg("SP")==0xf004
        if not self.keep:
            self.queue.pop(0)
        result=bytes(self.cpu.mem_read(0x40000,0x100))
        fields=[struct.unpack_from("<H",result,0x15)[0],struct.unpack_from("<h",result,0x50)[0],result[0x54],result[0x55],struct.unpack_from("<h",result,0x56)[0],struct.unpack_from("<h",result,0x60)[0],struct.unpack_from("<h",result,0x52)[0],struct.unpack("<I",self.cpu.mem_read(0x40400,4))[0],result[0x1b],len(self.queue)]
        for i in range(4):
            fields += self.queue[i] if i<len(self.queue) else [0,0]
        return fields


def vectors():
    rng=random.Random(110011)
    result=[]
    for op in range(6):
        for _ in range(1200):
            fps=rng.choice([4,8,15,30,59,60,120,32767,32768,65535,65536,0x7fffffff,0x80000004])
            extent=rng.choice([1,8,15,30,59,60,120])
            frame=rng.choice([0,1,extent//4,extent//4+1,extent-2,extent-1,extent])
            result.append([op,rng.choice([0,8,16,64,72,80,88,0xffff]),frame,rng.choice([1,2]),rng.choice([0,1,2]),rng.choice([-32768,-1,0,1,2,32767]),extent,rng.choice([0,1,2,3,4,8,0xffff]),rng.choice([0,1,2]),rng.choice([0,100,40000,0xffffffff]),rng.choice([-32768,-1,0,10,400,32767]),rng.choice([0,0x800,0x40000,0x40800,0xffffffff]),rng.randrange(65536),13 if op==5 else rng.choice([1,9,13]),fps,rng.choice([0,1,0x80,0xff]),rng.choice([0,8,16,64,72,80,88,0xffff])])
    return result


def crop_vectors(language):
    cpu=cpu_for(language,"image")
    records=[]
    for width,height in ((96,110),(96,114),(96,128),(96,192),(97,129)):
        pixels=bytes((i*37+i//width*13)%255+1 for i in range(width*height))
        cpu.mem_write(0x60000,pixels)
        for extent in (8,15,30,59,60):
            for flags in (0x48,0x108):
                for frame in range(extent):
                    header=struct.pack("<5HIHH",width,height,flags,extent,frame,width*height,0,0x6000)
                    cpu.mem_write(0x40000,header)
                    cpu.mem_write(0x3f000,struct.pack("<4H",0xfff0,0x5000,0,0x4000))
                    for name,value in (("SS",0x3000),("CS",0x5000),("SP",0xf000),("BP",0xf800)):
                        cpu.reg_write(getattr(R,"UC_X86_REG_"+name),value)
                    cpu.emu_start(0x50000+LAYOUT[language]["crop"],0x5fff0,count=100000)
                    pointer=cpu.reg_read(R.UC_X86_REG_DX)*16+cpu.reg_read(R.UC_X86_REG_AX)
                    got=bytes(cpu.mem_read(pointer,width*height))
                    shift=(width//extent if flags==0x48 else height//extent)*frame
                    expected=(b"".join(bytes(shift)+pixels[y*width:y*width+width-shift] for y in range(height)) if flags==0x48 else bytes(shift*width)+pixels[:(height-shift)*width])
                    assert got==expected,(language,width,height,flags,extent,frame)
                    assert bytes(cpu.mem_read(0x40000,18))==header
                    records.append(dict(width=width,height=height,flags=flags,extent=extent,phase=frame,shift=shift,pixels_sha256=sha(got)))
    return records


def mirror_vectors(language):
    cpu=cpu_for(language,"center")
    p=LAYOUT[language]
    def hook(c,address,size,context):
        if address==0x50000+p["helper"]:
            c.reg_write(R.UC_X86_REG_IP,p["helper"]+5)
    cpu.hook_add(UC_HOOK_CODE,hook)
    def put(off,fmt,*values):
        cpu.mem_write(0x30000+off,struct.pack(fmt,*values))
    d=p["delta"]
    put(0xaa16+d,"<i",200);put(0xaa1e+d,"<hh",159,79)
    put(0xaa22+d,"<4i",0,0,319,159);put(0xaa32+d,"<h",8);put(p["focal"],"<h",4096)
    records=[]
    for theta in (0,90):
        for heading,sine,cosine in ((0,0,32768),(90,32768,0),(180,0,-32768),(270,-32768,0)):
            put(0xaa3e+d,"<5i",sine,cosine,sine*2000//32768,-300,-cosine*2000//32768)
            pair=[]
            for reflected in (0,1):
                cpu.mem_write(0x40100,bytes(0x80));cpu.mem_write(0x40200,struct.pack("<iii",0,0,0));cpu.mem_write(0x40400,struct.pack("<HH",96,128))
                cpu.mem_write(0x3f000,struct.pack("<14H",0xfff0,0x5000,0x100,0x4000,0x200,0x4000,640,480,theta,0,0x400,0x4000,123,reflected))
                for name,value in (("DS",0x3000),("SS",0x3000),("CS",0x5000),("SP",0xf000),("BP",0xf800)):
                    cpu.reg_write(getattr(R,"UC_X86_REG_"+name),value)
                cpu.emu_start(0x50000,0x5fff0,count=100000)
                assert cpu.reg_read(R.UC_X86_REG_SP)==0xf004
                record=bytes(cpu.mem_read(0x40100,0x4f));assert record[6]&1
                pair.append(struct.unpack_from("<12i",record,0x13))
            a,b=pair
            assert b[:2]==a[3:5] and b[3:5]==a[:2] and b[6:8]==a[9:11] and b[9:11]==a[6:8]
            assert b[2::3]==a[2::3]
            records.append(dict(theta=theta,heading=heading,unreflected=a,reflected=b))
    return records


def cadence_vectors(language):
    p=LAYOUT[language]
    cpu=Uc(UC_ARCH_X86,UC_MODE_16);cpu.mem_map(0,0x100000)
    cpu.mem_write(0x50000,(OUT / "original" / language / "1008.bin").read_bytes())
    start,end,offset=(0xfd1,0xffc,0x1f9a) if language=="ru" else (0x14ae,0x14d9,0x204a)
    rows=[]
    values=(0,1,4,8,30,60,65535,65536,0x7fffffff,0x80000000,0xfffffff0,0xffffffff)
    for old in values:
        for ticks in (1,2,3,4,166,167,333,334,416,10000,65535,65536,0x80000000,0xffffffff):
            cpu.mem_write(0x300c2,struct.pack("<I",old));cpu.mem_write(0x30000+offset,struct.pack("<I",ticks))
            cpu.reg_write(R.UC_X86_REG_DS,0x3000);cpu.reg_write(R.UC_X86_REG_CS,0x5000)
            cpu.emu_start(0x50000+start,0x50000+end,count=100)
            got=struct.unpack("<I",cpu.mem_read(0x300c2,4))[0]
            rows.append(dict(old=old,ticks=ticks,result=got))
    return rows


def world_vectors(language, dump=None):
    # Reuse accepted N10 instrumentation only, never its C geometry or GBA.
    # New methods execute fresh N11 code, byte-equal to accepted N10 code where
    # shared. Unchanged lookup/resolver instrumentation is already proven N10.
    import audit_collision as base
    class OriginalWorld(base.Original):
        def __init__(self,lang,f):
            super().__init__(lang,f)
            for role,address in (("actor",0x50000),("world",0x60000)):
                fresh=OUT / "original" / lang / (self.p[role]+".bin")
                raw=fresh.read_bytes()
                assert raw==(base.OUT / "original" / lang / fresh.name).read_bytes()
                self.cpu.mem_write(address,raw)
            self.target=None;self.floor_mode=False
            self.model=json.loads((ROOT / f"research/evidence/runtime/{lang}/LEVEL{f['level']}-runtime.json").read_text()) if "level" in f else None
        def far_return(self,ax=0):
            c=self.cpu;sp=self.reg("SP");ip,cs=struct.unpack("<HH",c.mem_read(0x30000+sp,4))
            c.reg_write(R.UC_X86_REG_AX,ax);c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
        def hook(self,c,address,size,unused):
            if self.floor_mode and address==0x50000+(0x1cc2 if self.language=="ru" else 0x2276):
                sp=self.reg("SP");category,value=struct.unpack("<hh",c.mem_read(0x30000+sp+8,4))
                self.stat_calls.append([category,value]);return
            if self.floor_mode and address==0x50000+self.p["motion"]:
                self.far_return();return
            if self.floor_mode and address==0x5ffd0:
                self.floor_virtuals+=1;self.far_return();return
            if address==int(self.p["world"],16)*16+self.p["resolve"]:
                c.reg_write(R.UC_X86_REG_CS,0x6000);c.reg_write(R.UC_X86_REG_IP,self.p["resolve"])
            else:
                super().hook(c,address,size,unused)
        def projection(self,x,z,heading):
            point=bytearray(28);struct.pack_into("<iii",point,0,x,-300,z)
            struct.pack_into("<h",point,12,heading);struct.pack_into("<i",point,22,-self.f["side"])
            self.cpu.mem_write(0x40200,bytes(point))
            self.call(0x309 if self.language=="ru" else 0x3ed,struct.pack("<5H",0,0x9000,0x200,0x4000,1))
            result=bytes(self.cpu.mem_read(0x40200,28));px,_,pz=struct.unpack_from("<iii",result)
            pointer=(self.reg("DX")<<4)+self.reg("AX")
            glyph=(pointer-0x81000)//0x80
            assert 0<=glyph<256 and pointer==0x81000+glyph*0x80
            return dict(x=px,z=pz,target=glyph)
        def floor(self,x,z,c2,previous,score=0):
            actor=bytearray(0x100);struct.pack_into("<iii",actor,0x26,x,-300,z);actor[0x5c]=self.f["glyph"]
            if previous is not None:struct.pack_into("<HH",actor,0xda,0x1000+previous*0x80,0x8000)
            struct.pack_into("<h",actor,0x5e,100);struct.pack_into("<h",actor,0xd8,score)
            self.cpu.mem_write(0x40100,bytes(actor));self.cpu.mem_write(0x300c2,struct.pack("<I",c2))
            self.cpu.mem_write(0x30928,struct.pack("<HH",0xffd0,0x5000))
            cell=(z//480)*64+x//480
            self.cpu.mem_write(0x30000+self.p["mmap"]+cell,bytes([self.f["glyph"]]))
            for obj in self.model["objects"]:
                if obj["class"] not in (12,13):continue
                raw=bytearray.fromhex(obj["bytes"])[:0x80];struct.pack_into("<H",raw,0x10,0x900)
                self.cpu.mem_write(0x81000+obj["glyph"]*0x80,bytes(raw))
            self.floor_mode=True;self.stat_calls=[];self.floor_virtuals=0
            self.call(0x2127 if self.language=="ru" else 0x26d7,struct.pack("<HH",0x100,0x4000),0x5000)
            self.floor_mode=False
            flags=struct.unpack("<I",self.cpu.mem_read(0x4011c,4))[0]
            ptr,seg=struct.unpack("<HH",self.cpu.mem_read(0x401da,4));assert seg==0x8000
            x,y,z=struct.unpack("<iii",self.cpu.mem_read(0x40126,12));assert y==-300
            mmap=bytes(self.cpu.mem_read(0x30000+self.p["mmap"],4096))
            counters=[struct.unpack("<H",self.cpu.mem_read(0x81000+g*0x80+0x56,2))[0] for g in range(256)]
            health=struct.unpack("<h",self.cpu.mem_read(0x4015e,2))[0]
            return dict(x=x,z=z,flags=flags,stat_calls=self.stat_calls,virtual_calls=self.floor_virtuals,previous_floor=(ptr-0x1000)//0x80,finish=health==-1,mmap_sha256=sha(mmap),door_counters=counters)
        def absolute(self,x,z,nx,nz):
            actor=bytearray(0x100);struct.pack_into("<iii",actor,0x26,x,-300,z)
            actor[0x5c]=self.f["glyph"]
            self.cpu.mem_write(0x40100,bytes(actor))
            self.call(0x10a4 if self.language=="ru" else 0x15ae,struct.pack("<HHii",0x100,0x4000,nx,nz),0x5000)
            ax,ay,az=struct.unpack("<iii",self.cpu.mem_read(0x40126,12));assert ay==-300
            mmap=bytes(self.cpu.mem_read(0x30000+self.p["mmap"],4096))
            counters=[struct.unpack("<H",self.cpu.mem_read(0x81000+g*0x80+0x56,2))[0] for g in range(256)]
            return dict(x=ax,z=az,mmap_sha256=sha(mmap),door_counters=counters)
    records=[]
    rng=random.Random(110012)
    for level in (1,2,3):
        f=base.fixture(language,level);f["level"]=level;oracle=OriginalWorld(language,f)
        projection=[]
        # Every valid table direction, including360, both normal and out-of-map
        # poses. Native lookup clamps after signed quotient WORD narrowing.
        for heading in range(361):
            x,z=(f["pose"]["x"],f["pose"]["z"]) if heading%3 else (rng.randint(-500,31200),rng.randint(-500,31200))
            projection.append(dict(input=[x,z,heading],result=oracle.projection(x,z,heading)))
        walk=[]
        # Explicit direct absolute move at all open native cells in a shuffled
        # bounded subset. Fresh maps isolate occupancy and native overwrite.
        positions=[]
        for row in range(64):
            for col in range(64):
                x,z=col*480+240,row*480+240
                if oracle.query((0,x,z,0,0))[0]:positions.append((x,z))
        rng.shuffle(positions)
        for x,z in positions[:128]:
            oracle.reset_maps()
            for glyph in range(256):oracle.counter(glyph,0)
            cell=(z//480)*64+x//480
            oracle.cpu.mem_write(0x30000+oracle.p["mmap"]+cell,bytes([f["glyph"]]))
            nx=x+rng.choice([-350,-46,-1,0,1,46,350]);nz=z+rng.choice([-350,-46,-1,0,1,46,350])
            walk.append(dict(input=[x,z,nx,nz],result=oracle.absolute(x,z,nx,nz)))
        floors=[]
        for cell,glyph in enumerate(f["maps"][2]):
            if f["classes"][glyph] not in (12,13):continue
            for c2 in (4,30,60):
                oracle.reset_maps()
                x,z=cell%64*480+240,cell//64*480+240
                oracle.cpu.mem_write(0x30000+oracle.p["mmap"]+cell,b" ")
                admissible=bool(oracle.query((0,x,z,0,0))[0])
                for g in range(256):oracle.counter(g,0)
                result=oracle.floor(x,z,c2,None)
                admissible=admissible or f["classes"][glyph]==13 or result["stat_calls"][0][0] not in (100,101,102)
                assert result["previous_floor"]==glyph
                if f["classes"][glyph]==12:
                    assert result["flags"]==0x4000000 and len(result["stat_calls"])==1 and result["virtual_calls"]==1
                else:
                    assert not result["stat_calls"] and result["virtual_calls"]==1
                floors.append(dict(cell=cell,c2=c2,previous=None,score=0,admissible=admissible,result=result))
                if f["classes"][glyph]==12 and result["stat_calls"][0][0]==1000:
                    oracle.reset_maps()
                    for g in range(256):oracle.counter(g,0)
                    failed=oracle.floor(x,z,c2,None,1)
                    assert not failed["finish"]
                    floors.append(dict(cell=cell,c2=c2,previous=None,score=1,admissible=admissible,result=failed))
                if f["classes"][glyph]==13:
                    result=oracle.floor(x,z,c2,glyph)
                    assert result["virtual_calls"]==0
                    floors.append(dict(cell=cell,c2=c2,previous=glyph,score=0,admissible=admissible,result=result))
        comparisons={}
        if dump:
            import audit_collision as base
            floorcases=floors
            allcases=[(7,*row["input"],0) for row in projection]+[(8,*row["input"]) for row in walk]+[(9+row["score"],row["cell"]%64*480+240,row["cell"]//64*480+240,row["c2"],-1 if row["previous"] is None else row["previous"]) for row in floorcases]
            fieldwire=bytearray(256*16)
            for obj in oracle.model["objects"]:
                fields={v["offset"]:v.get("number",0) for v in obj["fields"]}
                if obj["class"]==12:struct.pack_into("<4i",fieldwire,obj["glyph"]*16,fields.get(0x66,0),fields.get(0x68,0),0,0)
                elif obj["class"]==13:struct.pack_into("<4i",fieldwire,obj["glyph"]*16,0,0,fields.get(0x66,0),fields.get(0x67,0))
            wire=base.fixture_wire(f)+fieldwire+b"".join(struct.pack("<5i",*row) for row in allcases)
            raw=subprocess.check_output([str(dump)],input=wire)
            stride=16+4096+512+16
            assert len(raw)==stride*len(allcases),(len(raw),stride,len(allcases))
            for ordinal,row in enumerate(projection):
                status,x,z,target=struct.unpack_from("<IiiI",raw,ordinal*stride)
                assert status==0 and target==row["result"]["target"],("Use target",language,level,row,status,target)
                assert (x,z)==tuple(row["input"][:2])
            exact,guarded=0,[]
            for ordinal,row in enumerate(walk,len(projection)):
                start=ordinal*stride
                status,x,z,target=struct.unpack_from("<IiiI",raw,start)
                mmap=raw[start+16:start+16+4096]
                counters=list(struct.unpack_from("<256H",raw,start+16+4096))
                want=row["result"]
                if status==0:
                    assert (x,z,sha(mmap),counters)==(want["x"],want["z"],want["mmap_sha256"],want["door_counters"]),("direct wrapper",language,level,row,[x,z,sha(mmap)])
                    exact+=1
                else:
                    ox,oz,nx,nz=row["input"]
                    assert abs(nx-ox)*2>=480 or abs(nz-oz)*2>=480,("unexpected source-domain guard",status,row)
                    assert (x,z)==(ox,oz) and all(v==0 for v in counters)
                    beforemap=f["maps"][0].copy();beforemap[(oz//480)*64+ox//480]=f["glyph"]
                    assert mmap==bytes(beforemap),"Guard wrote occupancy"
                    guarded.append(dict(input=row["input"],status=status,reason="N10 half-cell admission",source_final=[want["x"],want["z"]]))
            floor_guards=[];floor_exact=0
            for ordinal,row in enumerate(floorcases,len(projection)+len(walk)):
                start=ordinal*stride
                status,x,z,previous=struct.unpack_from("<IiiI",raw,start)
                mmap=raw[start+16:start+16+4096]
                counters=list(struct.unpack_from("<256H",raw,start+16+4096))
                flags,finish,queued,reason=struct.unpack_from("<4I",raw,start+16+4096+512)
                want=row["result"]
                if not row["admissible"]:
                    assert status==0 and reason==4 and (x,z)==(row["cell"]%64*480+240,row["cell"]//64*480+240),(language,level,row,[status,x,z,reason])
                    assert flags==0x4000000 and not finish and not queued and previous==want["previous_floor"]
                    assert all(n==0 for n in counters)
                    initial=f["maps"][0].copy();initial[row["cell"]]=f["glyph"]
                    assert mmap==bytes(initial),"Refused old blocked pose changed occupancy"
                    floor_guards.append(dict(cell=row["cell"],c2=row["c2"],status=status,reason_mask=reason,reason="Controlled hero pose starts inside closed TMap door; native resolver may return still-blocked pose, N10 final-footprint guard preserves old pose"))
                    continue
                assert status==0,("floor status",language,level,row,status)
                floor_exact+=1
                assert (x,z,flags,previous,bool(finish),sha(mmap),counters)==(want["x"],want["z"],want["flags"],want["previous_floor"],want["finish"],want["mmap_sha256"],want["door_counters"]),("floor native state",language,level,row,[x,z,flags,previous,finish,sha(mmap)])
                if f["classes"][want["previous_floor"]]==13:
                    assert queued==(1 if want["virtual_calls"] else 0),("sensor entry scheduling",row,queued)
            comparisons=dict(projection_exact=len(projection),absolute_exact=exact,absolute_guards=guarded,floor_exact=floor_exact,floor_guards=floor_guards)
        # Exact maps/counters were compared above. Keep compact distinct
        # counters in permanent JSON rather than thousands of repeated zeros.
        for record in [*walk,*floors]:
            counts=record["result"].pop("door_counters")
            record["result"]["door_counters_nonzero"]=[[g,n] for g,n in enumerate(counts) if n]
        records.append(dict(level=level,pins=oracle.pins,projection=projection,absolute=walk,floor=floors,c_comparison=comparisons))
    return records


def renderer_vectors(dump):
    from fractions import Fraction
    def nearest(value):
        negative=value<0
        value=abs(value)
        integer=(2*value.numerator+value.denominator)//(2*value.denominator)
        return -integer if negative else integer
    result=[]
    package=ROOT / "build/nds/assets/dok-assets.ndspak"
    for level in (1,2,3):
        report=json.loads(subprocess.check_output([str(dump),str(package),str(level),"--geometry"]))
        assert report["passed"]
        starts={}
        phases=0;hidden=0;mirrored=0;dimensions=set()
        for row in report["vectors"]:
            key=(*row["cell"],row["ordinal"])
            w,h=row["width"],row["height"];phase,extent=row["phase"],row["extent"]
            dimensions.add((w,h))
            if phase==0:
                starts[key]=row["vertices"]
                if row["vertices"][0][3]>(w-1)*32768:mirrored+=1
            assert key in starts
            base=starts[key]
            if phase==extent-1:
                assert row["hidden"]
                hidden+=1;continue
            assert not row["hidden"]
            axis=3 if row["flags"]&0xffc0==0x40 else 4
            assert row["flags"]&0xffc0 in (0x40,0x100)
            dimension=w if axis==3 else h
            shift=(dimension//extent)*phase
            wanted=[]
            for i,vertex in enumerate(base):
                v=list(vertex)
                if v[axis]<(shift<<16):
                    other=base[(1,0,3,2)[i] if axis==3 else (3,2,1,0)[i]]
                    ratio=Fraction((shift<<16)-v[axis],other[axis]-v[axis])
                    v=[a+nearest(Fraction(b-a)*ratio) for a,b in zip(v,other,strict=True)]
                v[axis]-=shift<<16
                wanted.append(v)
            assert row["vertices"]==wanted,("crop geometry",level,row,wanted)
            # All final samples refer to source pixels in the retained suffix;
            # the x86 image oracle separately executes exact zero-filled image.
            assert all(v[axis]>=0 for v in wanted)
            phases+=1
        assert hidden==len(starts)
        result.append(dict(level=level,panels=len(starts),mirrored_panels=mirrored,intermediate_cases=phases,fully_open_hidden=hidden,dimensions=sorted(dimensions)))
    return dict(passed=True,dump=pin(dump.resolve()),package=pin(package),levels=result,rounding="Port XYZ/UV intersection rounds nearest, delta ties away from zero; source integer pixel shift independently executed",native_fully_open_difference="All phase extent-1 panels hidden, while native residual source pixels remain")


def verify_live_interaction(row, original=None):
    """Read-only ABI1 scalar/native queue-effect audit for one observed record.

    Queue views are raw inside the callback, before accepted N08 compaction.
    Native cancel is checked via keep; set_interval writes owner slot zero,
    independently of the slot currently executing. Appends may be refused
    at the accepted source capacity (three usable slots).
    """
    if not row.get("native_valid"):
        return dict(checked=False)
    if original is None:
        evidence("ru");original=OriginalDoor("ru")
    def signed(value):
        value=int(value)&0xffffffff
        return value if value<0x80000000 else value-0x100000000
    vector=[int(row[f"native_input_{i}"]) for i in range(17)]
    for i in (2,5,6,8,10):vector[i]=signed(vector[i])
    before=[int(row[f"native_before_{i}"]) for i in range(18)]
    after=[int(row[f"native_after_{i}"]) for i in range(18)]
    for state in (before,after):
        for i in (1,4,5,6):state[i]=signed(state[i])
    expected_before=[vector[i] for i in (1,2,3,4,5,6,10,11,15)]
    assert before[:9]==expected_before,("Live original input binding",row.get("ordinal"),vector,before)
    want=original.run(vector)
    if int(row.get("action",0))==11:
        # Outer native hero Use always consumes Use after target handler;
        # target callback alone is not the source of this final consumption.
        want[7]&=~0x800
    assert after[:9]==want[:9],("Live native door scalar state",row.get("ordinal"),vector,want[:9],after[:9])
    op=vector[0]
    assert 0<=before[9]<=3 and 0<=after[9]<=3
    queue=[before[10+2*i:12+2*i] for i in range(before[9])]
    native=[want[10+2*i:12+2*i] for i in range(want[9])]
    keep=1
    if op in (1,2,3):
        keep=int(original.keep)
        assert int(row["native_keep"])==keep,("Live native cancellation",row.get("ordinal"),op,keep,row["native_keep"])
        assert queue,"Live native callback has no current owner slot"
        if (op==1 and vector[3]==2 and vector[2]<vector[6]-1) or (op==2 and vector[5]<1 and vector[3]==1 and vector[2]>0):
            queue[0][1]=vector[10]&0xffffffff
        if keep:
            assert native and native[0][0]==op
            native=native[1:]
    queue=(queue+native)[:3]
    actual=[after[10+2*i:12+2*i] for i in range(after[9])]
    assert actual==queue,("Live original queue operations",row.get("ordinal"),vector,before,after,queue)
    assert after[16:]==[0,0],"Unsupported fourth native runtime queue slot"
    return dict(checked=True,operation=op,keep=keep,requested_appends=len(native),accepted_appends=max(0,len(queue)-before[9]),raw_queue_before=before[9],raw_queue_after=len(queue))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--render-dump",type=Path)
    parser.add_argument("--world-dump",type=Path)
    parser.add_argument("--dump",type=Path,help="Optional NDS host text-protocol executable")
    args=parser.parse_args()
    before=sources()
    cases=vectors()
    report=dict(task="N11",status="passed",passed=True,whole_dos_session=False,unicorn_version=unicorn.__version__,script=pin(Path(__file__)),samples_before=before,protocol=dict(input="op flags phase state ready occupancy extent lock mode delay interval event keys sourceclass fps marker originalflags",output="flags phase state ready occupancy extent interval residualevent marker queuecount [callback delay]x4"),languages={},limitations=["One direct native callback at a time; queue calls recorded, not scheduler execution", "Sound, UI and runtime argument-copy intercepted", "Controlled relocated far pointers, not runtime DPMI selectors", "NDS fully-open suppression deliberately differs from native residual crop"])
    first=None
    for language in ("ru","en"):
        pins=evidence(language)
        oracle=OriginalDoor(language)
        outputs=[oracle.run(v) for v in cases]
        if first is None:first=outputs
        else:assert outputs==first,"RU/EN source interaction semantics diverge"
        if args.dump:
            data="".join(" ".join(map(str,v))+"\n" for v in cases)
            native=[list(map(int,line.split())) for line in subprocess.check_output([str(args.dump)],input=data,text=True).splitlines()]
            assert len(native)==len(outputs)
            for ordinal,(got,want) in enumerate(zip(native,outputs,strict=True)):
                assert got==want,(language,ordinal,cases[ordinal],got,want)
        crop=crop_vectors(language)
        mirror=mirror_vectors(language)
        cadence=cadence_vectors(language)
        if args.dump:
            c2cases=[[6,0,0,0,0,0,0,0,0,row["ticks"],0,0,0,0,row["old"],0,0] for row in cadence]
            data="".join(" ".join(map(str,v))+"\n" for v in c2cases)
            got=[list(map(int,line.split())) for line in subprocess.check_output([str(args.dump)],input=data,text=True).splitlines()]
            assert [row[0] for row in got]==[row["result"] for row in cadence]
        worlds=world_vectors(language,args.world_dump)
        report["languages"][language]=dict(source_pins=pins,door_base_sensor_vectors=len(cases),crop_vectors=len(crop),mirror_vectors=len(mirror),cadence_vectors=len(cadence),projection_vectors=sum(len(w["projection"]) for w in worlds),absolute_vectors=sum(len(w["absolute"]) for w in worlds),floor_vectors=sum(len(w["floor"]) for w in worlds),crop=crop,mirror=mirror,cadence=cadence,worlds=worlds)
    if args.render_dump:report["renderer_comparison"]=renderer_vectors(args.render_dump)
    report["samples_after"]=sources();assert report["samples_after"]==before
    report["vectors"]=cases
    report["expected"]=first
    report["c_comparison"]=args.dump is not None and args.world_dump is not None and args.render_dump is not None
    report["comparison_counts"]={"door_base_sensor":len(cases)*2 if args.dump else 0,"cadence":336 if args.dump else 0,"use_projection":sum(w["c_comparison"].get("projection_exact",0) for l in report["languages"].values() for w in l["worlds"]),"direct_displacement":sum(w["c_comparison"].get("absolute_exact",0) for l in report["languages"].values() for w in l["worlds"]),"floor":sum(w["c_comparison"].get("floor_exact",0) for l in report["languages"].values() for w in l["worlds"]),"floor_controlled_guards":sum(len(w["c_comparison"].get("floor_guards",[])) for l in report["languages"].values() for w in l["worlds"]),"source_crop":sum(l["crop_vectors"] for l in report["languages"].values()),"source_mirror_pairs":sum(l["mirror_vectors"] for l in report["languages"].values()),"render_geometry":4590 if args.render_dump else 0}
    if args.dump:report["dump"]=pin(args.dump.resolve())
    if args.world_dump:report["world_dump"]=pin(args.world_dump.resolve())
    path=OUT / "original-interactions.json"
    path.write_bytes((json.dumps(report,indent=2)+"\n").encode())
    print(json.dumps(dict(task="N11",passed=True,door_vectors_per_language=len(cases),crop_per_language=len(crop),mirror_per_language=len(mirror),c_comparison=report["c_comparison"],report=pin(path))))


if __name__=="__main__":
    main()

