"""N21P independent original-resource HUD and actual scanout observer.

Adapted from accepted N19 observer SHA256 ebbd4424144ef2e4a49e55013f07d7b2fd65f8f448b14481fd21b3ff6f44330b.
The product VRAM-memcmp counter is intentionally not an oracle precondition: 
release builds omit that expensive diagnostic; INDEX8 and LCD RGB remain checked.
"""
import struct
import run_renderer_emulator as render
from hud_pixel_oracle import render_hud

def observe_joint_hud(self):
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
    # Fade changes RGB while text/numeric state is unchanged. Stability must
    # include the exact palette, otherwise a new palette is compared with the
    # preceding LCD scanout when presentations arrive every VBlank.
    palette_bytes=render.ram(self,p[55],512) if p[55] else None
    key=(p[5],p[3],p[4],*p[9:21],p[29],p[30],p[31],palette_bytes)
    self.hud_report["last_probe"]=list(p)
    if key!=self.hud_stable:
        self.hud_stable=key;self.hud_stability=1
    else:
        self.hud_stability+=1
    if key in self.hud_seen or self.hud_stability<3 or not p[23] or not p[53]:
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
    palette=struct.unpack("<256H",palette_bytes)
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

