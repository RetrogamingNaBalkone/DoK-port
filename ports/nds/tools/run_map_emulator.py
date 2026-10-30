"""N20 real-pointer input and independent map scanout observer.

Uses the accumulated N19/N18 gate. No write to emulated memory is permitted.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import traceback

import run_hud_emulator as hud
import run_renderer_emulator as render

ROOT=Path(__file__).resolve().parents[3]
SCALES=(2,3,4,6,8)

def signed(value):
    return value if value<0x80000000 else value-0x100000000

def quotient(a,b):
    return (1 if a>=0 else -1)*(abs(a)//b)

class ViewOracle:
    """The plan's viewport rules, using integer cell fractions in Q16."""
    def __init__(self,p):
        self.width,self.length=p[8:10]
        self.zoom=1;self.follow=True;self.capture=0;self.held=False;self.blocked=False
        self.x=self.z=0;self.offset_x=self.offset_z=0;self.last=(0,0)
        self.step(p)

    def step(self,p):
        held,x,y,paused=bool(p[15]),signed(p[16]),signed(p[17]),bool(p[18])
        hero_x,hero_z=quotient(signed(p[20]),480),quotient(signed(p[21]),480)
        if paused:
            self.capture=0;self.blocked=held;self.held=held
        elif not held:
            self.capture=0;self.blocked=False;self.held=False
        elif not self.blocked:
            if not (0<=x<256 and 0<=y<192):
                self.capture=0;self.blocked=True;self.held=True
            elif not self.held:
                self.held=True;self.last=(x,y)
                if y<160:self.capture=1
                else:
                    self.capture=2 if x<85 else 3 if x<170 else 4
                    old_zoom=self.zoom
                    if self.capture==2:self.zoom=max(0,self.zoom-1)
                    elif self.capture==3:self.zoom=min(4,self.zoom+1)
                    else:self.follow=True;self.offset_x=self.offset_z=0
                    if old_zoom!=self.zoom and self.follow:
                        self.offset_x=self.x-(self.width-hero_x)*65536
                        self.offset_z=self.z-hero_z*65536
            elif self.capture==1:
                dx,dy=x-self.last[0],y-self.last[1]
                if dx or dy:
                    self.follow=False
                    self.x-=quotient(dx*65536,SCALES[self.zoom])
                    self.z-=quotient(dy*65536,SCALES[self.zoom])
                self.last=(x,y)
        if self.follow and not paused:
            self.x=(self.width-hero_x)*65536+self.offset_x;self.z=hero_z*65536+self.offset_z
        scale=SCALES[self.zoom]
        def bound(center,cells,pixels):
            if cells*scale<=pixels:return cells*32768
            edge=(pixels*32768+scale-1)//scale
            return max(edge,min(cells*65536-edge,center))
        self.x=bound(self.x,self.width,256);self.z=bound(self.z,self.length,160)

    def check(self,p):
        expected=(SCALES[self.zoom],int(self.follow),self.x,self.z,self.capture,int(self.blocked))
        actual=(p[10],p[11],signed(p[12]),signed(p[13]),p[14],p[54])
        if actual!=expected:
            raise AssertionError(f"Touch viewport differs from plan: {actual} != {expected}")
        left=quotient(self.x*p[10],65536)-128;top=quotient(self.z*p[10],65536)-80
        assert (signed(p[50]),signed(p[51]))==(left,top)
        assert (signed(p[58]),signed(p[59]))==(self.offset_x,self.offset_z)

def input_plan():
    result=[]
    def hold(name,touch=None,keys=0,frames=10):
        result.append(dict(name=name,touch=touch,keys=keys,frames=frames))
    def tap(name,x):
        hold(name,(x,175));hold(name+" release")
    hold("initial",frames=16)
    tap("minimum scale",40);tap("minimum saturation",40)
    hold("button capture starts",(125,175));hold("button cannot become drag",(220,60));hold("button release")
    for n in range(4):tap(f"zoom plus {n}",125)
    tap("maximum saturation",125)
    hold("new map gesture",(110,70));hold("drag east",(250,70));hold("drag south",(250,150));hold("drag west",(2,150));hold("drag north",(2,2));hold("drag release")
    hold("new touch has no delta",(220,140));hold("new touch release")
    for direction,start,end in (("right",(2,70),(254,70)),("left",(254,70),(2,70)),("down",(120,2),(120,158)),("up",(120,158),(120,2))):
        for n in range(3):
            hold(f"{direction} edge start {n}",start);hold(f"{direction} edge drag {n}",end);hold(f"{direction} edge release {n}")
    tap("center restores follow",210)
    for n in range(4):tap(f"zoom minus {n}",40)
    tap("small map minimum",40)
    for n in range(3):tap(f"return large zoom {n}",125)
    hold("live firing while dragging",(80,60),1<<11)
    hold("live drag with firing",(180,120),1<<11)
    hold("pause during gesture",(180,120),1<<3)
    hold("paused movement ignored",(20,20),frames=18)
    hold("resume held stylus",(20,20),1<<3)
    hold("resume absorbs old gesture",(240,140),frames=18)
    hold("resume stylus release")
    hold("fresh postpause touch",(80,60));hold("fresh postpause drag",(180,120));hold("fresh postpause release")
    tap("final center",210)
    hold("postresume clock and combat",keys=1<<11,frames=24)
    hold("finished",frames=16)
    return result

BaseHudFrontend=hud.HudFrontend

class MapFrontend(BaseHudFrontend):
    native_draw=None
    native_result=None
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.map_directory=self.target/"map";self.map_directory.mkdir(exist_ok=True)
        self.map_address,size=hud.audio.transport.elf_symbol(self.target/"dok_nds_gameplay.elf","g_nds_map_probe")
        assert size==256
        self.map_report=dict(task="N20",passed=False,rows=[],stages=[],index8_checks=0,rgb_checks=0,
            rom_sha256=self.hud_report["rom_sha256"],elf_sha256=self.hud_report["elf_sha256"],
            observer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            access="Real libretro pointer/joypad input, read-only RAM and actual LCD scanout")
        self.map_epoch=None;self.map_frame=None;self.map_view=None;self.map_last=None
        self.map_stable=None;self.map_stability=0;self.map_seen=set()
        self.map_snapshot_seen=set()
        self.plan=input_plan();self.plan_index=0;self.plan_start=None;self.plan_done=False
        self.script_active=False;self.map_camera=None;self.pause_sample=None
        self.write_map_report()

    def input(self,port,device,index,identifier):
        if self.script_active and not self.plan_done and not port and not index and device==1:
            keys=self.plan[self.plan_index]["keys"]
            return keys if identifier==256 else int(bool(keys&(1<<identifier)))
        return super().input(port,device,index,identifier)

    def observe_audio(self):
        super().observe_audio()
        if not hasattr(self,"map_address"):return
        try:self.observe_map()
        except Exception:
            self.map_report["error"]=traceback.format_exc();self.write_map_report();raise

    def observe_map(self):
        p=struct.unpack("<64I",render.ram(self,self.map_address,256))
        if p[0]!=0x20504b44 or p[1]!=1 or p[2]&1:return
        self.map_report["last_probe"]=list(p)
        if not self.map_last or p[7]!=self.map_last[7]:self.write_map_report()
        assert p[31]==0,"Map runtime fault"
        if not p[42]:return
        epoch=(p[3],p[4],p[41])
        new_frame=p[5]!=self.map_frame
        if new_frame and epoch==self.map_epoch and self.map_last and p[10]!=self.map_last[10]:
            # Direct contract assertion, independent of the controller model:
            # changing scale retains the previous center unless new bounds
            # force it to move. The touch fixture does not move the hero.
            for field,cells,pixels in ((12,p[8],256),(13,p[9],160)):
                old_center=signed(self.map_last[field]);edge=(pixels*32768+p[10]-1)//p[10]
                expected=cells*32768 if cells*p[10]<=pixels else max(edge,min(cells*65536-edge,old_center))
                assert signed(p[field])==expected,"Zoom moved the center world point without a bounds constraint"
        if epoch!=self.map_epoch:
            self.map_epoch=epoch;self.map_view=ViewOracle(p)
        elif new_frame:self.map_view.step(p)
        self.map_view.check(p);self.map_frame=p[5]
        if p[7]==63:
            camera=tuple(p[43:46])
            if self.map_camera is None:self.map_camera=camera
            assert camera==self.map_camera,"Touch changed the game camera"
            paused=tuple(p[i] for i in (19,38,39,40))
            if p[18]:
                if self.pause_sample is not None:assert paused==self.pause_sample,"Clock or callbacks advance while paused"
                self.pause_sample=paused
            else:self.pause_sample=None
            if not self.plan_done:
                self.script_active=True
                if self.plan_start is None:self.plan_start=p[5]
                if p[5]-self.plan_start>=self.plan[self.plan_index]["frames"]:
                    self.check_stage(self.plan[self.plan_index]["name"],p)
                    self.map_report["stages"].append(dict(**self.plan[self.plan_index],probe=list(p)))
                    self.plan_index+=1;self.plan_start=p[5];self.write_map_report()
                    if self.plan_index==len(self.plan):self.plan_done=True;self.script_active=False
                self.touch=None if self.plan_done else self.plan[self.plan_index]["touch"]
        self.observe_map_pixels(p)
        self.map_last=list(p)

    def check_stage(self,name,p):
        previous=self.map_report["stages"][-1]["probe"] if self.map_report["stages"] else None
        if name=="initial":assert (p[10],p[11])==(3,1)
        if name in ("minimum scale","minimum saturation","small map minimum"):assert p[10]==2
        if name=="maximum saturation":assert p[10]==8
        if name=="button cannot become drag":
            assert p[14]==3 and p[49]==previous[49] and tuple(p[12:14])==tuple(previous[12:14])
        if name=="new touch has no delta":assert tuple(p[12:14])==tuple(previous[12:14])
        if name in ("center restores follow","final center"):
            assert p[11]==1 and p[58]==p[59]==0
        if name in ("pause during gesture","paused movement ignored"):
            assert p[18]==1 and p[14]==0 and p[54]==1
        if name=="paused movement ignored":assert tuple(p[i] for i in (19,38,39,40))==tuple(previous[i] for i in (19,38,39,40))
        if name in ("resume held stylus","resume absorbs old gesture"):
            assert p[18]==0 and p[14]==0 and p[54]==1
        if name=="resume stylus release":assert p[54]==0
        if name=="fresh postpause touch":assert p[14]==1
        if name=="postresume clock and combat":
            paused=next(s["probe"] for s in self.map_report["stages"] if s["name"]=="paused movement ignored")
            assert p[19]>paused[19] and p[38]>paused[38] and p[40]>paused[40],"Simulation did not resume during map input"
        if name=="finished":
            initial=self.map_report["stages"][0]["probe"]
            assert p[46]==initial[46]+1 and p[47]==initial[47]+1
            assert {s["probe"][10] for s in self.map_report["stages"]}==set(SCALES)

    def observe_map_pixels(self,p):
        size=p[8]*p[9]
        snapshot=dict(width=p[8],length=p[9],hero_x=quotient(signed(p[20]),480),hero_z=quotient(signed(p[21]),480),
            heading=p[22],map_available=p[30],visited=render.ram(self,p[23],size),
            maps=[render.ram(self,p[i],size) for i in (25,24,26)],glyphs=render.ram(self,p[27],1024))
        snapshot_key=(p[3],"final" if p[7]==63 and self.plan_done else "touch" if p[7]==63 else "initial")
        if snapshot_key not in self.map_snapshot_seen:
            self.map_snapshot_seen.add(snapshot_key)
            stored={k:v for k,v in snapshot.items() if k not in ("maps","visited","glyphs")}
            stored.update(maps=[v.hex() for v in snapshot["maps"]],visited=snapshot["visited"].hex(),glyphs=snapshot["glyphs"].hex())
            path=self.map_directory/f"snapshot-{snapshot_key[0]}-{snapshot_key[1]}.json"
            path.write_text(json.dumps(dict(probe=list(p),snapshot=stored),indent=2)+"\n","utf8")
            self.map_report.setdefault("snapshots",[]).append(dict(path=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        key=(p[3],p[7],self.hud_report["last_probe"][3],p[10],p[11],p[14],p[18],p[50],p[51],snapshot["hero_x"],snapshot["hero_z"],p[22],
             hashlib.sha256(b"".join(snapshot["maps"])+snapshot["visited"]+snapshot["glyphs"]).digest())
        self.map_stability=self.map_stability+1 if key==self.map_stable else 1;self.map_stable=key
        if self.map_stability<3 or key in self.map_seen:return
        if self.native_draw is None:return
        if self.native_result is not None:
            pixels,postvisited=self.native_result("ru",snapshot,p[10],signed(p[50]),signed(p[51]))
            actual_post=render.ram(self,p[55],size)
            assert actual_post==postvisited[:size],"Map paint visited side effects differ from original x86"
            assert p[34]==sum(bool(v) for v in snapshot["visited"])
        else:pixels=self.native_draw("ru",snapshot,p[10],signed(p[50]),signed(p[51]))
        expected=bytearray(pixels)
        assert len(expected)==40960
        expected.extend(bytes(8192))
        font=self.art.resources[1]["FONT8"]
        language=self.hud_report["last_probe"][3]
        for button,(left,width,label) in enumerate(((0,85,b"-"),(85,85,b"+"),(170,86,"Центр".encode("cp866") if language==1 else b"Center"))):
            active=p[14]==button+2;fill=2 if active else 86
            for y in range(160,192):expected[y*256+left:y*256+left+width]=bytes([fill])*width;expected[y*256+left]=0
            expected[160*256+left:160*256+left+width]=bytes(width)
            x=left+(width-len(label)*8)//2
            for n,code in enumerate(label):
                for row in range(8):
                    for col in range(8):
                        if font[code*8+row]&(128>>col):expected[(172+row)*256+x+n*8+col]=86 if active else 2
        actual=render.ram(self,p[28],49152)
        if actual!=expected:
            at=next(i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b)
            self.capture("map-index-mismatch")
            raise AssertionError(f"Map differs from original x86 at {at%256},{at//256}: {actual[at]} != {expected[at]}")
        palette=struct.unpack("<256H",render.ram(self,p[29],512))
        assert palette==tuple(self.art.palette(1,p[3])),"Map palette differs from original GAMEP"
        width,height,rgb=self.rgb();assert (width,height)==(256,384)
        for at,index in enumerate(expected):
            pos=(256*192+at)*3
            actual_rgb=(rgb[pos]>>3)|((rgb[pos+1]>>3)<<5)|((rgb[pos+2]>>3)<<10)
            if actual_rgb!=palette[index]:
                self.capture("map-rgb-mismatch")
                raise AssertionError(f"Map actual scanout mismatch at {at%256},{at//256}")
        self.map_report["index8_checks"]+=49152;self.map_report["rgb_checks"]+=49152
        row=dict(level=p[3],case_id=p[7],frame=p[5],ordinal=p[6],scale=p[10],follow=p[11],capture=p[14],paused=p[18],
            left=signed(p[50]),top=signed(p[51]),visited=p[34],disclosure_records=p[35],camera=list(p[43:46]),
            index8_sha256=hashlib.sha256(actual).hexdigest())
        if p[7]==63:row["images"]=self.capture(f"map-{self.plan_index:03}-{p[10]}-{p[14]}")
        self.map_report["rows"].append(row);self.map_seen.add(key)

    def write_map_report(self):
        (self.map_directory/"result.json").write_text(json.dumps(self.map_report,ensure_ascii=False,indent=2)+"\n","utf8")

    def close(self):
        if hasattr(self,"map_report"):self.write_map_report()
        super().close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target",type=Path,required=True)
    parser.add_argument("--language",type=int,choices=(1,2),default=1)
    parser.add_argument("--packet",type=Path,default=ROOT/"build/nds/n20/scene.dat")
    parser.add_argument("--input-only",action="store_true",help="Development input gate; never accepts map rendering")
    args=parser.parse_args()
    # NDS-specific primary x86 instrumentation is supplied by the source audit.
    if not args.input_only:
        from map.original_map import original_map,original_map_result
        MapFrontend.native_draw=staticmethod(original_map)
        MapFrontend.native_result=staticmethod(original_map_result)
    else:MapFrontend.native_draw=None;MapFrontend.native_result=None
    old_frontend=hud.HudFrontend;old_argv=sys.argv
    try:
        hud.HudFrontend=MapFrontend
        sys.argv=[__file__,"--target",str(args.target),"--packet",str(args.packet),"--language",str(args.language)]
        code=hud.main();frontend=MapFrontend.active_instance
        report=frontend.map_report
        report["script_completed"]=frontend.plan_done
        accumulated=json.loads((args.target/"emulator/result.json").read_text("utf8"))
        report["input_checks_passed"]=accumulated["passed"] and frontend.plan_done and not report.get("error")
        report["passed"]=code==0 and frontend.plan_done and not report.get("error") and report["rgb_checks"]>0
        frontend.write_map_report()
        print(json.dumps(dict(map_passed=report["passed"],stages=len(report["stages"]),rows=len(report["rows"]))))
        return 0 if report["passed"] or (args.input_only and report["input_checks_passed"]) else 1
    finally:hud.HudFrontend=old_frontend;sys.argv=old_argv

if __name__=="__main__":raise SystemExit(main())
