"""Independent ARM sampler/coverage/index vectors from the linked G20 ELF.

Unicorn checks arithmetic, ABI and original ROM animation metadata; mGBA
camera/game suites separately check the actual console and cycle timings.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys
sys.path.insert(0, 'C:/Source/wasdify/build/research-deps')
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn import arm_const as R
from gba_rom import elf_info
from verify_gba import sha
from probe_render_layout import frames


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=Path('build/g20'))
    p.add_argument('--report',type=Path,default=Path('analysis/g20-arm-vectors.json'))
    a=p.parse_args()
    elf=a.build_dir/'dok-diagnostic.elf'
    data=elf.read_bytes()
    _,symbols,_=elf_info(elf)
    cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for base,size in ((0x02000000,0x40000),(0x03000000,0x10000),(0x08000000,0x2000000)):
        cpu.mem_map(base,size)
    phoff=struct.unpack_from('<I',data,28)[0]
    phsize,phcount=struct.unpack_from('<HH',data,42)
    for i in range(phcount):
        kind,off,addr,physical,filesz,memsz,flags,align=struct.unpack_from('<8I',data,phoff+i*phsize)
        if kind==1 and filesz:cpu.mem_write(addr,data[off:off+filesz])
    registers=[getattr(R,f'UC_ARM_REG_R{i}') for i in range(4,12)]
    preserved=[0x5a000000+i*0x10101 for i in range(8)]
    def call(name,*args):
        for reg,value in zip((R.UC_ARM_REG_R0,R.UC_ARM_REG_R1,R.UC_ARM_REG_R2,R.UC_ARM_REG_R3),args):cpu.reg_write(reg,value)
        for reg,value in zip(registers,preserved):cpu.reg_write(reg,value)
        cpu.reg_write(R.UC_ARM_REG_SP,0x03007e00)
        cpu.reg_write(R.UC_ARM_REG_LR,0x03007ff0)
        cpu.emu_start(symbols[name],0x03007ff0,count=1000000)
        assert cpu.reg_read(R.UC_ARM_REG_PC)==0x03007ff0,name
        assert cpu.reg_read(R.UC_ARM_REG_SP)==0x03007e00,name
        assert [cpu.reg_read(r) for r in registers]==preserved,name
        return cpu.reg_read(R.UC_ARM_REG_R0)
    rng=random.Random(0x4720)
    cell_table=bytes(cpu.mem_read(symbols['dok_cell_480'],1920))
    assert all(cell_table[n>>4]==n//480 for n in range(30720))
    pixel_addr,lut_addr,frame_addr,bits_addr,desc=0x09fc0000,0x09fe0000,0x02000000,0x02010000,0x02012000
    summary=hashlib.sha256()
    for case in range(4096):
        width=rng.choice((1,2,7,18,31,64,127,256))
        height=rng.choice((1,2,17,32,128,256,512))
        pixels=rng.randbytes(width*height)
        lut=rng.randbytes(256)
        frame=bytearray(rng.randbytes(38400))
        bits=list(struct.unpack('<600I',rng.randbytes(2400)))
        x=rng.randrange(121)*2
        last=rng.randrange(x,240) if x<240 else 239
        if case%17==0 and x:last=x-1
        y=rng.randrange(144)
        tu=rng.randrange(-3,width+3)*65536+rng.randrange(65536)
        tv=rng.randrange(-3,height+3)*65536+rng.randrange(65536)
        du,dv=rng.randrange(-200000,200001),rng.randrange(-200000,200001)
        mask=1<<(y&31)
        payload=struct.pack('<8I4i2I',pixel_addr,lut_addr,frame_addr+y*240+x,width,height,x,last,y,tu,tv,du,dv,
                            bits_addr+(x//2*5+y//32)*4,mask)
        cpu.mem_write(pixel_addr,pixels);cpu.mem_write(lut_addr,lut)
        cpu.mem_write(frame_addr,bytes(frame));cpu.mem_write(bits_addr,struct.pack('<600I',*bits));cpu.mem_write(desc,payload)
        expected=0
        for col in range(x,last+1,2):
            offset=col//2*5+y//32
            if not bits[offset]&mask:
                tx=max(0,min(width-1,tu//65536));ty=max(0,min(height-1,tv//65536))
                color=lut[pixels[ty*width+tx]]
                frame[y*240+col:y*240+col+2]=bytes((color,color))
                bits[offset]|=mask;expected+=1
            tu+=du*2;tv+=dv*2
        actual=call('dok_gba_surface_span',desc)
        assert actual==expected,(case,actual,expected)
        assert bytes(cpu.mem_read(frame_addr,38400))==frame,case
        assert bytes(cpu.mem_read(bits_addr,2400))==struct.pack('<600I',*bits),case
        summary.update(frame)
    for case in range(2048):
        bits=list(struct.unpack('<5I',rng.randbytes(20)))
        if case%3==0:bits=[0 if case&1 else 0xffffffff]*5
        y=rng.randrange(145);invert=0xffffffff if case&2 else 0
        expected=next((n for n in range(y,144) if ((bits[n//32]^invert)>>(n&31))&1),144)
        cpu.mem_write(bits_addr,struct.pack('<5I',*bits))
        assert call('coverage_edge',bits_addr,y,invert)==expected,case
    # Derive the animation set independently from all original RES resources,
    # then use G03's manifest solely to locate their unchanged embedded copies.
    from verify_sprites_gba import original_animations, load_json
    manifest=load_json('build/gba/assets/manifest.json')
    decoded_count=0;resource_count=0
    for language in (0,1):
        originals,_=original_animations(language)
        for row in manifest['resources']:
            raw=originals.get(row['name_hex'])
            if raw is None:continue
            try:sequence=frames(raw)
            except (ValueError,AssertionError,struct.error):continue
            entry=row['languages']['en' if language else 'ru']
            pointer=symbols['dok_gba_assets']+entry['offset']
            assert bytes(cpu.mem_read(pointer,entry['size']))==raw
            resource_count+=1
            assert call('dok_animation_open',pointer,len(raw),desc)==0
            view=bytes(cpu.mem_read(desc,16))
            assert struct.unpack('<IIIH',view[:14])==(pointer,len(raw),8+10*len(sequence),len(sequence))
            for index,(metadata,pixels) in enumerate(sequence):
                assert call('dok_animation_frame_at',desc,index,desc+64)==0
                actual=bytes(cpu.mem_read(desc+64,40))
                assert struct.unpack_from('<HHhhh',actual)==(index,metadata['interval_bits'],metadata['interval_ticks'],metadata['parameter_a'],metadata['parameter_b'])
                offset=struct.unpack_from('<I',actual,12)[0]
                pixel_ptr=struct.unpack_from('<I',actual,16)[0]
                assert offset==metadata['image_offset'] and pixel_ptr==pointer+offset+18
                assert bytes(cpu.mem_read(pixel_ptr,len(pixels)))==pixels
                decoded_count+=1
            cpu.mem_write(desc+64,b'\xa5'*40)
            assert call('dok_animation_frame_at',desc,len(sequence),desc+64)==5
            assert bytes(cpu.mem_read(desc+64,40))==b'\xa5'*40
            # A mutable RAM copy must be fully validated, including truncation.
            ram=0x02018000
            if len(raw)<0x10000:
                cpu.mem_write(ram,raw)
                assert call('dok_animation_open',ram,len(raw),desc)==0
                cpu.mem_write(desc,b'\xa5'*16)
                assert call('dok_animation_open',ram,len(raw)-1,desc)==2
                assert bytes(cpu.mem_read(desc,16))==b'\xa5'*16
    result=dict(task='G20',status='passed',elf_sha256=sha(elf),rom_sha256=sha(a.build_dir/'dok-diagnostic.gba'),
                script_sha256=sha(__file__),sampler_vectors=4096,coverage_edge_vectors=2048,
                exact_cell_coordinate_vectors=30720,
                sampler_digest=summary.hexdigest(),original_animation_resources=resource_count,
                original_animation_frames=decoded_count,callee_saved_registers_and_stack_checked=True,
                mutable_buffer_truncation_and_unchanged_error_output=True,
                timing_measured=False,scope='ARM/Thumb execution in Unicorn; hardware timing checked separately in mGBA')
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':main()
