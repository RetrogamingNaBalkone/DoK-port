"""PERF05: O(1) zero-run lengths for prelit central-plane source columns."""
import hashlib
import struct


def build_zero_runs(out, package):
    data=package.read_bytes();bank=(out/'prelit.bin').read_bytes()
    offsets=(out/'exact-offsets.bin').read_bytes()
    u16=lambda at:struct.unpack_from('<H',data,at)[0]
    u32=lambda at:struct.unpack_from('<I',data,at)[0]
    count=u32(20);selected=set()
    for view in range(6):
        rec=u32(28)+view*80;defs=u32(rec+36);bindings=u32(rec+72)
        for i in range(u16(rec+76)):
            obj,slot,rid=struct.unpack_from('<3H',data,bindings+i*8)
            if obj<u16(rec+16) and slot==0xfff0 and u16(defs+obj*32)==4:
                selected.add((view,rid))
    payload=bytearray(4);index=[];cache={}
    for i in range(len(offsets)//4):
        view=i//((count+1)*16);rid=i//16%(count+1)
        base=struct.unpack_from('<I',offsets,i*4)[0]
        if not base or (view,rid) not in selected:index.append(0);continue
        start=u32(u32(24)+(rid-1)*32+16+(view%2)*8)
        w,h=struct.unpack_from('<HH',data,start)
        colors=bank[base:base+w*h];assert len(colors)==w*h
        key=(w,h,colors)
        if key not in cache:
            runs=bytearray(len(colors))
            for x in range(w):
                n=0
                for y in reversed(range(h)):
                    n=min(n+1,255) if colors[x*h+y]==0 else 0
                    runs[x*h+y]=n
            for j,n in enumerate(runs):
                assert j%h+n<=h and not any(colors[j:j+n])
                if n<255 and j%h+n<h:assert colors[j+n]!=0
            cache[key]=len(payload);payload.extend(runs)
        index.append(cache[key])
    (out/'zero-runs.bin').write_bytes(payload)
    (out/'zero-run-offsets.bin').write_bytes(struct.pack('<'+'I'*len(index),*index))
    assembly=['@ zero runs '+hashlib.sha256(payload).hexdigest()]
    for symbol,name in [('dok_zero_runs','zero-runs.bin'),('dok_zero_run_offsets','zero-run-offsets.bin')]:
        assembly += [f'.section .rodata.{symbol},"a",%progbits','.balign 4',f'.global {symbol}',
                     f'{symbol}:',f'.incbin "{(out/name).resolve().as_posix()}"']
    (out/'zero-runs.S').write_text('\n'.join(assembly)+'\n')
    return dict(bytes=len(payload),index_bytes=len(index)*4,unique_images=len(cache))
