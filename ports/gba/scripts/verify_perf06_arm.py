"""PERF06 flat span/column/fence kernels: independent pixels, coverage and ABI."""
import argparse
import json
from pathlib import Path
import random
import struct
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_ARM
from unicorn import arm_const as R
from gba_rom import elf_info
from verify_gba import sha


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    a=ap.parse_args();elf=a.build_dir/'dok-diagnostic.elf';data=elf.read_bytes()
    _,symbols,_=elf_info(elf);cpu=Uc(UC_ARCH_ARM,UC_MODE_ARM)
    for addr,size in [(0x02000000,0x40000),(0x03000000,0x10000),(0x08000000,0x2000000)]:cpu.mem_map(addr,size)
    off=struct.unpack_from('<I',data,28)[0];size,count=struct.unpack_from('<HH',data,42)
    for i in range(count):
        kind,at,addr,_,length,_,_,_=struct.unpack_from('<8I',data,off+i*size)
        if kind==1 and length:cpu.mem_write(addr,data[at:at+length])
    frame=0x02010004;bits_at=0x02020000;record_at=0x02022000;packet=0x02023000
    rng=random.Random(6006);totals={}
    tile=lambda x,y:(y//8)*1024+(x//16)*64+(y%8)*8+(x//2)%8
    def call(name,args):
        cpu.reg_write(R.UC_ARM_REG_CPSR,0x1f)
        for i,v in enumerate(args[:4]):cpu.reg_write(getattr(R,f'UC_ARM_REG_R{i}'),v)
        for i in range(4,12):cpu.reg_write(getattr(R,f'UC_ARM_REG_R{i}'),0x50000000+i)
        cpu.reg_write(R.UC_ARM_REG_SP,0x03007e00);cpu.reg_write(R.UC_ARM_REG_LR,0x03007ff0)
        if len(args)>4:cpu.mem_write(0x03007e00,struct.pack('<'+'I'*(len(args)-4),*args[4:]))
        cpu.emu_start(symbols[name],0x03007ff0,count=200000)
        assert cpu.reg_read(R.UC_ARM_REG_PC)==0x03007ff0
        assert cpu.reg_read(R.UC_ARM_REG_SP)==0x03007e00
        for i in range(4,12):assert cpu.reg_read(getattr(R,f'UC_ARM_REG_R{i}'))==0x50000000+i
        return cpu.reg_read(R.UC_ARM_REG_R0)
    for name in ['dok_gba_flat_span','dok_gba_flat_column','dok_gba_far_fence']:
        if name not in symbols:continue
        for case in range(512):
            original=rng.randbytes(0x8000);expected=bytearray(original)
            bits=[rng.getrandbits(32) if case%3==0 else (0xffffffff if case%3==1 else 0) for _ in range(480)]
            cpu.mem_write(frame,original);cpu.mem_write(frame-4,b'HEAD');cpu.mem_write(frame+len(original),b'TAIL')
            cpu.mem_write(bits_at,struct.pack('<480I',*bits))
            x=rng.randrange(120)*2;lo=rng.randrange(128);hi=rng.randrange(lo,128);color=rng.randrange(1,256)
            wanted=0
            def pixel(px,py):
                nonlocal wanted
                index=px//2*4+py//32;mask=1<<(py%32)
                if not bits[index]&mask:
                    bits[index]|=mask;expected[tile(px,py)]=color;wanted+=1
            if name=='dok_gba_flat_span':
                count=rng.randrange(1,(240-x)//2+1)
                for k in range(count):pixel(x+k*2,lo)
                args=[frame+tile(x,lo),bits_at+(x//2*4+lo//32)*4,1<<(lo%32),count,color]
            elif name=='dok_gba_flat_column':
                for y in range(lo,hi+1):pixel(x,y)
                args=[frame+tile(x,lo),bits_at+(x//2*4+lo//32)*4,1<<(lo%32),hi-lo+1,color,lo%8]
            else:
                top=lo-rng.randrange(90);bottom=hi+rng.randrange(90);u=rng.randrange(256)
                rows=sorted(rng.sample(range(257),8));rails=list(zip(rows[::2],rows[1::2]))
                solid=case%2;record=bytearray(276);record[1]=4;record[4+u]=solid
                for k,(first,last) in enumerate(rails):struct.pack_into('<HH',record,260+k*4,first,last)
                cpu.mem_write(record_at,bytes(record))
                intervals=[(lo,hi)] if solid else [(max(lo,top+((bottom-top)*s>>8)),min(hi,top+((bottom-top)*e>>8))) for s,e in rails]
                for s,e in intervals:
                    for y in range(s,e+1):pixel(x,y)
                cpu.mem_write(packet,struct.pack('<5I4iI',frame,bits_at+x//2*16,record_at,x,u,top,bottom,lo,hi,color))
                args=[packet]
            assert call(name,args)==wanted,(name,case,'count')
            assert bytes(cpu.mem_read(frame,len(original)))==expected,(name,case,'pixels')
            assert bytes(cpu.mem_read(bits_at,1920))==struct.pack('<480I',*bits),(name,case,'coverage')
            assert bytes(cpu.mem_read(frame-4,4))==b'HEAD' and bytes(cpu.mem_read(frame+len(original),4))==b'TAIL'
        totals[name]=512
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(dict(status='passed',vectors=totals,rom_sha256=sha(a.build_dir/'dok-diagnostic.gba'),abi_guards=True),indent=2)+'\n')
    print(totals)


if __name__=='__main__':main()
