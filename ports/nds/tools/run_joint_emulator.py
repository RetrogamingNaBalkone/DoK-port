"""N21P ABI2 real-input and separately retained display-snapshot observer.

Input checks use the published live48 state; native pixels use LAST SHOWN state.
No emulated RAM is changed and no missing product audit counters are fabricated.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
import run_map_emulator as maps
import run_joint_profile_emulator as profile
from joint_hud_oracle import observe_joint_hud
from joint_profile_abi import decode_profile
render=maps.render
signed=maps.signed
ViewOracle=maps.ViewOracle
ROOT=Path(__file__).resolve().parents[3]


def input_probe(display,live):
    p=list(display)
    mapping={3:2,4:3,5:0,6:4,7:5,10:9,11:10,12:11,13:12,14:13,15:14,16:15,17:16,
        18:17,19:1,20:6,21:7,22:8,30:38,31:44,32:42,33:41,35:31,36:39,37:40,
        38:18,39:19,40:20,41:21,42:43,43:22,44:23,45:24,46:25,47:26,48:27,
        49:28,52:29,53:32,54:30,56:33,57:34,58:36,59:37}
    for dst,source in mapping.items():p[dst]=live[source]
    p[50]=(maps.quotient(signed(p[12])*p[10],65536)-128)&0xffffffff
    p[51]=(maps.quotient(signed(p[13])*p[10],65536)-80)&0xffffffff
    return p


# Input logic adapted from accepted N20 observer SHA256 b4d0cd9eedbc000d4c746bbcd32e6a419dfdb81bd30ec69add9f39f24f75f8f9
def observe_input_and_display(self,p,display):
    self.map_report["last_probe"]=list(p)
    if not self.map_last or p[7]!=self.map_last[7]:self.write_map_report()
    assert p[31]==0,"Map runtime fault"
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
    self.observe_map_pixels(display)
    self.map_last=list(p)


class JointFrontend(profile.ProfileMapFrontend):
    observe_hud=observe_joint_hud
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.profile_report['joint_observer_sha256']=profile.transport.sha(Path(__file__))
        self.profile_report['joint_hud_oracle_sha256']=profile.transport.sha(ROOT/'ports/nds/tools/joint_hud_oracle.py')
        self.profile_report['profile_decoder_sha256']=profile.transport.sha(ROOT/'ports/nds/tools/joint_profile_abi.py')
        self.map_report['abi2_snapshot_contract']='Live input/control/counters separated from last shown raster state'
        self.stage_pointer=None
        self.stage_history=None
    def collect_stages(self,display):
        if display[0]==0x20504b44 and display[60]:
            pointer=display[60]
            assert 0x02000000<=pointer<0x02380000-256
            if self.stage_pointer is None:self.stage_pointer=pointer
            assert self.stage_pointer==pointer,'Stage storage moved during the session'
        if self.stage_pointer is None:return
        at=self.stage_pointer+64
        before=render.ram(self,at+4,4)
        latest=struct.unpack('<48I',render.ram(self,at,192))
        after=render.ram(self,at+4,4)
        valid=before==after and not latest[1]&1 and latest[0]==0x21504b44
        if valid and latest[40]:
            assert latest[41:43]==(8,192)
            history=(latest[40],latest[41],latest[42])
            if self.stage_history is not None:assert self.stage_history==history
            self.stage_history=history
        rows=[]
        if self.stage_history:
            pointer,capacity,stride=self.stage_history
            before_ring=render.ram(self,pointer,capacity*stride)
            raw=render.ram(self,pointer,capacity*stride)
            after_ring=render.ram(self,pointer,capacity*stride)
            for slot in range(capacity):
                row=struct.unpack_from('<48I',raw,slot*stride)
                at=slot*stride+4
                if not before_ring[at:at+4]==raw[at:at+4]==after_ring[at:at+4]:continue
                if row[0]!=0x21504b44 or row[1]&1 or not row[4]:continue
                assert (row[4]-1)%capacity==slot
                rows.append(row)
        elif valid:rows.append(latest)
        for values in sorted(rows,key=lambda row:row[4]):
            row=decode_profile(values);key=row['frame_id']
            if key in self.profile_seen:continue
            assert row['abi'] in (1,2,3) and row['timer_hz']==33513982
            self.profile_seen.add(key);row['frontend_frame']=self.frames
            self.profile_report['rows'].append(row)
            if len(self.profile_seen)%100==0:
                (self.profile_directory/'progress.json').write_text(json.dumps(dict(
                    rows=len(self.profile_seen),frame_id=key,case_id=row['case_id'])),encoding='utf8')
    def observe_map(self):
        display=struct.unpack('<64I',render.ram(self,self.map_address,256))
        self.collect_stages(display)
        if display[0]!=0x20504b44 or display[1]!=2 or display[2]&1 or not display[61]:return
        live=struct.unpack('<48I',render.ram(self,display[61],192))
        assert live[44]==0,'Live map fault'
        self.map_report['last_display_probe']=list(display)
        self.map_report['last_live_state']=list(live)
        observe_input_and_display(self,input_probe(display,live),display)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--language',type=int,choices=(1,2),default=1)
    parser.add_argument('--packet',type=Path,default=ROOT/'build/nds/n21p/scene.dat')
    args=parser.parse_args();original,argv=maps.MapFrontend,sys.argv
    try:
        maps.MapFrontend=JointFrontend
        sys.argv=[__file__,'--target',str(args.target),'--language',str(args.language),'--packet',str(args.packet)]
        code=maps.main();f=JointFrontend.active_instance
        accumulated=json.loads((args.target/'emulator/result.json').read_text('utf8'))
        f.profile_report['expected_presentations']=accumulated['last_observation']['total_frames']
        expected=set(range(1,f.profile_report['expected_presentations']+1))
        f.profile_report['missing_frames']=sorted(expected-f.profile_seen)
        f.profile_report['profile_ring_used']=f.stage_history is not None
        f.profile_report['passed']=code==0 and f.profile_seen==expected;f.write_profile()
        print(json.dumps(dict(joint_profile_passed=f.profile_report['passed'],rows=len(f.profile_report['rows']))))
        return 0 if f.profile_report['passed'] else 1
    finally:maps.MapFrontend,sys.argv=original,argv

if __name__=='__main__':raise SystemExit(main())
