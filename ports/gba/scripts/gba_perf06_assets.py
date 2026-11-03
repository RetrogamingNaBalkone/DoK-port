"""PERF06 experimental fence silhouettes: fewer vertical bars, flat color."""
from collections import Counter
import hashlib
import struct


from experiment_cleanup import write_generated_bytes


def build_fences(out, package):
    b=package.read_bytes();u16=lambda p:struct.unpack_from('<H',b,p)[0]
    u32=lambda p:struct.unpack_from('<I',b,p)[0]
    payload=bytearray(4);offsets=[0]*1536;cache={};records=[]
    def groups(mask):
        result=[];start=None
        for i,value in enumerate([*mask,False]):
            if value and start is None:start=i
            if not value and start is not None:result.append((start,i));start=None
        return result
    for view in range(6):
        rec=u32(28)+view*80;defs=u32(rec+36);bind=u32(rec+72)
        links={(u16(bind+i*8),u16(bind+i*8+2)):u16(bind+i*8+4) for i in range(u16(rec+76))}
        for obj in range(u16(rec+16)):
            if u16(defs+obj*32)!=4 or (obj,0xfff0) not in links:continue
            rid=links[obj,0xfff0];at=u32(u32(24)+(rid-1)*32+16+(view%2)*8)
            w,h=struct.unpack_from('<HH',b,at);pixels=b[at+18:at+18+w*h]
            if not .25<=pixels.count(0)/len(pixels)<=.95:continue
            cols=groups([sum(pixels[y*w+x]!=0 for y in range(h))*5>=h*3 for x in range(w)])
            rows=groups([sum(pixels[y*w+x]!=0 for x in range(w))*5>=w*3 for y in range(h)])
            if len(cols)<3 or len(rows)>4:continue
            kept=[run for i,run in enumerate(cols) if i%2==0 or i==len(cols)-1]
            flags=bytearray(256)
            for lo,hi in kept:flags[lo:hi]=bytes([1])*(hi-lo)
            color=Counter(v for v in pixels if v).most_common(1)[0][0]
            record=bytes([color,len(rows),0,0])+flags
            record+=b''.join(struct.pack('<HH',lo*256//h,hi*256//h) for lo,hi in rows)
            record+=bytes((4-len(rows))*4)
            assert len(record)==276
            if record not in cache:cache[record]=len(payload);payload.extend(record)
            offsets[view*256+obj]=cache[record]
            records.append(dict(view=view,object=obj,glyph=b[defs+obj*32+10],resource=rid,
                                original_columns=cols,kept_columns=kept,rows=rows,color=color))
    write_generated_bytes(out/'perf06-fences.bin',payload)
    write_generated_bytes(out/'perf06-fence-offsets.bin',struct.pack('<1536I',*offsets))
    assembly=['@ '+hashlib.sha256(payload).hexdigest()]
    for symbol,name in [('dok_perf06_fences','perf06-fences.bin'),('dok_perf06_fence_offsets','perf06-fence-offsets.bin')]:
        assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'.global {symbol}',f'{symbol}:',f'.incbin "{(out/name).resolve().as_posix()}"']
    (out/'perf06-fences.S').write_text('\n'.join(assembly)+'\n')
    return dict(bytes=len(payload)+6144,records=records)
