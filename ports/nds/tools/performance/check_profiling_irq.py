"""Actual SDK/wrapper instructions under explicit timer/IRQ-delay fixtures.

MMIO fixtures model counter progress during IRQ preemption. This is not a
reproduction of the user's DraStic execution or a cycle-accurate DS model.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_R0
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from check_items import ROOT, run
from map.compiler_cache import compiler_environment
from run_diag_emulator import elf_symbol


def main():
    target=Path(sys.argv[1]).resolve()
    elf=target/'dok_nds_gameplay.elf';data=elf.read_bytes()
    sdk,size=elf_symbol(elf,'cpuGetTiming');sdk &= ~1
    sections=[]
    table=struct.unpack_from('<I',data,32)[0];stride,count=struct.unpack_from('<HH',data,46)
    for i in range(count):
        s=struct.unpack_from('<10I',data,table+i*stride)
        if s[1]!=8 and s[2]&2 and s[5] and (0x01000000<=s[3]<0x01010000 or 0x02000000<=s[3]<0x02400000):
            sections.append((s[3],data[s[4]:s[4]+s[5]]))
    code=next(b[sdk-a:sdk-a+size] for a,b in sections if a<=sdk<a+len(b))
    assert size==60
    timer_index=struct.unpack_from('<I',code,48)[0]

    def execute(entry, initial_ime, preempt):
        cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB)
        for a,n in ((0x01000000,0x10000),(0x02000000,0x400000),(0x04000000,0x1000)):
            cpu.mem_map(a,n)
        for a,b in sections:cpu.mem_write(a,b)
        cpu.mem_write(timer_index,struct.pack('<I',0))
        cpu.mem_write(0x04000208,struct.pack('<I',initial_ime))
        ticks=0x1234fffe;reads=[];writes=[]
        def read(uc,access,at,n,value,ctx):
            nonlocal ticks
            if at not in (0x04000100,0x04000104):return
            ime=struct.unpack('<I',uc.mem_read(0x04000208,4))[0]
            # Two explicit IRQ preemptions: after first high, after second low.
            # Counter values remain coherent at each individual register read.
            if preempt and ime and len(reads)==2:ticks=0x1235f000
            if preempt and ime and len(reads)==3:ticks=0x12360000
            value=(ticks>>(16 if at==0x04000104 else 0))&0xffff
            uc.mem_write(at,struct.pack('<H',value));reads.append(dict(address=hex(at),counter=hex(ticks),ime=ime))
            ticks+=1
        def write(uc,access,at,n,value,ctx):
            if at==0x04000208:writes.append(value)
        cpu.hook_add(UC_HOOK_MEM_READ,read);cpu.hook_add(UC_HOOK_MEM_WRITE,write)
        cpu.reg_write(UC_ARM_REG_SP,0x023ff000);cpu.reg_write(UC_ARM_REG_LR,0x0100fff1)
        cpu.emu_start(entry|1,0x0100fff0,count=300)
        result=cpu.reg_read(UC_ARM_REG_R0)
        final_ime=struct.unpack('<I',cpu.mem_read(0x04000208,4))[0]
        assert len(reads)==4
        return dict(sample=hex(result),counter_after=hex(ticks),future_ticks=max(result-ticks,0),
                    final_ime=final_ime,reads=reads,ime_writes=writes)

    baseline=execute(sdk,1,True)
    assert baseline['future_ticks']>0
    # A subsequent ordinary sample after a short DMA gives unsigned underflow.
    after_dma=int(baseline['counter_after'],16)+0x1000
    false_duration=(after_dma-int(baseline['sample'],16))&0xffffffff
    assert false_duration>=560190
    normal=execute(sdk,1,False);assert normal['future_ticks']==0
    report=dict(passed=True,scope=__doc__,elf_sha256=hashlib.sha256(data).hexdigest(),
                sdk_address=hex(sdk),sdk_sha256=hashlib.sha256(code).hexdigest(),
                preempted_sdk=baseline,normal_sdk=normal,false_dma_duration=hex(false_duration))
    try:wrapper,_=elf_symbol(elf,'__wrap_cpuGetTiming')
    except ValueError:wrapper=None
    if wrapper:
        report['wrapped']={}
        for ime in (0,1):
            row=execute(wrapper,ime,True)
            assert row['future_ticks']==0 and row['final_ime']==ime
            assert row['ime_writes']==[0,ime]
            assert all(r['ime']==0 for r in row['reads'])
            report['wrapped'][str(ime)]=row
    out=ROOT/'build/nds/n23/profiling38-host';out.mkdir(parents=True,exist_ok=True)
    command=[Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe','cc','-std=c11','-O2',
             '-Wall','-Wextra','-Werror','-Iports/nds/tests/stubs/clock',
             'ports/nds/platform/time/profiling.c','ports/nds/tests/test_profiling_irq.c','-o',out/'test.exe']
    run(command,env=compiler_environment(ROOT))
    report['host']=json.loads(run([out/'test.exe']))
    (target/'profiling-irq.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
