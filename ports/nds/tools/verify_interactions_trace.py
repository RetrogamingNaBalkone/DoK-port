"""Read-only N11 whole-chain original-instruction replay. No emulator writes.

N08 clocks/input/ABI transport are checked by the caller. This verifier binds
observed operands to prior complete snapshots and executes source geometry,
floor division/stat predicate and N09 motion against evolving runtime state.
It does not claim a complete DOS session or reproduce sound/presentation code.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import sys

import audit_collision as collision
from audit_movement import FIELDS
from audit_interactions import OriginalDoor, evidence, sources, verify_live_interaction
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn import x86_const as R

ROOT=Path(__file__).resolve().parents[3]
SIGNED={name for name,_,code in FIELDS if code!="I"}

def signed(n):
    n=int(n)&0xffffffff
    return n if n<0x80000000 else n-0x100000000

def trunc(n,d):
    return abs(n)//abs(d)*(1 if (n>=0)==(d>=0) else -1)

def state(row):
    return {name:signed(row[name]) if name in SIGNED else int(row[name]) for name,_,_ in FIELDS}

def decode(data):
    assert len(data)==19520
    return dict(maps=[data[64+4096*p:64+4096*(p+1)] for p in range(4)],flags=list(struct.unpack_from("<256H",data,16448)),counters=list(struct.unpack_from("<256H",data,16960)),known=data[17472:17728],phase=list(struct.unpack_from("<256h",data,17728)),extent=list(struct.unpack_from("<256h",data,18240)),state=data[18752:19008],ready=data[19008:19264],marker=data[19264:19520])

class Source(collision.Original):
    def __init__(self,level):
        f=collision.fixture("ru",level)
        super().__init__("ru",f)
        self.model=json.loads((ROOT/f"research/evidence/runtime/ru/LEVEL{level}-runtime.json").read_text())
        self.objects={o["glyph"]:o for o in self.model["objects"]}
        for role,addr in (("actor",0x50000),("world",0x60000)):
            path=ROOT/"research/evidence/nds/n11/original/ru"/(self.p[role]+".bin")
            raw=path.read_bytes()
            assert raw==(collision.OUT/"original/ru"/path.name).read_bytes()
            self.cpu.mem_write(addr,raw)
        self.main=Uc(UC_ARCH_X86,UC_MODE_16);self.main.mem_map(0,0x100000)
        self.main.mem_write(0x50000,(ROOT/"research/evidence/nds/n11/original/ru/1008.bin").read_bytes())
    def hook(self,c,address,size,unused):
        if address==int(self.p["world"],16)*16+self.p["resolve"]:
            c.reg_write(R.UC_X86_REG_CS,0x6000);c.reg_write(R.UC_X86_REG_IP,self.p["resolve"])
        else:super().hook(c,address,size,unused)
    def load(self,snapshot):
        for p in range(3):self.cpu.mem_write(0x30000+self.p["mmap"]+p*4356,snapshot["maps"][p])
        for g in range(256):
            self.cpu.mem_write(0x81000+g*0x80+0x15,struct.pack("<H",snapshot["flags"][g]))
            self.counter(g,snapshot["counters"][g])
    def memory(self):
        return bytes(self.cpu.mem_read(0x30000+self.p["mmap"],4096)),[struct.unpack("<H",self.cpu.mem_read(0x81000+g*0x80+0x56,2))[0] for g in range(256)]
    def actor(self,m):
        b=bytearray(0x100)
        for name,offset,code in FIELDS:struct.pack_into("<"+code,b,offset,m[name])
        b[0x5c]=self.f["glyph"];self.cpu.mem_write(0x40100,bytes(b))
    def floor_glyph(self,m):
        self.actor(m);self.call(0x1435,struct.pack("<HHH",0x100,0x4000,2),0x5000)
        ptr=(self.reg("DX")<<4)+self.reg("AX")
        assert (ptr-0x81000)%0x80==0
        glyph=(ptr-0x81000)//0x80;assert 0<=glyph<256
        return glyph
    def floor_value(self,glyph,c2):
        value=next(f["number"] for f in self.objects[glyph]["fields"] if f["offset"]==0x68)
        self.cpu.mem_write(0x81000+glyph*0x80+0x68,struct.pack("<h",value))
        self.cpu.mem_write(0x300c2,struct.pack("<I",c2))
        for name,v in (("DS",0x3000),("ES",0x8000),("BX",0x1000+glyph*0x80),("CS",0x5000)):
            self.cpu.reg_write(getattr(R,"UC_X86_REG_"+name),v)
        self.cpu.emu_start(0x5222e,0x5223b,count=30)
        n=self.reg("AX");return n if n<32768 else n-65536
    def absolute(self,m,x,z):
        self.actor(m);self.call(0x10a4,struct.pack("<HHii",0x100,0x4000,x,z),0x5000)
        result=dict(m);result["x"],result["y"],result["z"]=struct.unpack("<iii",self.cpu.mem_read(0x40126,12))
        return result
    def finish(self,m,score,total,value):
        self.actor(m)
        self.cpu.mem_write(0x4015e,struct.pack("<h",100));self.cpu.mem_write(0x401d8,struct.pack("<h",score))
        self.cpu.mem_write(0x90710,struct.pack("<h",total))
        self.call(0x1cc2,struct.pack("<HHhh",0x100,0x4000,1000,value),0x5000)
        return struct.unpack("<h",self.cpu.mem_read(0x4015e,2))[0]==-1
    def cadence(self,old,ticks):
        assert ticks
        self.main.mem_write(0x300c2,struct.pack("<I",old));self.main.mem_write(0x31f9a,struct.pack("<I",ticks))
        self.main.reg_write(R.UC_X86_REG_DS,0x3000);self.main.reg_write(R.UC_X86_REG_CS,0x5000)
        self.main.emu_start(0x50fd1,0x50ffc,count=100)
        return struct.unpack("<I",self.main.mem_read(0x300c2,4))[0]


def verify_trace(rows,snapshots):
    assert rows,"Empty N11 whole-chain trace"
    before_samples=sources();evidence("ru")
    source=None;door=OriginalDoor("ru");epoch=None;current=None;current_motion=None
    previous_floor=None;pending=None;floor_state=None;history=[];previous=None
    last_sweep=None;cadence_pending=False;c2=4
    counts=dict(native_operations=0,native_queue_deadlines=0,operand_snapshot_bindings=0,source_floor_lookups=0,source_floor_divisions=0,source_conveyor_callbacks=0,source_motion_callbacks=0,source_motion_substeps=0,exact_mmap_bytes=0,exact_counter_bytes=0,source_finish_predicates=0,sensor_entries=0,cadence_updates=0,camera_renders=0,fixture_resets=0)
    per_case={}
    def compare_memory(expected,s,ordinal):
        mmap,counters=expected
        assert mmap==s["maps"][0],("Source/NDS MMap",ordinal)
        assert counters==s["counters"],("Source/NDS occupancy",ordinal)
        counts["exact_mmap_bytes"]+=4096;counts["exact_counter_bytes"]+=512
    def pose(m):return (m["x"],m["y"],m["z"],m["heading"]%360*65536)
    for row in rows:
        action=row["action"];ordinal=row["ordinal"]
        s=decode(snapshots[row["snapshot_ordinal"]]) if row.get("snapshot_ordinal") else None
        if row["world_epoch"]!=epoch:
            assert action==6,"New world must begin with LEVEL_BEGIN"
            epoch=row["world_epoch"];source=Source(row["level_id"]);current=s;current_motion=state(row)
            previous_floor=None;pending=None;history=[(row["camera_current_tick"],pose(current_motion))]
            c2=4;last_sweep=None;cadence_pending=False
            per_case[row["case_id"]]=dict(native=0,hero=0,floor=0,conveyor=0,render=0)
        assert source is not None and current is not None
        case=per_case[row["case_id"]]
        if action not in (1,4,5,6) and cadence_pending:
            if not row["paused"] and last_sweep["status"]!=3 and last_sweep["sweep_ticks"]:
                c2=source.cadence(c2,last_sweep["sweep_ticks"]);counts["cadence_updates"]+=1
            cadence_pending=False
        if action==4:cadence_pending=False
        if action==5:cadence_pending=False
        assert row["c2"]==c2,("Source C2",ordinal,row["c2"],c2)
        if row.get("native_valid"):
            verify_live_interaction(row,door);counts["native_operations"]+=1;case["native"]+=1
            g=row["native_target_glyph"]
            native=[row[f"native_before_{i}"] for i in range(18)]
            bound=[current["flags"][g],current["phase"][g]&0xffffffff,current["state"][g],current["ready"][g],current["counters"][g] if current["counters"][g]<32768 else (current["counters"][g]-65536)&0xffffffff,current["extent"][g]&0xffffffff,current["marker"][g]]
            assert [native[i] for i in (0,1,2,3,4,5,8)]==bound,("Source operands not bound to previous snapshot",ordinal,g)
            counts["operand_snapshot_bindings"]+=1
            n_before=native[9];n_after=row["native_after_9"]
            for i in range(min(n_before,n_after)):
                assert row[f"queue_before_deadline_{i}"]==row[f"queue_after_deadline_{i}"],("Callback rewrote deadline before N08 postprocessing",ordinal,i)
            lo=previous["native_now"] if previous is not None and previous["world_epoch"]==epoch else 0
            for i in range(n_before,n_after):
                delay=row[f"native_after_{11+2*i}"]
                tick=(row[f"queue_after_deadline_{i}"]-delay)&0xffffffff
                assert ((tick-lo)&0xffffffff)<=((row["native_now"]-lo)&0xffffffff),("Scheduled deadline outside observed source clock range",ordinal,i,tick,lo,row["native_now"])
                counts["native_queue_deadlines"]+=1
        if action==17:
            assert pending is None,"Nested hero callback"
            m=state(row)
            expected=dict(current_motion);expected["flags"]=(expected["flags"]&~0x00182a3c)|row["native_commands"]
            assert m==expected,("Hero before-floor state continuity",ordinal,m,expected)
            assert row["input_flags"]==m["flags"]
            elapsed=(row["service_tick"]-row["watch_start"])&0xffffffff if row["watch_started"] else 0
            assert row["service_elapsed"]==elapsed and row["first_service"]==int(not row["watch_started"])
            pending=m;source.load(current)
        if action==15:
            assert pending is not None and s is not None,"FLOOR without observed hero begin"
            source.load(current);g=source.floor_glyph(pending);obj=source.objects[g]
            expected=dict(pending);enter=obj["class"]==13 and previous_floor!=g
            assert bool(row["native_valid"])==enter,("Sensor identity entry",ordinal,g,previous_floor)
            if enter:
                f={v["offset"]:v.get("number",0) for v in obj["fields"]}
                assert row["native_target_glyph"]==f[0x66] and row["native_input_11"]==f[0x67] and row["native_input_13"]==13
                counts["sensor_entries"]+=1
            finish=False
            if obj["class"]==12:
                expected["flags"]|=0x4000000
                f={v["offset"]:v.get("number",0) for v in obj["fields"]};category=f[0x66]
                value=source.floor_value(g,row["c2"]);counts["source_floor_divisions"]+=1
                if category in (100,101,102):
                    nx=expected["x"]+(value if category!=102 else 0);nz=expected["z"]+(value if category==102 else 0)
                    expected=source.absolute(expected,signed(nx),signed(nz))
                    assert row["collision_requested"]==row["collision_committed"]==1 and not row["collision_rejected"] and not row["collision_reason_mask"]
                    assert row["collision_adjusted"]==int((expected["x"],expected["z"])!=(nx,nz))
                    counts["source_conveyor_callbacks"]+=1;case["conveyor"]+=1
                elif category==1000:
                    total=source.model.get("pickup_99_total")
                    if total is None:
                        initial=json.loads((ROOT/f"research/evidence/nds/n05/oracle/level{row['level_id']}-lang1.json").read_text())
                        total=initial["world"].get("category99_total",initial["world"].get("total_category99"))
                    assert total is not None,"Missing independently recovered category99 total"
                    finish=source.finish(expected,signed(row["score"]),int(total),value)
                    counts["source_finish_predicates"]+=1
            assert state(row)==expected,("Source floor state",ordinal,state(row),expected)
            if previous is not None:assert row["finish_total"]-previous["finish_total"]==int(finish),("Native health-1/FINISH adaptation",ordinal)
            if not enter:compare_memory(source.memory(),s,ordinal)
            else:
                assert current["maps"]==s["maps"] and current["counters"]==s["counters"],"Sensor changed occupancy"
            if row["collision_adjusted"] or row["collision_rejected"]:
                history=[];assert row["camera_previous_tick"]==row["camera_current_tick"]==0,"Conveyor correction failed to reset camera"
            previous_floor=g;floor_state=expected;current=s;counts["source_floor_lookups"]+=1;case["floor"]+=1
        if action==2:
            assert pending is not None and floor_state is not None and s is not None
            source.load(current);values=dict(floor_state);values["elapsed"]=row["service_elapsed"]
            result=source.run_motion(values)
            assert state(row)==result["state"],("Actual source motion/collision state",ordinal,state(row),result)
            assert row["moved"]==result["moved"] and row["substeps"]==result["substeps"]
            compare_memory(source.memory(),s,ordinal)
            counts["source_motion_callbacks"]+=1;counts["source_motion_substeps"]+=result["substeps"];case["hero"]+=1
            if row["collision_adjusted"] or row["collision_rejected"]:history=[]
            commit=(row["camera_current_tick"],pose(state(row)))
            if not history or commit[0]==history[-1][0]:history=[commit]
            else:history=(history+[commit])[-2:]
            assert row["camera_previous_tick"]==(history[-2][0] if len(history)==2 else history[-1][0])
            assert row["watch_start"]==commit[0] and row["watch_started"]==1
            current_motion=state(row);current=s;pending=None;floor_state=None
        if action==9:
            assert s is not None
            current_motion=state(row);current=s
            if not history or row["camera_current_tick"]!=history[-1][0] or row["camera_previous_tick"]==row["camera_current_tick"]:
                history=[(row["camera_current_tick"],pose(current_motion))]
            counts["fixture_resets"]+=1
        if action in (11,12,13,14):
            assert s is not None;assert current["maps"]==s["maps"] and current["counters"]==s["counters"],("Door operation changed occupancy",ordinal)
            current=s
        if action==3:
            assert history
            a=history[0][1];b=history[-1][1];alpha=row["camera_alpha_q16"]
            if len(history)==1:expected=b;assert alpha==65536
            else:
                angle=b[3]-a[3]
                if angle>11796480:angle-=23592960
                if angle< -11796480:angle+=23592960
                expected=tuple(a[i]+trunc((b[i]-a[i])*alpha,65536) for i in range(3))+((a[3]+trunc(angle*alpha,65536))%23592960,)
                interval=history[-1][0]-history[0][0]
                low=previous["native_now"] if previous is not None else history[-1][0]
                amin=min(65536,max(0,((low-history[-1][0])<<16)//interval));amax=min(65536,max(0,((row["native_now"]-history[-1][0])<<16)//interval))
                assert amin<=alpha<=amax,("Camera alpha outside observed clock range",ordinal,alpha,amin,amax)
            source.load(current)
            cell=(current_motion["z"]//480)*64+current_motion["x"]//480
            source.cpu.mem_write(0x30000+source.p["mmap"]+cell,b" ")
            clear=238<=expected[0]<30720-238 and 238<=expected[2]<30720-238 and bool(source.query((0,expected[0],expected[2],0,0))[0])
            assert row["camera_admitted"]==int(clear) and row["camera_fallback"]==int(not clear)
            if not clear:expected=history[-1][1]
            assert (signed(row["camera_x"]),signed(row["camera_y"]),signed(row["camera_z"]),row["camera_heading_q16"])==expected,("Camera interpolation/fallback",ordinal)
            counts["camera_renders"]+=1;case["render"]+=1
        if action==16:
            assert row["sweep_ticks"]==(row["sweep_end"]-row["sweep_begin"])&0xffffffff
            last_sweep=row;cadence_pending=True
        if s is not None and action not in (2,9,15,11,12,13,14,6):
            assert current==s,("Unexpected unowned world mutation",ordinal)
        previous=row
    assert pending is None,"Unfinished observed hero callback"
    assert sources()==before_samples,"Original executable changed during replay"
    assert all(v["hero"] and v["render"] for v in per_case.values()),"Missing case chain coverage"
    return dict(passed=True,counts=counts,cases=per_case,whole_dos_session=False,limitations=["Original queue operations with intercepted services; accepted N08 dispatch checked separately","Fixture rows explicitly replace controlled pose/inventory/occupancy baseline","FINISH event substitutes native health=-1, no campaign transition"])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("result",type=Path);parser.add_argument("snapshots",type=Path)
    args=parser.parse_args();report=json.loads(args.result.read_text())
    rows=report["traces"];raw=args.snapshots.read_bytes();assert len(raw)%19520==0
    snapshots={struct.unpack_from("<I",raw,n+16)[0]:raw[n:n+19520] for n in range(0,len(raw),19520)}
    print(json.dumps(verify_trace(rows,snapshots),indent=2))

if __name__=="__main__":main()
