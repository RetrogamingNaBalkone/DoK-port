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
    manifest=a.build_dir.parent/'prototype-build.json'
    defines=json.loads(manifest.read_text())['variants'][a.build_dir.name].get('prototype_defines',[]) if manifest.exists() else []
    scene_rows=96 if 'HW_ROWS96' in defines else 72 if 'HW_ROWS72' in defines else 128 if 'HW_VERTICAL128' in defines else 144
    cover_words=4 if 'HW_COVER4' in defines else 5
    cover_count=120*cover_words;cover_bytes=cover_count*4;cover_format='<'+str(cover_count)+'I'
    surface_prelit='HW_SURFACE_PRELIT' in defines
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
        if len(args)>4:cpu.mem_write(0x03007e00,struct.pack('<'+'I'*(len(args)-4),*args[4:]))
        cpu.reg_write(R.UC_ARM_REG_LR,0x03007ff0)
        cpu.emu_start(symbols[name],0x03007ff0,count=1000000)
        assert cpu.reg_read(R.UC_ARM_REG_PC)==0x03007ff0,name
        assert cpu.reg_read(R.UC_ARM_REG_SP)==0x03007e00,name
        assert [cpu.reg_read(r) for r in registers]==preserved,name
        return cpu.reg_read(R.UC_ARM_REG_R0)
    rng=random.Random(0x4720)
    projection_vectors=0
    if 'HW_PROJECT_RECIP' in defines:
        def signed32(n):return (n+0x80000000)%0x100000000-0x80000000
        for divisor in list(range(-8,16386))+[32767,65535,2147483647,-2147483648]:
            for case in range(2):
                distance=rng.randrange(-1000,1001);scale=rng.randrange(-32768,32768);focal=rng.randrange(-32768,32768)
                cx,cy=rng.randrange(-32768,32768),rng.randrange(-32768,32768)
                lateral,vertical=rng.randrange(-0x80000000,0x80000000),rng.randrange(-0x80000000,0x80000000)
                camera=struct.pack('<6i4h',0,0,0,0,32768,distance,scale,focal,cx,cy)
                point=struct.pack('<3i',lateral,signed32(divisor-distance),vertical)
                n=signed32(scale*distance*focal)
                cpu.mem_write(0x02012000,camera);cpu.mem_write(0x02012040,point);cpu.mem_write(0x02012080,b'\xa5'*12)
                valid=divisor!=0 and not(n==-2147483648 and divisor==-1)
                assert call('dok_projection_project',0x02012000,0x02012040,0x02012080)==int(valid),(divisor,case)
                if valid:
                    quotient=abs(n)//abs(divisor)*(-1 if (n<0)!=(divisor<0) else 1)
                    expected=(signed32((signed32(lateral*quotient)>>15)+cx),signed32((signed32(vertical*quotient)>>15)+cy),quotient)
                    assert bytes(cpu.mem_read(0x02012080,12))==struct.pack('<3i',*expected),(divisor,case,expected)
                else:assert bytes(cpu.mem_read(0x02012080,12))==b'\xa5'*12
                projection_vectors+=1
        for n in (-2147483648,2147483647,0):
            for divisor in (-1,0,1,16384,16385):
                cpu.mem_write(0x02012000,struct.pack('<6i4h',0,0,0,0,32768,n,1,1,0,0))
                cpu.mem_write(0x02012040,struct.pack('<3i',0,signed32(divisor-n),0))
                cpu.mem_write(0x02012080,b'\xa5'*12)
                valid=divisor!=0 and not(n==-2147483648 and divisor==-1)
                assert call('dok_projection_project',0x02012000,0x02012040,0x02012080)==int(valid)
                if valid:
                    q=abs(n)//abs(divisor)*(-1 if (n<0)!=(divisor<0) else 1)
                    assert bytes(cpu.mem_read(0x02012080,12))==struct.pack('<3i',0,0,q)
                else:assert bytes(cpu.mem_read(0x02012080,12))==b'\xa5'*12
                projection_vectors+=1
    unsigned_division_vectors=0
    positive_division_vectors=0
    if 'dok_gba_divide_positive32' in symbols:
        divisors=set(range(1,65537))
        for shift in range(16,31):
            divisors.update(((1<<shift)-1,1<<shift,(1<<shift)+1))
        divisors.update(rng.randrange(1,0x80000000) for _ in range(20000))
        for divisor in sorted(divisors):
            for numerator in (0,0x7fffffff,rng.randrange(0x80000000)):
                assert call('dok_gba_divide_positive32',numerator,divisor)==numerator//divisor,(numerator,divisor)
                positive_division_vectors+=1
    if 'dok_gba_divide_u16' in symbols:
        for divisor in range(1,65536):
            for numerator in (0,0xffffffff,rng.randrange(0x100000000)):
                assert call('dok_gba_divide_u16',numerator,divisor)==numerator//divisor,(numerator,divisor)
                unsigned_division_vectors+=1
    face_box_vectors=0
    if 'face_box_covered' in symbols:
        address=0x02012000
        for case in range(1024):
            first=rng.randrange(121)*2;last=rng.randrange(first,240) if first<240 else 239
            top0,top1=[rng.randrange(-200,scene_rows+200) for _ in range(2)]
            bottom0,bottom1=[rng.randrange(-200,scene_rows+200) for _ in range(2)]
            top=min(top0,top1);bottom=max(bottom0,bottom1)
            words=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
            if case%3==0:words=[0xffffffff if case&1 else 0]*cover_count
            outside=bottom<0 or top>=scene_rows or bottom<top
            expected=outside or all(words[(x//2)*cover_words+y//32]&(1<<(y&31))
                for x in range(first,last+1,2) for y in range(max(top,0),min(bottom,scene_rows-1)+1))
            raw=struct.pack(cover_format,*words)
            cpu.mem_write(symbols['coverage_bits'],raw)
            face=struct.pack('<11iHh',first,last,top0,top1,bottom0,bottom1,100,100,0,31,100,1,0)
            cpu.mem_write(address-4,b'HEAD'+face+b'TAIL')
            assert call('face_box_covered',address,first,last)==int(expected),case
            assert bytes(cpu.mem_read(address-4,len(face)+8))==b'HEAD'+face+b'TAIL'
            assert bytes(cpu.mem_read(symbols['coverage_bits'],cover_bytes))==raw
            face_box_vectors+=1
    plane_division_vectors=0
    if 'dok_gba_plane_divide' in symbols:
        # Every supported divisor, both signs, zero, and the full signed range.
        for divisor in range(1,16385):
            for numerator in (0,-2147483648,2147483647,rng.randrange(-2147483648,2147483648)):
                expected=(abs(numerator)//divisor)*(-1 if numerator<0 else 1)
                assert call('dok_gba_plane_divide',numerator&0xffffffff,divisor)==expected&0xffffffff,(numerator,divisor)
                plane_division_vectors+=1
    cell_table=bytes(cpu.mem_read(symbols['dok_cell_480'],1920))
    assert all(cell_table[n>>4]==n//480 for n in range(30720))
    pixel_addr,lut_addr,frame_addr,bits_addr,desc=0x09fc0000,0x09fe0000,0x02000000,0x02010000,0x02012000
    empty_column_vectors=0
    if 'hw_empty_column_count' in symbols:
        for case in range(1024):
            words=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
            if case%3==0:words=[0 if case&1 else 0xffffffff]*cover_count
            x=rng.randrange(120)*2;first=rng.randrange(scene_rows);last=rng.randrange(first,scene_rows)
            expected=sum(not(words[x//2*cover_words+y//32]&(1<<(y&31))) for y in range(first,last+1))
            raw=struct.pack(cover_format,*words);cpu.mem_write(symbols['coverage_bits'],raw)
            assert call('hw_empty_column_count',x,first,last)==expected,(case,x,first,last)
            assert bytes(cpu.mem_read(symbols['coverage_bits'],cover_bytes))==raw
            empty_column_vectors+=1
    coverage_span_vectors=0
    if 'dok_gba_coverage_span' in symbols:
        for case in range(1024):
            original=rng.randbytes(1920);expected=bytearray(original)
            first=rng.randrange(120);count=rng.randrange(1,121-first);word=rng.randrange(4);mask=1<<rng.randrange(32)
            changed=0
            for x in range(first,first+count):
                at=x*16+word*4;value=struct.unpack_from('<I',original,at)[0]
                changed+=int(not(value&mask));struct.pack_into('<I',expected,at,value|mask)
            cpu.mem_write(desc-4,b'HEAD'+original+b'TAIL')
            assert call('dok_gba_coverage_span',desc+first*16+word*4,mask,count)==changed
            assert bytes(cpu.mem_read(desc-4,1928))==b'HEAD'+expected+b'TAIL'
            coverage_span_vectors+=1
    rectangle_vectors=0
    if 'raster_rect' in symbols:
        # Synthetic post-LIGHT columns isolate the rectangle algorithm from
        # asset generation. The oracle evaluates the original Q7 fraction,
        # coverage, source-zero alpha, clipping and tiled byte addresses.
        bank=symbols['dok_prelit_bank'];table=0x09f00000;offset=512
        cpu.mem_write(table,struct.pack('<32I',*([0]*16+[offset]*16)))
        cpu.mem_write(symbols['prelit_view'],struct.pack('<I',table))
        cpu.mem_write(symbols['g_hw_tile_active'],struct.pack('<I',1))
        cpu.mem_write(symbols['g_hw_tile_black'],struct.pack('<I',223))
        cpu.mem_write(symbols['hw_tile_frame'],struct.pack('<I',frame_addr+4))
        for case in range(512):
            width=rng.choice((1,17,64,96,192,256));height=rng.choice((1,17,96,192,512))
            colors=bytearray(rng.randbytes(width*height))
            for n in range(case%7,len(colors),13):colors[n]=0
            prefix=bytes(int(0 not in colors[x*height:(x+1)*height]) for x in range(width))+bytes(256-width)
            if 'HW_ALPHA_EMPTY' in defines:
                for x in range(case%5,width,7):colors[x*height:(x+1)*height]=bytes(height)
                prefix=bytes(int(0 not in colors[x*height:(x+1)*height]) for x in range(width))+bytes(256-width)
                empty=bytes(int(not any(colors[x*height:(x+1)*height])) for x in range(width))+bytes(256-width)
                cpu.mem_write(bank+offset-512,empty)
            cpu.mem_write(bank+offset-256,prefix+bytes(colors))
            x0=rng.randrange(-128,230);x1=rng.randrange(max(x0+1,0),641)
            top=rng.randrange(-256,scene_rows+64);bottom=top+rng.randrange(513)
            depth=rng.choice((0,1,1920,2040,3720,32767,32768,65535))
            u0,u1=rng.randrange(width),rng.randrange(width);v0=rng.randrange(-4,height+4);masked=case%2*8
            face=struct.pack('<11iHh',x0,x1,top,top,bottom,bottom,depth,depth,u0,u1,0,0,v0)
            image=struct.pack('<3I4H',bank+offset,bank+offset,0,width,height,masked,1)
            initial=rng.randbytes(38400);expected=bytearray(initial)
            bits=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
            if case%4==0:bits=[0]*cover_count
            if case%4==1:bits=[0xffffffff]*cover_count
            cpu.mem_write(symbols['coverage_bits'],struct.pack(cover_format,*bits))
            cpu.mem_write(symbols['coverage_count'],bytes(128 if any(bits[x*cover_words:(x+1)*cover_words]) else 0 for x in range(120)))
            written=transparent=0
            if bottom>=0 and top<scene_rows:
                begin=max(0,top);end=min(scene_rows-1,bottom);start=max(0,min(height-1,v0))
                step=((height-1-start)<<16)//(bottom-top) if bottom>top else 0
                q=max(1,(6553600//(depth+200))>>8);span=x1-x0
                for x in range((max(0,x0)+1)&~1,min(239,x1)+1,2):
                    i=x-x0;u=min(width-1,(u0*q*(span-i)+u1*q*i)//(q*span))
                    for y in range(begin,end+1):
                        b=x//2*cover_words+y//32;mask=1<<(y%32)
                        if bits[b]&mask:continue
                        color=colors[u*height+(((start<<16)+(y-top)*step)>>16)]
                        if masked and not color:transparent+=1;continue
                        at=(y//8)*1024+(x//16)*64+(y%8)*8+(x//2)%8
                        expected[at]=color or 223;bits[b]|=mask;written+=1
            cpu.mem_write(frame_addr,b'HEAD'+initial+b'TAIL')
            cpu.mem_write(desc,face);cpu.mem_write(desc+64,image);cpu.mem_write(desc+96,bytes(44))
            assert call('raster_rect',frame_addr,desc,desc+64,desc+96)==1,('rect handled',case)
            actual_frame=bytes(cpu.mem_read(frame_addr+4,38400))
            assert actual_frame==expected,('rect frame',case,(width,height,x0,x1,top,bottom,depth,u0,u1,v0,masked),[(i,a,expected[i]) for i,a in enumerate(actual_frame) if a!=expected[i]][:8])
            assert bytes(cpu.mem_read(symbols['coverage_bits'],cover_bytes))==struct.pack(cover_format,*bits),('rect coverage',case)
            stats=struct.unpack('<10I2H',bytes(cpu.mem_read(desc+96,44)))
            assert stats[4]==int(written>0) and stats[7]==written and stats[8]==transparent,('rect stats',case,stats,written,transparent)
            assert bytes(cpu.mem_read(frame_addr,4))==b'HEAD' and bytes(cpu.mem_read(frame_addr+38404,4))==b'TAIL'
            rectangle_vectors+=1
    geometry_copy_vectors=0;geometry_equal_vectors=0
    if 'dok_gba_geometry_copy' in symbols:
        for case in range(1024):
            count=rng.randrange(2049)*4
            source=0x02018000+(case%8)*4;destination=0x0201c000+(case%4)*4
            payload=rng.randbytes(count);initial=rng.randbytes(count)
            cpu.mem_write(source-4,b'HEAD'+payload+b'TAIL')
            cpu.mem_write(destination-4,b'FRNT'+initial+b'BACK')
            call('dok_gba_geometry_copy',destination,source,count)
            assert bytes(cpu.mem_read(source-4,count+8))==b'HEAD'+payload+b'TAIL'
            assert bytes(cpu.mem_read(destination-4,count+8))==b'FRNT'+payload+b'BACK'
            assert call('dok_gba_geometry_equal',destination,source,count)==1
            geometry_copy_vectors+=1;geometry_equal_vectors+=1
            if count:
                at=rng.randrange(count);changed=bytes([payload[at]^255]);cpu.mem_write(destination+at,changed)
                assert call('dok_gba_geometry_equal',destination,source,count)==0
                geometry_equal_vectors+=1
    hw_pack_vectors=0
    if 'dok_gba_hw_compact' in symbols:
        hw_frame=frame_addr+4
        for case in range(256):
            black=rng.randrange(1,256);frame=bytearray(rng.randbytes(38400))
            for i in range(case%9,34560,17):frame[i]=0
            original=bytes(frame);expected=bytearray(frame)
            compact=bytearray(128*144)
            for y in range(144):
                for x in range(120):compact[y*128+x]=original[y*240+x*2] or black
            expected[:len(compact)]=compact
            cpu.mem_write(hw_frame-4,b'GUAR');cpu.mem_write(hw_frame,bytes(frame));cpu.mem_write(hw_frame+38400,b'END!')
            call('dok_gba_hw_compact',hw_frame,black)
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,('compact',case)
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'GUAR' and bytes(cpu.mem_read(hw_frame+38400,4))==b'END!'
            tiled=bytes(compact[(ty*8+y)*128+tx*8+x] for ty in range(18) for tx in range(16) for y in range(8) for x in range(8))
            expected[:len(tiled)]=tiled
            call('dok_gba_hw_tiles',hw_frame,bits_addr)
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,('tiles',case)
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'GUAR' and bytes(cpu.mem_read(hw_frame+38400,4))==b'END!'
            pitch=rng.choice((32,48,240));source=bytearray(rng.randbytes(pitch*8))
            for i in range(0,len(source),7):source[i]=0
            cpu.mem_write(pixel_addr,bytes(source));cpu.mem_write(desc-4,b'HEAD'+b'\xa5'*64+b'TAIL')
            call('dok_gba_hw_ui_tile',desc,pixel_addr,pitch,black)
            assert bytes(cpu.mem_read(desc,64))==bytes(source[y*pitch+x] or black for y in range(8) for x in range(8)),('ui',case)
            assert bytes(cpu.mem_read(desc-4,4))==b'HEAD' and bytes(cpu.mem_read(desc+64,4))==b'TAIL'
            hw_pack_vectors+=3
    summary=hashlib.sha256()
    tiled_vectors=0;map_vectors=0;masked_tiled_vectors=0;gap_tiled_vectors=0;nonzero_column_vectors=0;cached_column_vectors=0
    if 'dok_gba_hw_map_cells' in symbols:
        cpu.mem_map(0x06000000,0x18000)
        for case in range(256):
            video=bytearray(rng.randbytes(0x18000));expected=bytearray(video);commands=bytearray()
            count=rng.randrange(1,257)
            for _ in range(count):
                x,z=rng.randrange(16),rng.randrange(16);offset=z*256+x*4
                first=[0 if rng.randrange(4)==0 else 0x03020100+rng.randrange(1,240)*0x01010101 for _ in range(6)]
                commands.extend(struct.pack('<7I',offset,*first))
                for band,value in enumerate(first):
                    for row in range(4):
                        struct.pack_into('<I',expected,0x8000+band*4096+offset+row*64,value+row*0x04040404 if value else 0)
            cpu.mem_write(desc-4,b'HEAD'+bytes(commands)+b'TAIL');cpu.mem_write(0x06000000,bytes(video))
            call('dok_gba_hw_map_cells',desc,count)
            assert bytes(cpu.mem_read(0x06000000,0x18000))==expected,('hardware map',case)
            assert bytes(cpu.mem_read(desc-4,len(commands)+8))==b'HEAD'+commands+b'TAIL'
            map_vectors+=1
    for name,masked in (('dok_gba_tiled_masked_column',True),('dok_gba_tiled_opaque_gaps',False),('dok_gba_tiled_opaque_batch',False)):
        if name not in symbols:continue
        tile_at=lambda x,y:(y//8)*1024+(x//16)*64+(y%8)*8+(x//2)%8
        for case in range(512):
            hw_frame=frame_addr+4;frame=bytearray(rng.randbytes(38400));expected=bytearray(frame)
            bits=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
            if case%4==0:bits=[0]*cover_count
            if case%4==1:bits=[0xffffffff]*cover_count
            original_bits=struct.pack(cover_format,*bits)
            x=rng.randrange(120)*2;y=rng.randrange(scene_rows);count=rng.randrange(1,scene_rows+1-y)
            height=rng.choice((1,17,96,192,512));pixels=bytearray(rng.randbytes(height));black=rng.randrange(1,256)
            for k in range(case%7,height,11):pixels[k]=0
            if name=='dok_gba_tiled_opaque_batch' and case%3==0:
                black=0;pixels=bytearray(p or 1 for p in pixels)
            v=rng.randrange(height*65536);step=rng.randrange((height*65536-1-v)//max(1,count-1)+1)
            written=available=0
            for n in range(count):
                at=x//2*cover_words+(y+n)//32;mask=1<<((y+n)%32)
                if bits[at]&mask:continue
                available+=1;color=pixels[(v+n*step)>>16]
                if masked and not color:continue
                expected[tile_at(x,y+n)]=color or black;bits[at]|=mask;written+=1
            cpu.mem_write(hw_frame-4,b'HEAD');cpu.mem_write(hw_frame,bytes(frame));cpu.mem_write(hw_frame+38400,b'TAIL')
            cpu.mem_write(pixel_addr,bytes(pixels));cpu.mem_write(bits_addr,original_bits)
            dest=hw_frame+tile_at(x,y);bitptr=bits_addr+(x//2*cover_words+y//32)*4
            if masked:
                cpu.mem_write(desc,struct.pack('<8I',dest,pixel_addr,v,step,count,bitptr,1<<(y%32),y%8))
                actual=call(name,desc);wanted=written|((available-written)<<16);masked_tiled_vectors+=1
            else:
                if name=='dok_gba_tiled_opaque_batch':
                    cpu.mem_write(desc,struct.pack('<9I',dest,pixel_addr,v,step,count,bitptr,1<<(y%32),y%8,black))
                    actual=call(name,desc)
                else:actual=call(name,dest,pixel_addr,v,step,count,y%8,black,bitptr,y%32)
                wanted=written;gap_tiled_vectors+=1
            assert actual==wanted,(name,case,actual,wanted)
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,(name,case,'frame')
            assert bytes(cpu.mem_read(bits_addr,cover_bytes))==struct.pack(cover_format,*bits),(name,case,'coverage')
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'HEAD' and bytes(cpu.mem_read(hw_frame+38400,4))==b'TAIL'
    if 'dok_gba_tiled_column' in symbols:
        hw_frame=frame_addr+4
        tile_at=lambda x,y:(y//8)*1024+(x//16)*64+(y%8)*8+(x//2)%8
        for case in range(512):
            frame=bytearray(rng.randbytes(38400));expected=bytearray(frame)
            black=rng.randrange(1,256);height=rng.choice((1,17,96,192,512));pixels=bytearray(rng.randbytes(height))
            for i in range(case%7,height,11):pixels[i]=0
            if 'HW_NONZERO_COLUMN' in defines and case%2==0:
                black=0;pixels=bytearray(p or 1 for p in pixels);nonzero_column_vectors+=1
            x=rng.randrange(120)*2;y=rng.randrange(scene_rows);count=rng.randrange(1,scene_rows+1-y)
            v=rng.randrange(height*65536);step=rng.randrange((height*65536-1-v)//max(1,count-1)+1)
            for k in range(count):expected[tile_at(x,y+k)]=pixels[(v+k*step)>>16] or black
            source_addr=pixel_addr
            if 'HW_ROM_COLUMN_CACHE' in defines:
                source_addr=(0x02018000 if case%4==0 else pixel_addr)+case%16
            cpu.mem_write(hw_frame-4,b'HEAD');cpu.mem_write(hw_frame,bytes(frame));cpu.mem_write(hw_frame+38400,b'TAIL');cpu.mem_write(source_addr,bytes(pixels))
            name='dok_gba_tiled_column_cached' if 'HW_ROM_COLUMN_CACHE' in defines else 'dok_gba_tiled_column'
            cpu.mem_write(0x03007cb4,b'STK!')
            assert call(name,hw_frame+tile_at(x,y),source_addr,v,step,count,y%8,black)==count,case
            assert bytes(cpu.mem_read(0x03007cb4,4))==b'STK!'
            assert bytes(cpu.mem_read(0x03007e00,12))==struct.pack('<3I',count,y%8,black)
            assert bytes(cpu.mem_read(source_addr,height))==bytes(pixels)
            if name=='dok_gba_tiled_column_cached':cached_column_vectors+=1
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,('tiled column',case)
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'HEAD' and bytes(cpu.mem_read(hw_frame+38400,4))==b'TAIL'
            tiled_vectors+=1
        for case in range(1024 if 'dok_gba_tiled_surface_pair' in symbols else 512):
            width=rng.choice((1,7,48,96,256));height=rng.choice((1,17,96,192,512))
            pixels=rng.randbytes(width*height);lut=rng.randbytes(256);black=rng.randrange(1,256)
            frame=bytearray(rng.randbytes(38400));expected=bytearray(frame)
            bits=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)));x=rng.randrange(121)*2
            last=rng.randrange(x,240) if x<240 else 239;y=rng.randrange(scene_rows)
            pair=case>=512
            if pair:y&=~1
            if case%17==0 and x:last=x-1
            tu=rng.randrange(-3,width+3)*65536;tv=rng.randrange(-3,height+3)*65536
            du,dv=rng.randrange(-200000,200001),rng.randrange(-200000,200001);mask=1<<(y%32)
            if pair:mask|=mask<<1
            payload=struct.pack('<8I4i3I',pixel_addr,lut_addr,hw_frame+tile_at(x,y),width,height,x,last,y,tu,tv,du,dv,
                bits_addr+(x//2*cover_words+y//32)*4,mask,black)
            cpu.mem_write(hw_frame-4,b'HEAD');cpu.mem_write(hw_frame,bytes(frame));cpu.mem_write(hw_frame+38400,b'TAIL')
            cpu.mem_write(pixel_addr,pixels);cpu.mem_write(lut_addr,lut);cpu.mem_write(bits_addr,struct.pack(cover_format,*bits));cpu.mem_write(desc,payload)
            if surface_prelit:cpu.mem_write(lut_addr,pixels)
            written=0
            for col in range(x,last+1,2):
                offset=col//2*cover_words+y//32
                if bits[offset]&mask!=mask:
                    tx=max(0,min(width-1,tu//65536));ty=max(0,min(height-1,tv//65536))
                    for delta in range(2 if pair else 1):
                        bit=1<<((y+delta)%32)
                        if not bits[offset]&bit:expected[tile_at(col,y+delta)]=(pixels[ty*width+tx] if surface_prelit else lut[pixels[ty*width+tx]]) or black
                    bits[offset]|=mask;written+=1
                tu+=du*2;tv+=dv*2
            assert call('dok_gba_tiled_surface',desc)==written,case
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,('tiled surface',case)
            assert bytes(cpu.mem_read(bits_addr,cover_bytes))==struct.pack(cover_format,*bits)
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'HEAD' and bytes(cpu.mem_read(hw_frame+38400,4))==b'TAIL'
            tiled_vectors+=1
        for case in range(256):
            frame=bytearray(rng.randbytes(38400));expected=bytearray(frame);pitch=rng.choice((6,8,9,30))*64
            columns=rng.randrange(1,pitch//64+1);x=rng.randrange(pitch//64-columns+1)*64
            source=rng.randbytes(columns*128)
            for row in range(2):expected[x+row*pitch:x+row*pitch+columns*64]=source[row*columns*64:(row+1)*columns*64]
            cpu.mem_write(hw_frame-4,b'HEAD');cpu.mem_write(hw_frame,bytes(frame));cpu.mem_write(hw_frame+38400,b'TAIL');cpu.mem_write(pixel_addr,source)
            call('dok_gba_tiled_ui_panel',hw_frame+x,pixel_addr,columns,pitch)
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,('tiled panel',case)
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'HEAD' and bytes(cpu.mem_read(hw_frame+38400,4))==b'TAIL'
            tiled_vectors+=1
        for case in range(256):
            frame=bytearray(rng.randbytes(38400));count=rng.randrange(1,213)*32;expected=bytearray(frame);expected[:count]=bytes(count)
            cpu.mem_write(hw_frame-4,b'HEAD');cpu.mem_write(hw_frame,bytes(frame));cpu.mem_write(hw_frame+38400,b'TAIL')
            call('dok_gba_tiled_clear',hw_frame,count)
            assert bytes(cpu.mem_read(hw_frame,38400))==expected,('tiled clear',case)
            assert bytes(cpu.mem_read(hw_frame-4,4))==b'HEAD' and bytes(cpu.mem_read(hw_frame+38400,4))==b'TAIL'
            tiled_vectors+=1
    for case in range(4096):
        width=rng.choice((1,2,7,18,31,64,127,256))
        height=rng.choice((1,2,17,32,128,256,512))
        pixels=rng.randbytes(width*height)
        lut=rng.randbytes(256)
        frame=bytearray(rng.randbytes(38400))
        bits=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
        x=rng.randrange(121)*2
        last=rng.randrange(x,240) if x<240 else 239
        if case%17==0 and x:last=x-1
        y=rng.randrange(scene_rows)
        tu=rng.randrange(-3,width+3)*65536+rng.randrange(65536)
        tv=rng.randrange(-3,height+3)*65536+rng.randrange(65536)
        du,dv=rng.randrange(-200000,200001),rng.randrange(-200000,200001)
        mask=1<<(y&31)
        payload=struct.pack('<8I4i2I',pixel_addr,lut_addr,frame_addr+y*240+x,width,height,x,last,y,tu,tv,du,dv,
                            bits_addr+(x//2*cover_words+y//32)*4,mask)
        cpu.mem_write(pixel_addr,pixels);cpu.mem_write(lut_addr,lut)
        cpu.mem_write(frame_addr,bytes(frame));cpu.mem_write(bits_addr,struct.pack(cover_format,*bits));cpu.mem_write(desc,payload)
        expected=0
        for col in range(x,last+1,2):
            offset=col//2*cover_words+y//32
            if not bits[offset]&mask:
                tx=max(0,min(width-1,tu//65536));ty=max(0,min(height-1,tv//65536))
                color=lut[pixels[ty*width+tx]]
                frame[y*240+col:y*240+col+2]=bytes((color,color))
                bits[offset]|=mask;expected+=1
            tu+=du*2;tv+=dv*2
        actual=call('dok_gba_surface_span',desc)
        assert actual==expected,(case,actual,expected)
        assert bytes(cpu.mem_read(frame_addr,38400))==frame,case
        assert bytes(cpu.mem_read(bits_addr,cover_bytes))==struct.pack(cover_format,*bits),case
        summary.update(frame)
    for case in range(2048):
        bits=list(struct.unpack('<5I',rng.randbytes(20)))
        if case%3==0:bits=[0 if case&1 else 0xffffffff]*5
        y=rng.randrange(scene_rows+1);invert=0xffffffff if case&2 else 0
        expected=next((n for n in range(y,scene_rows) if ((bits[n//32]^invert)>>(n&31))&1),scene_rows)
        cpu.mem_write(bits_addr,struct.pack('<5I',*bits))
        assert call('coverage_edge',bits_addr,y,invert)==expected,case
    panel_vectors=0
    if 'dok_gba_copy_panel_rows' in symbols:
        for case in range(768):
            width,height=rng.choice([(240,16),(16,16),*((w,14) for w in range(24,69,4))])
            x=rng.randrange((240-width)//4+1)*4;y=rng.randrange(161-height)
            source=rng.randbytes(width*height);frame=bytearray(rng.randbytes(38400))
            cpu.mem_write(pixel_addr,source);cpu.mem_write(frame_addr,bytes(frame))
            for row in range(height):frame[(y+row)*240+x:(y+row)*240+x+width]=source[row*width:(row+1)*width]
            call('dok_gba_copy_panel_rows',frame_addr+y*240+x,pixel_addr,width//4,height)
            assert bytes(cpu.mem_read(frame_addr,38400))==frame,('panel',case)
            panel_vectors+=1
    world_vectors=0
    coarse='g_world_texture_coarse' in symbols
    for name,wrapped,shifted in (('dok_gba_world_texture_span',False,False),
                                  ('dok_gba_world_texture_span_shift',False,True),
                                  ('dok_gba_world_texture_span_wrap',True,True)):
        if name not in symbols:continue
        for case in range(1024):
            width=rng.choice((1,2,17,48,64,92,96,128,256));height=rng.choice((1,2,31,48,64,96,256))
            pixels=rng.randbytes(width*height);lut=rng.randbytes(256)
            frame=bytearray(rng.randbytes(38400));bits=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
            x=rng.randrange(120)*2;y=rng.randrange(72)*2 if coarse else rng.randrange(scene_rows);count=rng.randrange(1,121-x//2)
            wx,wz=rng.randrange(0,0x7fffffff),rng.randrange(0,0x7fffffff)
            dx,dz=rng.randrange(-120000,120001),rng.randrange(-120000,120001)
            shiftx=rng.randrange(1-width,width) if shifted else 0
            shifty=rng.randrange(1-height,height) if shifted else 0
            mask=1<<(y&31)
            payload=struct.pack('<4I4i4I2i',frame_addr+y*240+x,bits_addr+(x//2*cover_words+y//32)*4,pixel_addr,lut_addr,
                                wx,wz,dx,dz,mask,count,width-1,height-1,shiftx,shifty)
            cpu.mem_write(pixel_addr,pixels);cpu.mem_write(lut_addr,lut);cpu.mem_write(desc,payload)
            cpu.mem_write(frame_addr,bytes(frame));cpu.mem_write(bits_addr,struct.pack(cover_format,*bits))
            expected=0
            stride=2 if coarse else 1
            for sample in range(0,count,stride):
                tx=((wx&65535)*(width-1)>>16)+shiftx;ty=((wz&65535)*(height-1)>>16)+shifty
                if wrapped:
                    if tx<0:tx+=width
                    elif tx>=width:tx-=width
                    if ty<0:ty+=height
                    elif ty>=height:ty-=height
                color=lut[0 if tx<0 or tx>=width or ty<0 or ty>=height else pixels[ty*width+tx]]
                for sub in range(min(stride,count-sample)):
                    col=x+(sample+sub)*2;offset=col//2*cover_words+y//32
                    for row_offset in range(2 if coarse else 1):
                        rowmask=mask<<row_offset
                        if not bits[offset]&rowmask:
                            at=(y+row_offset)*240+col
                            frame[at:at+2]=bytes((color,color));bits[offset]|=rowmask;expected+=1
                wx+=dx*stride;wz+=dz*stride
            assert call(name,desc)==expected,(name,case)
            assert bytes(cpu.mem_read(frame_addr,38400))==frame,(name,case,'frame')
            assert bytes(cpu.mem_read(bits_addr,cover_bytes))==struct.pack(cover_format,*bits),(name,case,'coverage')
            world_vectors+=1
    world_skip_vectors=0
    if 'dok_gba_world_skip' in symbols:
        for case in range(1024):
            bits=list(struct.unpack(cover_format,rng.randbytes(cover_bytes)))
            if case%3==0:bits=[0 if case&1 else 0xffffffff]*cover_count
            x=rng.randrange(120);y=rng.randrange(72)*2;count=rng.randrange(1,121-x)
            mask=(3 if case&1 else 1)<<(y&31)
            expected=next((k for k in range(count) if bits[(x+k)*cover_words+y//32]&mask!=mask),count)
            raw=struct.pack(cover_format,*bits);cpu.mem_write(bits_addr,raw)
            assert call('dok_gba_world_skip',bits_addr+(x*cover_words+y//32)*4,mask,count)==expected,case
            assert bytes(cpu.mem_read(bits_addr,cover_bytes))==raw
            world_skip_vectors+=1
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
                exact_plane_division_vectors=plane_division_vectors,
                scalar_projection_vectors=projection_vectors,
                rectangle_raster_vectors=rectangle_vectors,
                coverage_span_vectors=coverage_span_vectors,
                unsigned_texture_division_vectors=unsigned_division_vectors,
                positive_texture_division_vectors=positive_division_vectors,
                covered_face_box_vectors=face_box_vectors,
                exact_panel_copy_vectors=panel_vectors,world_texture_vectors=world_vectors,
                world_skip_vectors=world_skip_vectors,world_coarse_4x2=coarse,
                hardware_pack_vectors=hw_pack_vectors,
                geometry_copy_vectors=geometry_copy_vectors,geometry_equal_vectors=geometry_equal_vectors,
                tiled_scene_vectors=tiled_vectors,
                nonzero_column_vectors=nonzero_column_vectors,
                cached_column_vectors=cached_column_vectors,
                hardware_map_vectors=map_vectors,
                hardware_masked_vectors=masked_tiled_vectors,hardware_gap_vectors=gap_tiled_vectors,
                coverage_scene_rows=scene_rows,
                empty_column_vectors=empty_column_vectors,
                sampler_digest=summary.hexdigest(),original_animation_resources=resource_count,
                original_animation_frames=decoded_count,callee_saved_registers_and_stack_checked=True,
                mutable_buffer_truncation_and_unchanged_error_output=True,
                timing_measured=False,scope='ARM/Thumb execution in Unicorn; hardware timing checked separately in mGBA')
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':main()
