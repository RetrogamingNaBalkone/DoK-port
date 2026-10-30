"""N19 independent original-resource HUD observer plus the audio/gameplay gate."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import traceback

import make_assets_fixture as fat
import run_audio_emulator as audio
import run_renderer_emulator as render
from hud_pixel_oracle import OriginalHudArt, render_hud

ROOT = Path(__file__).resolve().parents[3]
BaseAudioFrontend = audio.AudioFrontend


class HudFrontend(BaseAudioFrontend):
    art = None
    messages = None

    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.hud_directory=self.target/"hud"
        self.hud_directory.mkdir(parents=True,exist_ok=True)
        self.ui_address,size=audio.transport.elf_symbol(self.target/"dok_nds_gameplay.elf","g_nds_ui_probe")
        if size!=256:
            raise ValueError("UI telemetry size changed")
        self.hud_report=dict(task="N19",passed=False,rows=[],pixel_checks=0,hardware_pixel_checks=0,
                            source="Original RES fonts/images and original EXE/RES message bytes; no ui.dat used by the pixel oracle",
                            rom_sha256=audio.transport.sha(self.target/"dok_nds_gameplay.nds"),
                            elf_sha256=audio.transport.sha(self.target/"dok_nds_gameplay.elf"))
        self.hud_seen=set()
        self.hud_stable=None
        self.hud_stability=0
        self.hud_capture_seen=set()
        self.write_hud_report()

    def observe_audio(self):
        super().observe_audio()
        try:
            self.observe_hud()
        except Exception:
            self.hud_report["error"]=traceback.format_exc()
            self.write_hud_report()
            raise

    def observe_hud(self):
        p=struct.unpack("<64I",render.ram(self,self.ui_address,256))
        if p[0]!=0x19504b44 or p[1]!=1 or p[2]&1:
            return
        if p[28]:
            self.hud_report["fault_probe"]=list(p)
            raise AssertionError("Actual UI runtime fault")
        if p[3] not in (1,2) or p[4] not in (1,2,3):
            return
        if p[36:44]!=p[44:52]:
            raise AssertionError("Language presentation changed the initial world")
        if p[56] and p[57] and render.ram(self,p[56],32)!=render.ram(self,p[57],32):
            raise AssertionError("UI language changed the live gameplay presentation descriptor")
        key=(p[5],p[3],p[4],*p[9:21],p[29],p[30],p[31])
        self.hud_report["last_probe"]=list(p)
        if key!=self.hud_stable:
            self.hud_stable=key;self.hud_stability=1
        else:
            self.hud_stability+=1
        if key in self.hud_seen or self.hud_stability<3 or not p[54]:
            return
        message=b""
        if p[17]:
            lookup=(p[3],p[4] if p[17]==1 else 0,p[17],p[18],p[31] if p[17]==1 else 0)
            if lookup not in self.messages:
                raise AssertionError(f"Unproven original message binding {lookup}")
            message=self.messages[lookup]
        actual_message=render.ram(self,p[33],p[32]) if p[32] else b""
        if actual_message!=message:
            raise AssertionError("Actual ARM9 text span differs from original EXE/RES bytes")
        names=("health","charge","max_health","max_charge","lives","score")
        state=dict(language=p[3],level=p[4],keys=p[15],weapon=p[16])
        state.update({name:value if value<0x80000000 else value-0x100000000
                      for name,value in zip(names,p[9:15])})
        expected,meta=render_hud(self.art,state,message,p[19])
        actual=render.ram(self,p[53],9728)
        if actual!=expected:
            first=next(i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b)
            self.hud_report["pixel_mismatch"]=dict(x=first%256,y=154+first//256,
                actual=actual[first],expected=expected[first],probe=list(p),state=state)
            self.capture("hud-mismatch")
            raise AssertionError(f"HUD original-resource pixel mismatch at {first%256},{154+first//256}")
        palette=struct.unpack("<256H",render.ram(self,p[55],512))
        width,height,rgb=self.rgb()
        if (width,height)!=(256,384):
            raise AssertionError("Screen geometry changed")
        # The stable stage has crossed at least two complete frontend display
        # frames; compare actual LCD scanout, not a mirror pass flag.
        for i,index in enumerate(expected):
            at=(154*256+i)*3
            value=(rgb[at]>>3)|((rgb[at+1]>>3)<<5)|((rgb[at+2]>>3)<<10)
            if value!=(palette[index]&32767):
                self.hud_report["hardware_mismatch"]=dict(x=i%256,y=154+i//256,
                    expected=palette[index]&32767,actual=value,probe=list(p))
                self.capture("hud-hardware-mismatch")
                raise AssertionError("Actual scanned HUD RGB differs from original-resource target")
        self.hud_report["pixel_checks"]+=9728
        self.hud_report["hardware_pixel_checks"]+=9728
        row=dict(language=p[3],level=p[4],epoch=p[5],serial=p[6],stage=p[29],stage_count=p[30],
                 kind=p[17],message_id=p[18],slot=p[31],page=p[19],pages=p[20],frame_id=p[34],
                 source_ticks=p[26],updates=p[23],uploaded_bytes=p[24],uploaded_rows=p[25],
                 state=state,expected=meta,world_before=list(p[36:44]),world_after=list(p[44:52]))
        # Keep all measured states and only justified visual control samples.
        capture_key=(p[3],p[4],"limits" if not p[17] else "long" if p[20]>1 else "native" if p[17]==2 else "message",p[35] if p[35]<8 else p[19])
        if capture_key not in self.hud_capture_seen:
            self.hud_capture_seen.add(capture_key)
            row["images"]=self.capture(f"hud-{p[3]}-{p[4]}-{p[29]:03}-{p[19]}")
        self.hud_report["rows"].append(row)
        self.hud_seen.add(key)
        if p[30]:
            self.write_hud_report()

    def write_hud_report(self):
        (self.hud_directory/"result.json").write_text(json.dumps(self.hud_report,ensure_ascii=False,indent=2)+"\n","utf8")

    def close(self):
        if hasattr(self,"hud_report"):
            self.write_hud_report()
        super().close()


def original_messages(art):
    catalog=json.loads((ROOT/"research/evidence/nds/n19/ui-catalog.json").read_text("utf8"))
    result={};native={1:0,2:0}
    exes={language:(ROOT/("originals/pkr/RUS/PKR.EXE" if language==1 else "originals/pkr/ENG/PKE.EXE")).read_bytes()
          for language in (1,2)}
    for row in catalog["messages"]:
        language=row["language"]
        source=exes[language] if row["source"]=="native-door" else art.sources[language]["data"]
        raw=source[row["file_offset"]:row["file_offset"]+row["bytes"]]
        if hashlib.sha256(raw).hexdigest()!=row["sha256"] or raw.hex()!=row["cp866_hex"]:
            raise AssertionError("Original message provenance differs")
        if row["source"]=="native-door":
            key=(language,0,2,native[language],0);native[language]+=1
        else:
            key=(language,row["level"],1,row["glyph"],row["native_offset"])
        result[key]=raw
    assert native=={1:7,2:7}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target",type=Path,required=True)
    parser.add_argument("--language",type=int,choices=(1,2),default=1)
    parser.add_argument("--packet",type=Path,default=ROOT/"build/nds/n19/scene.dat")
    args=parser.parse_args()
    HudFrontend.art=OriginalHudArt();HudFrontend.messages=original_messages(HudFrontend.art)
    original_frontend=audio.AudioFrontend;original_writer=fat.write_image;original_argv=sys.argv
    def with_ui(path,files):
        return original_writer(path,[*files,("ui.dat",b"UI      DAT",(ROOT/"build/nds/n19/ui.dat").read_bytes())])
    try:
        audio.AudioFrontend=HudFrontend;fat.write_image=with_ui
        sys.argv=[__file__,"--target",str(args.target),"--packet",str(args.packet),"--language",str(args.language)]
        code=audio.main()
        frontend=HudFrontend.active_instance
        report=frontend.hud_report if frontend else {}
        if frontend:
            fixture=json.loads((ROOT/"research/evidence/nds/n19/inputs.json").read_text("utf8"))
            coverage={level:{r["stage"] for r in report["rows"] if r["level"]==level and r["stage_count"]} for level in (1,2,3)}
            missing={level:sorted(set(range(fixture["stages"][str(level)]))-coverage[level]) for level in (1,2,3)}
            report["missing_stages"]=missing
            report["passed"]=code==0 and not report.get("error") and not any(missing.values())
            frontend.write_hud_report()
            print(json.dumps(dict(hud_passed=report["passed"],rows=len(report["rows"]),missing=missing)))
        return 0 if report.get("passed") else 1
    finally:
        audio.AudioFrontend=original_frontend;fat.write_image=original_writer;sys.argv=original_argv


if __name__=="__main__":
    raise SystemExit(main())
