"""N13 original-instruction LIGHT/crop/water/VGA6-fade oracle; no GBA imports."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
from audit_sprites import originals,resources
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT/'research/evidence/nds/n13'
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
class Original:
    def __init__(self,language):
        self.ru=language=='ru';self.language=language;self.folder=OUT/'original'/language
        self.c=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x200000)
        m=json.loads((self.folder/'analysis-manifest.json').read_text('utf-8-sig'))
        assert m['read_only'] and m['sha256_before']==m['sha256_after']
        assert m['script_sha256']==digest(ROOT/'tools/ghidra/ExportNdsEffects.java')
        for seg,base in [('10f8' if self.ru else '1060',0x80000),('1120' if self.ru else '1078',0x90000),
                         ('1088' if self.ru else '1028',0xa0000),('10b8' if self.ru else '1048',0xd0000),
                         ('1110' if self.ru else '1068',0xf0000),('1068' if self.ru else '1020',0x70000),
                         ('1010',0xe0000)]:
            p=self.folder/(seg+'.bin');assert digest(p)==m['binary_sha256'][seg];c.mem_write(base,p.read_bytes())
        self.stopped=False;self.schedule=[];self.percentages=[];self.dac=[]
        c.hook_add(UC_HOOK_CODE,self.hook)
    def ret(self,ax=None,dx=None):
        c=self.c;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        if ax is not None:c.reg_write(R.UC_X86_REG_AX,ax)
        if dx is not None:c.reg_write(R.UC_X86_REG_DX,dx)
        for r,v in [('SP',sp+4),('CS',cs),('IP',ip)]:c.reg_write(getattr(R,'UC_X86_REG_'+r),v)
    def hook(self,c,address,size,unused):
        if address in (0x8fff0,0xafff0,0xdfff0,0xffff0,0x7fff0):self.stopped=True;c.emu_stop();return
        image_seg=0x10f8 if self.ru else 0x1060;palette_seg=0x10b8 if self.ru else 0x1048
        scheduler=0x1038 if self.ru else 0x1010
        if address==image_seg*16+(0x592 if self.ru else 0x6a2):
            c.reg_write(R.UC_X86_REG_CS,0x8000);c.reg_write(R.UC_X86_REG_IP,0x592 if self.ru else 0x6a2)
        elif address==palette_seg*16+0x2ed:
            sp=c.reg_read(R.UC_X86_REG_SP);self.percentages.append(struct.unpack('<h',c.mem_read(0x30000+sp+12,2))[0])
            c.reg_write(R.UC_X86_REG_CS,0xd000);c.reg_write(R.UC_X86_REG_IP,0x2ed)
        elif address==scheduler*16+(0x265 if self.ru else 0x161b):
            sp=c.reg_read(R.UC_X86_REG_SP);self.schedule.append(struct.unpack('<i',c.mem_read(0x30000+sp+8,4))[0]);self.ret()
        elif address==scheduler*16+(0x214 if self.ru else 0x15ca):self.schedule.append(None);self.ret()
        elif address==scheduler*16+(0x108 if self.ru else 0x14be):self.getter_schedules+=1;self.ret()
        elif address==0x15cd5:
            sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
            ptr=c.reg_read(R.UC_X86_REG_DX)*16+c.reg_read(R.UC_X86_REG_AX)
            c.mem_write(0x30000+sp-4,bytes(c.mem_read(ptr,8)))
            for r,v in [('SP',sp-4),('CS',cs),('IP',ip)]:c.reg_write(getattr(R,'UC_X86_REG_'+r),v)
        elif address in ((0x1110*16+0x37a if self.ru else 0x1068*16+0x386),
                         0xf0000+(0x37a if self.ru else 0x386)):
            sp=c.reg_read(R.UC_X86_REG_SP);off,seg=struct.unpack('<HH',c.mem_read(0x30000+sp+4,4))
            self.dac.append(bytes(c.mem_read(seg*16+off,768)));self.ret()
        elif address==(0x10a8*16+0x65 if self.ru else 0x1038*16+0x8b0):self.ret(0x1000,0xb000)
        elif address in (0x15718,0x10a8*16+0xb7,0x1038*16+0x902):self.ret()
        elif not self.ru and address==0x10900:self.ret() # PKE diagnostic bookkeeping only
    def setup(self,segment,args=b''):
        self.stopped=False
        for r,v in [('SS',0x3000),('DS',0x3000),('CS',segment),('SP',0xf000),('BP',0xf800)]:
            self.c.reg_write(getattr(R,'UC_X86_REG_'+r),v)
        self.c.mem_write(0x3f000,struct.pack('<HH',0xfff0,segment)+args)
    def light(self,depth,near):
        self.setup(0x9000);self.c.mem_write(0x3f7f4,struct.pack('<i',depth))
        self.c.mem_write(0x30000+(0xdfa if self.ru else 0xea2),struct.pack('<h',near))
        self.c.emu_start(0x90000+(0x147d if self.ru else 0x1483),0x90000+(0x1497 if self.ru else 0x149d),count=30)
        return self.c.reg_read(R.UC_X86_REG_AX)&65535
    def clamp(self,row):
        for n in range(16):self.c.mem_write(0x41000+4*n,struct.pack('<HH',n*256,0xb000))
        self.setup(0xa000,struct.pack('<3H',0x1000,0x4000,row))
        self.c.emu_start(0xa0000+(0x544 if self.ru else 0x196e),0xafff1,count=300)
        assert self.stopped
        off,seg=struct.unpack('<HH',self.c.mem_read(0x30000+(0x25de if self.ru else 0x268e),4));assert seg==0xb000
        return off//256
    def crop(self,width,height,flags,divisor,phase,pixels):
        self.c.mem_write(0xb0000,pixels)
        self.c.mem_write(0x41000,struct.pack('<5HII',width,height,flags,divisor,phase,len(pixels),0xb0000000))
        self.setup(0x8000,struct.pack('<HH',0x1000,0x4000))
        self.c.emu_start(0x80000+(0x5c9 if self.ru else 0x6d9),0x8fff1,count=500000)
        assert self.stopped
        offset=self.c.reg_read(R.UC_X86_REG_AX);segment=self.c.reg_read(R.UC_X86_REG_DX)
        return bytes(self.c.mem_read(segment*16+offset,len(pixels)))
    def brightness(self,rgb6,percentage):
        self.c.mem_write(0xb0000,rgb6);self.setup(0xd000,struct.pack('<5H',0,0xb000,0x1000,0xb000,percentage))
        self.c.emu_start(0xd0000+0x2ed,0xdfff1,count=30000);assert self.stopped
        return bytes(self.c.mem_read(0xb1000,768))
    def span(self,row,masked):
        c=self.c;self.setup(0xe000)
        c.mem_write(0xb0000,bytes(range(256)));c.mem_write(0xb1000,row);c.mem_write(0xb2000,bytes([0xa5])*256)
        c.mem_write(0x3f7fc,struct.pack('<h',1));c.mem_write(0x3f7f4,struct.pack('<h',0))
        for r,v in [('GS',0xb000),('FS',0xb100),('ES',0xb200),('SI',0),('DI',0),('AX',1),('BX',0),('CX',256),('DX',0)]:
            c.reg_write(getattr(R,'UC_X86_REG_'+r),v)
        c.emu_start(0xe0000+(0x197 if masked else 0x168),0xe0000+(0x1c1 if masked else 0x18e),count=10000)
        assert c.reg_read(R.UC_X86_REG_CX)==0
        return bytes(c.mem_read(0xb2000,256))
    def fades(self,rgb6):
        result=[]
        for entry in ((0x524 if self.ru else 0x554),(0x5b7 if self.ru else 0x659)):
            self.c.mem_write(0x42048,rgb6);self.setup(0xf000,struct.pack('<HH',0x2000,0x4000))
            self.percentages=[];self.dac=[]
            self.c.emu_start(0xf0000+entry,0xffff1,count=2000000);assert self.stopped
            expected=list(range(0,101,3)) if entry in (0x524,0x554) else list(range(100,-1,-3))
            assert self.percentages==expected
            assert self.dac==[bytes(v*p//100 for v in rgb6) for p in expected]
            result.append(expected)
        return result
    def water(self):
        c=self.c;c.mem_write(0x44000,bytes(128));c.mem_write(0x44044,struct.pack('<HH',0x1000,0x4000))
        c.mem_write(0x41000,struct.pack('<5HII',17,19,0x408,1,0,323,0xb0000000))
        rows=[]
        for k in range(19):
            self.schedule=[];self.setup(0x7000,struct.pack('<HH',0x4000,0x4000))
            c.emu_start(0x70000+(0x741 if self.ru else 0xc33),0x7fff1,count=3000);assert self.stopped
            counter,interval=struct.unpack('<2h',c.mem_read(0x44050,4));divisor,phase=struct.unpack('<2H',c.mem_read(0x41006,4))
            assert divisor==18 and phase==min(k,17) and counter==min(k+1,18) and self.schedule==([0] if k<18 else [None])
            rows.append([counter,phase,self.schedule[0]])
        # Native getter resets18 before scheduling, preserving phase17. It
        # ignores scheduler capacity refusal (already instruction-proved N08).
        c.mem_write(0x44050,struct.pack('<h',18));self.getter_schedules=0
        self.setup(0x7000,struct.pack('<HH',0x4000,0x4000))
        c.emu_start(0x70000+(0x7a1 if self.ru else 0xc93),0x7fff1,count=3000)
        assert self.stopped and self.getter_schedules==1
        assert struct.unpack('<h',c.mem_read(0x44050,2))[0]==0
        assert struct.unpack('<H',c.mem_read(0x41008,2))[0]==17
        return rows

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dump',type=Path,required=True);args=p.parse_args()
    before=originals();report=dict(task='N13',passed=False,originals_before=before,languages={})
    rng=random.Random(13);vectors=[]
    for near in (-32768,-30000,-1,0,1920,30000,32767):
        depths={-32768,-32767,-1,0,1,32766,32767}
        depths.update((near+120*r+d+32768)%65536-32768 for r in range(17) for d in (-1,0,1))
        depths.update(rng.randrange(-32768,32768) for _ in range(80))
        vectors += [(d*65536+fraction,near) for d in sorted(depths) for fraction in (0,32767,65535) if -2**31<=d*65536+fraction<2**31]
    actual=list(map(int,subprocess.check_output([args.dump,'--light'],input=''.join(f'{d} {n}\n' for d,n in vectors),text=True).split()))
    assert len(actual)==len(vectors)
    for language in ('ru','en'):
        original=Original(language);clamps={r:original.clamp(r) for r in (0,1,14,15,16,17,32767,32768,65535)}
        assert all(v==min(r,15) for r,v in clamps.items())
        for (d,n),row in zip(vectors,actual):assert min(original.light(d,n),15)==row,(language,d,n,row)
        requests=[];expected=[];pixel_cases=0
        for w,h in ((17,19),(96,192),(64,64)):
            source=bytes((i*37+i//w*11)%256 for i in range(w*h));sw=1<<max(3,(w-1).bit_length());sh=1<<max(3,(h-1).bit_length())
            padded=b''.join(source[y*w:(y+1)*w]+bytes(sw-w) for y in range(h))+bytes(sw*(sh-h))
            for flags in (0,0x40,0x400,0x80,0x800,0x100,0x1000,0x200,0x2000,0x3fc0):
                for phase in (0,1,9,17,18):
                    native=original.crop(w,h,flags,18,phase,source)
                    requests.append(f'{w} {h} {sw} {sh} {flags} 18 {phase} 0\n'+padded.hex()+'\n'+bytes(range(256)).hex()+'\n')
                    expected.append(b''.join(native[y*w:(y+1)*w]+bytes(sw-w) for y in range(h))+bytes(sw*(sh-h)))
                    pixel_cases+=1
        outputs=subprocess.check_output([args.dump,'--pixels'],input=''.join(requests),text=True).splitlines()
        assert [bytes.fromhex(s) for s in outputs]==expected
        source=resources(language);palettes=[]
        catalog=json.loads((ROOT/f'analysis/originals/resources-{language}.json').read_text('utf8'))
        # Derive palette indices by their exact original byte-size/range; all
        # five VGA6 resources are independently inventoried by N04 as well.
        for index,data in enumerate(source):
            if len(data)==768 and max(data)<=63:palettes.append((index,data))
        assert len(palettes)==5
        for _,rgb6 in palettes:
            for percentage in range(101):assert original.brightness(rgb6,percentage)==bytes(v*percentage//100 for v in rgb6)
        fades=original.fades(palettes[0][1]);water=original.water()
        # Every actual LIGHT table and all VGA6 fade endpoints are compared to
        # C, including original odd6-bit components before5-bit conversion.
        cases=[];expected_palettes=[]
        lights=[data for data in source if len(data)==4096 and data[:256]==bytes(range(256))]
        assert len(lights)==3
        masked_requests=[];masked_expected=[]
        for light in lights:
            for row in range(16):
                lut=light[row*256:(row+1)*256]
                opaque=original.span(lut,False);masked=original.span(lut,True)
                assert opaque==lut and masked==bytes(v if v else 0xa5 for v in lut)
                pixels=bytes(range(256))+bytes(256*7)
                masked_requests.append('256 1 256 8 0 18 0 1\n'+pixels.hex()+'\n'+lut.hex()+'\n')
                masked_expected.append(bytes(i if lut[i] else 0 for i in range(256))+bytes(256*7))
        transformed=[bytes.fromhex(s) for s in subprocess.check_output([args.dump,'--pixels'],input=''.join(masked_requests),text=True).splitlines()]
        assert transformed==masked_expected
        for light in lights:
            assert all(light[r*256]==0 for r in range(16))
            for _,rgb6 in palettes:
                for percentage in (0,1,3,33,50,66,99,100):
                    cases.append(str(percentage)+'\n'+rgb6.hex()+'\n'+light.hex()+'\n')
                    expected_palettes.append(''.join(f'{((rgb6[3*i]*percentage//100)>>1)|(((rgb6[3*i+1]*percentage//100)>>1)<<5)|(((rgb6[3*i+2]*percentage//100)>>1)<<10):04x}' for i in light))
        actual_palettes=subprocess.check_output([args.dump,'--palette'],input=''.join(cases),text=True).splitlines()
        assert actual_palettes==expected_palettes
        report['languages'][language]=dict(light_vectors=len(vectors),clamps=clamps,crop_vectors=pixel_cases,
            vga6_brightness_palettes=len(palettes),percentages=101,palette_variants=len(cases)*16,
            fade_percentages=fades,water_callbacks=water,manifest_sha256=digest(original.folder/'analysis-manifest.json'))
        report['languages'][language]['native_after_LIGHT_texels']=len(lights)*16*256*2
        print(language,report['languages'][language]['light_vectors'],'LIGHT vectors,',pixel_cases,'crop vectors PASS',flush=True)
    report['originals_after']=originals();assert before==report['originals_after'];report['passed']=True
    (OUT/'original-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
if __name__=='__main__':main()
