"""G17 original glyph and 38-byte message copy executed as x86 slices.

Only the virtual pixel operation is replaced by a recording sink. Glyph loops,
font indexing, signed clipping, transparent/opaque mode, far args/return and
message padding execute original machine code. This is not a DOS session.
"""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, 'C:/Source/wasdify/build/research-deps')
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as R
from verify_sprites_gba import original_animations

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    report = dict(task='G17', status='passed', whole_DOS_session=False,
                  intercepted='virtual pixel callback only', languages={})
    native_rows = []
    for language in ('ru', 'en'):
        ru = language == 'ru'
        directory = Path('build/ui') / language
        data_seg, font_seg, hud_seg = (0x1190, 0x1058, 0x10c0) if ru else (0x10d0, 0x1018, 0x1050)
        data=(directory/f'{data_seg:04x}.bin').read_bytes()
        c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        # NE segments are independent selector address spaces. Load only the
        # selected code block per execution: linear cs*16 ranges may overlap.
        pixels = []
        def hook(cpu, address, size, unused):
            if cpu.reg_read(R.UC_X86_REG_CS) == 0x6000:
                sp = cpu.reg_read(R.UC_X86_REG_SP)
                obj_off,obj_seg,x,y,color = struct.unpack('<HHiiH', cpu.mem_read(0x30000+sp+4,14))
                assert (obj_off,obj_seg)==(0,0x4000)
                pixels.append((x,y,color & 255))
                ip,cs=struct.unpack('<HH',cpu.mem_read(0x30000+sp,4))
                cpu.reg_write(R.UC_X86_REG_SP,sp+4)
                cpu.reg_write(R.UC_X86_REG_CS,cs)
                cpu.reg_write(R.UC_X86_REG_IP,ip)
        c.hook_add(UC_HOOK_CODE, hook)
        def run(segment, entry, args):
            for register,value in ((R.UC_X86_REG_CS,segment),(R.UC_X86_REG_SS,0x3000),
                                   (R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):
                c.reg_write(register,value)
            c.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x7000)+args)
            c.emu_start(segment*16+entry,0x7fff0,count=100000)
            assert c.reg_read(R.UC_X86_REG_SP)==0xf004
        c.mem_write(font_seg*16,(directory/f'{font_seg:04x}.bin').read_bytes())
        c.mem_write(0x31004,struct.pack('<HH',0,0x6000))
        mask_at=0x4d4 if ru else 0x4dc
        masks=data[mask_at:mask_at+16]
        assert struct.unpack('<8H',masks)==(128,64,32,16,8,4,2,1)
        c.mem_write(0x30000+mask_at,masks)
        resources,origin=original_animations(0 if ru else 1)
        count=0
        for height in (8,16):
            font=resources[f'FONT{height}'.encode().hex()]
            assert len(font)==256*height
            c.mem_write(0x50000,font)
            for glyph in range(256):
                for x,y,mode in ((3,4,1),(-3,155,0)):
                    obj=bytearray(0x80)
                    struct.pack_into('<H',obj,0,0x1000)
                    struct.pack_into('<HH',obj,0x1a,0,0x5000)
                    obj[0x1e]=255;obj[0x20]=0
                    struct.pack_into('<H',obj,0x24,height)
                    struct.pack_into('<iiii',obj,0x38,0,0,239,159)
                    c.mem_write(0x40000,bytes(obj));pixels.clear()
                    run(font_seg,0x158 if ru else 0x153e,struct.pack('<HHiiHH',0,0x4000,x,y,glyph,mode))
                    expected=[]
                    for row in range(height):
                        for col in range(8):
                            on=bool(font[glyph*height+row]&(128>>col))
                            if (on or mode!=1) and 0<=x+col<240 and 0<=y+row<160:
                                expected.append((x+col,y+row,255 if on else 0))
                    assert pixels==expected,(language,height,glyph,x,y,mode)
                    count+=1
        c.mem_write(hud_seg*16,(directory/f'{hud_seg:04x}.bin').read_bytes())
        for size in (0,1,29,30,31,36,38,39,240):
            payload=bytes(65+i%26 for i in range(size))
            c.mem_write(0x40000,b'\xa5'*128);c.mem_write(0x50000,payload+b'\0')
            run(hud_seg,0x1278 if ru else 0x15db,struct.pack('<HHHH',0,0x4000,0,0x5000))
            actual=bytes(c.mem_read(0x40000,128))
            assert actual[0x2c:0x52]==payload[:38].ljust(38,b' ')
            assert actual[0x52]==0 and actual[0x53:]==b'\xa5'*(128-0x53)
            assert struct.unpack_from('<H',actual,0x10)[0]==(0xa5a5|0x22)
        offsets=[0x58c,0x59d,0x5ae,0x5c1,0x5d6,0x5ea,0x5fb] if ru else [0x59c,0x5a7,0x5b4,0x5ca,0x5de,0x5f4,0x601]
        texts=[data[at:data.index(0,at)] for at in offsets]
        native_rows.append(texts)
        manifest=json.loads((directory/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['sha256_before']==manifest['sha256_after']
        assert sha(manifest['sample'])==manifest['sha256_after']
        report['languages'][language]=dict(sample=manifest['sample'],sha256=manifest['sha256_after'],
            manifest_sha256=sha(directory/'analysis-manifest.json'),glyph_vectors=count,message_vectors=9,
            exports={p.name:sha(p) for p in directory.iterdir() if p.suffix in ('.asm','.c','.bin')},
            data_segment=f'{data_seg:04x}',native_text_offsets=[hex(at) for at in offsets],
            native_text_cp866_hex=[t.hex() for t in texts],font_source=origin)
    checked=re.findall(r'"((?:\\[0-7]{3})+)"',Path('src/ui_native_text.inc').read_text())
    actual=[bytes(int(v,8) for v in re.findall(r'\\([0-7]{3})',s)) for s in checked]
    assert actual==[text for row in native_rows for text in row]
    report['script_sha256']=sha(__file__)
    Path('analysis/g17-original-verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print('G17 original x86: 2048 glyph/clipping/mode vectors and18 message-copy vectors passed')

if __name__=='__main__':
    main()
