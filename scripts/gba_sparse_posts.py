"""G20C auxiliary nonzero source runs; existing prelit payload is unchanged."""
import hashlib
import struct


def build_posts(out, package):
    data=package.read_bytes();bank=(out/'prelit.bin').read_bytes()
    offsets=(out/'exact-offsets.bin').read_bytes()
    count=struct.unpack_from('<I',data,20)[0]
    directory=struct.unpack_from('<I',data,24)[0]
    image_offsets=[];images={};records={};payload=bytearray(4);columns=0
    for index in range(len(offsets)//4):
        base=struct.unpack_from('<I',offsets,index*4)[0]
        if not base:image_offsets.append(0);continue
        view=index//((count+1)*16);rid=index//16%(count+1)
        start,size=struct.unpack_from('<II',data,directory+(rid-1)*32+16+(view%2)*8)
        width,height=struct.unpack_from('<HH',data,start)
        assert size==18+width*height and height<=65535
        key=(base,width,height)
        if key not in images:
            payload.extend(bytes(-len(payload)%4));table=len(payload)
            payload.extend(bytes(width*4));images[key]=table
            for x in range(width):
                column=bank[base+x*height:base+(x+1)*height]
                runs=[];first=None
                for y,color in enumerate(column+b'\0'):
                    if color and first is None:first=y
                    elif not color and first is not None:runs.append((first,y));first=None
                # Independent reconstruction asserts the exact post-LIGHT mask.
                restored=bytearray(height)
                for lo,hi in runs:restored[lo:hi]=column[lo:hi]
                assert restored==column
                record=struct.pack('<H',len(runs))+b''.join(struct.pack('<HH',*r) for r in runs)
                if record not in records:
                    records[record]=len(payload);payload.extend(record)
                struct.pack_into('<I',payload,table+x*4,records[record]);columns+=1
        image_offsets.append(images[key])
    files={'dok_sparse_image_offsets':('sparse-offsets.bin',struct.pack('<'+'I'*len(image_offsets),*image_offsets)),
           'dok_sparse_posts':('sparse-posts.bin',payload)}
    assembly=[]
    for symbol,(filename,blob) in files.items():
        (out/filename).write_bytes(blob)
        assembly.extend(['.section .rodata.sparse_posts,"a",%progbits','.balign 4',f'.global {symbol}',
                         symbol+':',f'.incbin "{(out/filename).resolve().as_posix()}"'])
    (out/'sparse-posts.S').write_text('\n'.join(assembly)+'\n',encoding='utf-8')
    return dict(images=len(images),columns=columns,unique_records=len(records),
                bytes=sum(len(blob) for _,blob in files.values()),
                bank_sha256=hashlib.sha256(bank).hexdigest(),
                package_sha256=hashlib.sha256(data).hexdigest(),
                format='u32 image offsets; u32 per-column record offsets; u16 count, u16 begin/end exclusive')
