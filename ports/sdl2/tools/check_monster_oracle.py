"""SP12 original x86 monster hit/death boundaries versus the compiled port.

Actual instruction bodies execute; scheduler, clock, sound and animation lookup
are explicit external stubs. This is not a DOS playthrough or pixel equivalence.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp12-monsters'
PROFILES={1:('old-gold',0x32ac,0x3323),2:('ru',0x37cf,0x3845),3:('en',0x3f38,0x3fae)}
RES=['originals/Old_Gold_3D/OLD_GOLD.RES','originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

class Oracle:
    def __init__(self,profile):
        self.profile=profile;folder=OUT/'ghidra'/PROFILES[profile][0]
        manifest=json.loads((folder/'manifest.json').read_text())
        for name,digest in manifest['bytes_sha256'].items():assert sha(folder/(name+'.bin'))==digest
        self.cpu=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
        c.mem_write(0x50000,(folder/'objects.bin').read_bytes())
        self.copy=0x1347 if profile==1 else 0x5cd5
        c.mem_write(0x70000+self.copy,(folder/'copy-stack.bin').read_bytes())
        self.calls=set();c.hook_add(UC_HOOK_CODE,self.hook)

    def ret(self,value=0):
        c=self.cpu;sp=c.reg_read(R.UC_X86_REG_SP)
        ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        c.reg_write(R.UC_X86_REG_AX,value&65535);c.reg_write(R.UC_X86_REG_DX,value>>16)
        c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)

    def hook(self,c,address,size,unused):
        if address==0x5fff0:c.emu_stop();return
        if address==0x10000+self.copy:
            c.reg_write(R.UC_X86_REG_CS,0x7000);c.reg_write(R.UC_X86_REG_IP,self.copy);return
        if 0x70000<=address<0x80000:return
        if self.profile==1 and address==0x53dd2:self.ret(0x90002000);return
        if c.reg_read(R.UC_X86_REG_CS)!=0x5000:
            self.calls.add(hex(address))
            # A death-frame lookup has five WORD arguments. Its out parameter
            # is object+52. Other external calls do not mutate actor fields.
            if address in [0x1aadc,0x105df,0x1116d]:
                sp=c.reg_read(R.UC_X86_REG_SP)
                pointer=struct.unpack('<I',c.mem_read(0x30000+sp+10,4))[0]
                segment,offset=pointer>>16,pointer&65535
                c.mem_write(segment*16+offset,struct.pack('<H',7));self.ret(0x90003333);return
            self.ret()

    def invoke(self,offset,args):
        c=self.cpu;c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x5000)+args)
        for reg,value in [(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),
                          (R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,value)
        c.emu_start(0x50000+offset,0,count=30000)
        assert c.reg_read(R.UC_X86_REG_IP)==0xfff0,'Monster callback did not return'

    def damage(self,hp,state,amount,course):
        c=self.cpu;c.mem_write(0x91000,b'\0'*0x3000)
        for at,value in [(0xa4,hp),(0xa2,state),(0x50,9),(0xaa,5),(0xac,2),(0xae,course)]:
            c.mem_write(0x91000+at,struct.pack('<H',value&65535))
        c.mem_write(0x92000,struct.pack('<I',0x40000))
        c.mem_write(0x93000+(0x5a if self.profile==1 else 0x5c),struct.pack('<H',amount&65535))
        self.invoke(PROFILES[self.profile][1],struct.pack('<III',0x90001000,0x90002000,0x90003000))
        values=[struct.unpack('<h',c.mem_read(0x91000+at,2))[0] for at in [0xa4,0xa2,0x50,0xaa,0xac]]
        mask=struct.unpack('<I',c.mem_read(0x92000,4))[0]
        return values+[int(not mask&0x40000)]

    def death(self,height,state,phase,count):
        c=self.cpu;c.mem_write(0x91000,b'\0'*0x5000)
        for at,value in [(0x19,height),(0xa4,0),(0xa2,state),(0x50,phase)]:c.mem_write(0x91000+at,struct.pack('<H',value&65535))
        c.mem_write(0x910ec,struct.pack('<I',0x90004000));c.mem_write(0x94000,struct.pack('<H',count))
        self.invoke(PROFILES[self.profile][2],struct.pack('<I',0x90001000))
        return [struct.unpack('<h',c.mem_read(0x91000+at,2))[0] for at in [0x50,0xa2,0x19]]

def main():
    inventory=json.loads((ROOT/'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    originals=[p['sample'] for p in inventory]+RES
    before={p:sha(ROOT/p) for p in originals}
    for p in inventory:assert before[p['sample']]==p['sha256_before']
    oracles={p:Oracle(p) for p in PROFILES};report={}
    for mode in ['native','death']:
        p=subprocess.run([str(ROOT/'build/sdl2/windows/dok_monster_probe.exe'),'--'+mode+'-matrix',*RES],capture_output=True,text=True,check=True)
        (OUT/(mode+'-cpp.csv')).write_text(p.stdout)
        vectors=[]
        for line in p.stdout.splitlines():
            values=list(map(int,line.split(',')));profile=values[0];args=values[1:5];port=values[5:]
            native=getattr(oracles[profile],'damage' if mode=='native' else 'death')(*args)
            vectors.append(dict(input=values[:5],native=native,port=port,equal=native==port))
        report[mode]=dict(count=len(vectors),mismatches=sum(not v['equal'] for v in vectors),vectors=vectors)
        print(mode,report[mode]['count'],'vectors',report[mode]['mismatches'],'mismatches')
        for v in vectors:
            if not v['equal']:print(v);break
    after={p:sha(ROOT/p) for p in originals};assert before==after
    report.update(sha256_before=before,sha256_after=after,external_calls={PROFILES[p][0]:sorted(o.calls) for p,o in oracles.items()},
        boundary='Original hit/death instructions with external clock/scheduler/audio/animation lookup stubs; no full DOS session')
    (OUT/'oracle-report.json').write_text(json.dumps(report,indent=2)+'\n')
    assert not sum(report[k]['mismatches'] for k in ['native','death'])
if __name__=='__main__':main()
