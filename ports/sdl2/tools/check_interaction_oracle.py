"""Compare SP10 C++ callbacks with independent relocated original instructions.

Only external scheduler/audio/HUD boundaries are stubbed; occupancy helper and
callback instructions execute natively in Unicorn. Interval7 avoids the port's
explicit interval0 / FPS adaptation. No DOS playthrough is claimed.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp10-interactions'
LAYOUT={1:('old-gold',0x2cdf,0x2dbb,0x2e8e),2:('ru',0x3262,0x334e,0x3427),3:('en',0x3917,0x3a03,0x3adc)}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

class Oracle:
    def __init__(self,profile):
        self.profile=profile
        folder=OUT/'ghidra'/LAYOUT[profile][0]
        manifest=json.loads((folder/'manifest.json').read_text())
        raw=(folder/'objects.bin').read_bytes()
        assert sha(folder/'objects.bin')==manifest['bytes_sha256']['objects']
        self.cpu=c=Uc(UC_ARCH_X86,UC_MODE_16)
        c.mem_map(0,0x100000)
        c.mem_write(0x50000,raw)
        self.copy_offset=0x1347 if profile==1 else 0x5cd5
        self.copy_address=0x70000+self.copy_offset
        copy=(folder/'copy-stack.bin').read_bytes()
        assert sha(folder/'copy-stack.bin')==manifest['bytes_sha256']['copy-stack']
        c.mem_write(self.copy_address,copy)
        c.hook_add(UC_HOOK_CODE,self.hook)
        self.calls=[]

    def hook(self,c,address,size,unused):
        if address==0x5fff0:
            c.emu_stop();return
        if getattr(self,'surface_mode',False) and c.reg_read(R.UC_X86_REG_CS)==0x5000:
            offset=address-0x50000
            if offset in [0x1188,0x18b9,0x908]:
                sp=c.reg_read(R.UC_X86_REG_SP)
                value=0
                if offset==0x1188:
                    plane=struct.unpack('<H',c.mem_read(0x30000+sp+8,2))[0]
                    value=0x90002000 if plane==2 else 0x90003000
                elif offset==0x18b9:
                    self.surface_calls.append(struct.unpack('<hh',c.mem_read(0x30000+sp+8,4)))
                ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
                c.reg_write(R.UC_X86_REG_AX,value&65535);c.reg_write(R.UC_X86_REG_DX,value>>16)
                c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
                return
        if address==0x10000+self.copy_offset:
            c.reg_write(R.UC_X86_REG_CS,0x7000)
            c.reg_write(R.UC_X86_REG_IP,self.copy_offset)
            return
        if 0x70000<=address<0x80000:return
        if c.reg_read(R.UC_X86_REG_CS)!=0x5000:
            self.calls.append(hex(address))
            sp=c.reg_read(R.UC_X86_REG_SP)
            ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
            c.reg_write(R.UC_X86_REG_AX,0)
            c.reg_write(R.UC_X86_REG_DX,0)
            c.reg_write(R.UC_X86_REG_SP,sp+4)
            c.reg_write(R.UC_X86_REG_CS,cs)
            c.reg_write(R.UC_X86_REG_IP,ip)

    def run(self,event,state,phase,occupants,ready,flags):
        c=self.cpu
        c.mem_write(0x91000,b'\0'*0x100)
        for offset,value in [(0x15,flags),(0x50,phase),(0x52,7),(0x56,occupants),(0x60,30),(0x6a,0x2442)]:
            c.mem_write(0x91000+offset,struct.pack('<H',value))
        c.mem_write(0x91054,bytes([state,ready]))
        c.mem_write(0x91066,struct.pack('<I',40000))
        c.mem_write(0x3f000,struct.pack('<HHI',0xfff0,0x5000,0x90001000))
        for reg,value in [(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0),(R.UC_X86_REG_EFLAGS,2)]:
            c.reg_write(reg,value)
        offset=LAYOUT[self.profile][event-1]
        c.emu_start(0x50000+offset,0,count=20000)
        assert c.reg_read(R.UC_X86_REG_IP)==0xfff0,'callback did not return'
        b=bytes(c.mem_read(0x91000,0x100))
        return [struct.unpack_from('<H',b,0x50)[0],b[0x54],b[0x55],struct.unpack_from('<H',b,0x15)[0],struct.unpack_from('<H',b,0x6a)[0]]

    def base_use(self,flags,event,source):
        c=self.cpu
        offset={1:0x400,2:0x521,3:0xa01}[self.profile]
        folder=OUT/'ghidra'/LAYOUT[self.profile][0]
        vtable,base={1:(0x612,0x400),2:(0x63d,0x500),3:(0x656,0x500)}[self.profile]
        far=struct.unpack_from('<HH',(folder/'vtables.bin').read_bytes(),vtable-base+0x14)
        assert far[0]==offset
        c.mem_write(0x91000,b'\0'*0x100)
        c.mem_write(0x91015,struct.pack('<H',flags))
        words=struct.pack('<HHH',0x1111,0x2222,0x3333)
        c.mem_write(0x91054,words)
        c.mem_write(0x92000,struct.pack('<I',event))
        c.mem_write(0x3f000,struct.pack('<HHIII',0xfff0,0x5000,0x90001000,0x90002000,0x90003000 if source else 0))
        for reg,value in [(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,value)
        c.emu_start(0x50000+offset,0,count=20000)
        assert c.reg_read(R.UC_X86_REG_IP)==0xfff0
        result=struct.unpack('<I',c.mem_read(0x92000,4))[0]
        assert bytes(c.mem_read(0x91054,6))==words
        expected=event&~0x40000 if event&0x40000 and source and not flags&0x18 else event
        assert result==expected
        return dict(flags=flags,event=event,source=source,result=result)

    def surface(self,category,value):
        assert self.profile==1
        c=self.cpu;self.surface_mode=True;self.surface_calls=[]
        c.mem_write(0x91000,b'\0'*0x3000)
        c.mem_write(0x92010,struct.pack('<HH',0x4000,12))
        c.mem_write(0x92066,struct.pack('<hh',category,value))
        c.mem_write(0x34028,struct.pack('<HH',0,0x6000))
        c.mem_write(0x3f000,struct.pack('<HHI',0xfff0,0x5000,0x90001000))
        for reg,v in [(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,v)
        c.emu_start(0x51e16,0,count=20000)
        assert c.reg_read(R.UC_X86_REG_IP)==0xfff0
        assert self.surface_calls==[(category,value)]
        flags=struct.unpack('<I',c.mem_read(0x9101c,4))[0]
        assert not flags&0x4000000
        self.surface_mode=False
        return dict(category=category,value=value,stat_calls=self.surface_calls,slow_flag=False)

def main():
    inventory=json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    originals=[p['sample'] for p in inventory]+['originals/Old_Gold_3D/OLD_GOLD.RES','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES']
    before={p:sha(ROOT/p) for p in originals}
    for p in inventory:assert before[p['sample']]==p['sha256_before']
    result=subprocess.run([str(ROOT/'build/sdl2/windows/dok_interaction_tests.exe'),'--native-matrix'],capture_output=True,text=True,check=True)
    (OUT/'cpp-matrix.csv').write_text(result.stdout)
    oracles={p:Oracle(p) for p in LAYOUT}
    vectors=[]
    for line in result.stdout.splitlines():
        values=list(map(int,line.split(',')))
        profile,event,state,phase,occupants,ready,flags=values[:7]
        native=oracles[profile].run(event,state,phase,occupants,ready,flags)
        port=values[7:]
        vectors.append(dict(input=values[:7],native=native,port=port,equal=native==port))
    base_use={LAYOUT[p][0]:[o.base_use(flags,event,source)
              for flags in [0,8,0x10,0x18,0x1000,0x1008,0x1010]
              for event in [0,0x800,0x40000,0x40800] for source in [0,1]] for p,o in oracles.items()}
    surface=[oracles[1].surface(category,value) for category in [0,100,102] for value in [-100,-1,1,100]]
    after={p:sha(ROOT/p) for p in originals};assert before==after
    report=dict(vectors=vectors,count=len(vectors),mismatches=sum(not v['equal'] for v in vectors),sha256_before=before,sha256_after=after,
                boundary='Original instructions; external scheduler/audio/HUD stubs. No full DOS session or playthrough.',
                external_calls={LAYOUT[p][0]:sorted(set(o.calls)) for p,o in oracles.items()},class6_base_use=base_use,og_surface_calls=surface)
    (OUT/'callback-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f"{len(vectors)} callbacks, {report['mismatches']} mismatches; hashes unchanged")
    for v in vectors:
        if not v['equal']:print(v);break
    assert not report['mismatches']

if __name__=='__main__':main()
