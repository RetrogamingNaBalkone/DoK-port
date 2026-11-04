"""SP16 original check-death instructions and real 30-byte retry copy.

Audio/HUD and OG's position-pointer getter are controlled external boundaries.
Actual HP/lives branches, map stores, copy helpers and WORD wrap execute in x86.
"""
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp16-flow'
PROFILES={
    'ru':dict(entry=0x193e,retry=0x11bdb,copy=0x15cb3,world_global=0x266a,map=0x608a),
    'en':dict(entry=0x1efc,retry=0x1163f,copy=0x15cb3,world_global=0x271a,map=0x613a),
    'old-gold':dict(entry=0x15a2,retry=0x22c66,copy=0x11126,world_global=0x358a,map=0x6faa),
}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def signed(value):return value-65536 if value&32768 else value

class Oracle:
    def __init__(self,profile):
        self.profile=profile;self.settings=PROFILES[profile]
        folder=OUT/'ghidra'/profile;manifest=json.loads((folder/'manifest.json').read_text())
        self.cpu=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000)
        self.ranges=[(0x50000,0x54300 if profile=='old-gold' else 0x55000)]
        for name,address in [('objects',0x50000),('retry',self.settings['retry']),('copy',self.settings['copy'])]:
            raw=(folder/(name+'.bin')).read_bytes();assert sha(folder/(name+'.bin'))==manifest['bytes_sha256'][name]
            c.mem_write(address,raw)
            if name!='objects':self.ranges.append((address,address+len(raw)))
        c.hook_add(UC_HOOK_CODE,self.hook);self.calls=[]
    def hook(self,c,address,size,unused):
        if address==0x5fff0:c.emu_stop();return
        if any(start<=address<end for start,end in self.ranges):return
        self.calls.append(hex(address));sp=c.reg_read(R.UC_X86_REG_SP)
        ax,dx=0,0
        if self.profile=='old-gold' and address==0x21b01:ax,dx=0x1026,0x9000
        ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        c.reg_write(R.UC_X86_REG_AX,ax);c.reg_write(R.UC_X86_REG_DX,dx)
        c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
    def run(self,hp,lives,finish):
        c=self.cpu;s=self.settings;self.calls=[]
        c.mem_write(0x30000,bytes(0x10000));c.mem_write(0x90000,bytes(0x10000))
        struct_pack=lambda address,fmt,*values:c.mem_write(address,struct.pack(fmt,*values))
        struct_pack(0x30000+s['world_global'],'<I',0x90003000)
        struct_pack(0x300c6,'<H',finish)
        struct_pack(0x93000,'<H',3);struct_pack(0x93004,'<H',3)
        struct_pack(0x93738,'<h',480);struct_pack(0x93742,'<I',3)
        struct_pack(0x93734,'<I',0x90001000)
        spawn=struct.pack('<iiih',720,-300,720,90)+bytes(range(16))
        c.mem_write(0x93716,spawn)
        actor=bytearray([0x55]*0x200)
        struct.pack_into('<iiih',actor,0x26,240,-300,240,270)
        actor[0x5c]=ord('P');struct.pack_into('<h',actor,0x5e,hp);struct.pack_into('<h',actor,0x62,100);struct.pack_into('<h',actor,0xd6,lives)
        c.mem_write(0x91000,bytes(actor));c.mem_write(0x30000+s['map'],b'P'+b' '*8)
        c.mem_write(0x3f000,struct.pack('<HHI',0xfff0,0x5000,0x90001000))
        for reg,value in [(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_IP,s['entry']),(R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0),(R.UC_X86_REG_ES,0),(R.UC_X86_REG_EFLAGS,2)]:c.reg_write(reg,value)
        try:c.emu_start(0x50000+s['entry'],0,count=10000)
        except Exception as error:raise RuntimeError((self.profile,hp,lives,finish,hex(c.reg_read(R.UC_X86_REG_CS)),hex(c.reg_read(R.UC_X86_REG_IP)),self.calls)) from error
        assert c.reg_read(R.UC_X86_REG_IP)==0xfff0,(self.profile,hp,lives,finish,'Did not return')
        phase=c.reg_read(R.UC_X86_REG_AX)
        expected=3 if finish or hp<0 else 0 if hp>0 else 2 if lives==1 else 1
        assert phase==expected,(self.profile,hp,lives,finish,'phase',phase,expected)
        expected_lives=signed((lives-1)&65535) if hp==0 and not finish else lives
        assert struct.unpack('<h',c.mem_read(0x910d6,2))[0]==expected_lives
        assert struct.unpack('<h',c.mem_read(0x9105e,2))[0]==(100 if phase==1 else hp)
        if phase==1:
            assert bytes(c.mem_read(0x91026,30))==spawn,(self.profile,'Spawn30 copy mismatch')
            assert bytes(c.mem_read(0x30000+s['map'],9))==b' '*4+b'P'+b' '*4
        else:assert bytes(c.mem_read(0x30000+s['map'],9))==b'P'+b' '*8
        # Motion accelerations outside native copied range remain untouched.
        assert bytes(c.mem_read(0x91090,8))==bytes(actor[0x90:0x98])
        return dict(profile=self.profile,hp=hp,lives=lives,finish=finish,phase=phase,remaining_lives=expected_lives,calls=self.calls)

def main():
    rows=[];before={}
    for profile in PROFILES:
        manifest=json.loads((OUT/'ghidra'/profile/'manifest.json').read_text())
        before[manifest['sample']]=sha(ROOT/manifest['sample'])
        assert before[manifest['sample']]==manifest['sha256_before']==manifest['sha256_after']
    for profile in PROFILES:
        oracle=Oracle(profile)
        for hp in [-32768,-1,0,1,100,32767]:
            for lives in [-32768,-1,0,1,2,3,32767]:
                for finish in ([0] if profile=='old-gold' else [0,1]):rows.append(oracle.run(hp,lives,finish))
    after={name:sha(ROOT/name) for name in before};assert before==after
    report=dict(vectors=len(rows),mismatches=0,sha256_before=before,sha256_after=after,scope='Original HP/lives/map stores and real 30-byte retry copy. External audio/HUD and OG position getter stubbed.',vectors_detail=rows)
    (OUT/'native-flow-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(len(rows),'native flow vectors; 0 mismatches')
if __name__=='__main__':main()
