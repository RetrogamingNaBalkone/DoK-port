"""ARM instruction bound for early G27 admission; not a GBA cycle estimate."""
import json, os, struct, subprocess, zlib
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import arm_const as R
from check_g27 import ROOT,OUT
from verify_gba import sha

def main():
    dest=ROOT/'build/gba/g27/admission';dest.mkdir(parents=True,exist_ok=True)
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    from build_cache import compiler_environment
    os.environ.update(compiler_environment(ROOT))
    source=ROOT/'ports/gba/scripts/g27_admission.c'
    linker=dest/'probe.ld';linker.write_text('ENTRY(g27_admit)\nSECTIONS { . = 0x03000000; .text : { *(.text.entry) *(.text*) } /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes*) *(.comment*) } }\n')
    commands=[[str(zig),'cc','-target','arm-freestanding-eabi','-mcpu=arm7tdmi','-marm','-O2','-ffreestanding','-fno-stack-protector','-c',str(source),'-o',str(dest/'probe.o')],
        [str(zig),'ld.lld','-T',str(linker),str(dest/'probe.o'),'-o',str(dest/'probe.elf')],
        [str(zig),'objcopy','-O','binary',str(dest/'probe.elf'),str(dest/'probe.bin')]]
    for cmd in commands:subprocess.run(cmd,check=True,capture_output=True)
    captures=json.loads(zlib.decompress((OUT/'captures.json.zlib').read_bytes()))
    packing=json.loads((OUT/'packing.json').read_text());rows=[]
    for capture,packed in zip(captures['rows'],packing['rows']):
        cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM);cpu.mem_map(0x03000000,32768);cpu.mem_map(0x02000000,262144)
        cpu.mem_write(0x03000000,(dest/'probe.bin').read_bytes())
        faces=b''.join(struct.pack('<11i2H',*f['values']) for f in capture['faces'])
        classes=bytes(f['object_class']&255 for f in capture['faces'])
        if faces:cpu.mem_write(0x02000000,faces);cpu.mem_write(0x02010000,classes)
        cpu.reg_write(R.UC_ARM_REG_CPSR,0x1f)
        for reg,value in ((R.UC_ARM_REG_R0,0x02000000),(R.UC_ARM_REG_R1,0x02010000),(R.UC_ARM_REG_R2,len(classes)),(R.UC_ARM_REG_R3,0x02020000),
                          (R.UC_ARM_REG_SP,0x03007f00),(R.UC_ARM_REG_LR,0x03007ff0)):cpu.reg_write(reg,value)
        instructions=[0]
        def count(*args):instructions[0]+=1
        cpu.hook_add(UC_HOOK_CODE,count);cpu.emu_start(0x03000000,0x03007ff0,count=100000)
        assert cpu.reg_read(R.UC_ARM_REG_PC)==0x03007ff0
        accept=bool(cpu.reg_read(R.UC_ARM_REG_R0));assert accept==packed['early_accept']
        visits,total=struct.unpack('<2I',cpu.mem_read(0x02020000,8))
        rows.append(dict(name=capture['name'],language=capture['language'],accept=accept,visited_faces=visits,
            estimated_objects=total,arm_instructions=instructions[0],early_gate_texture_reads=0,
            early_gate_atlas_bytes=0,late_reject_sample_reads=packed['open_samples'] if packed['early_false_accept'] else 0,
            late_reject_atlas_bytes=packed['separate']['raw_atlas_bytes'] if packed['early_false_accept'] else 0,
            false_accept=packed['early_false_accept'],false_reject=packed['early_false_reject']))
    result=dict(status='passed',source_sha256=sha(source),binary_sha256=sha(dest/'probe.bin'),binary_bytes=(dest/'probe.bin').stat().st_size,
        commands=commands,rows=rows,scope='Standalone ARM7TDMI instruction execution in Unicorn. No waitstates/IRQs: instructions are not cycles. Production projected faces are prerequisites; projector cost not removed.')
    (OUT/'admission.json').write_text(json.dumps(result,indent=2)+'\n')
    print('admission',len(rows),'cases',min(r['arm_instructions'] for r in rows),max(r['arm_instructions'] for r in rows),'instructions')
if __name__=='__main__':main()
