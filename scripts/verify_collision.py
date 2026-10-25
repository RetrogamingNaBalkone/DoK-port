"""G08: execute Ghidra-exported collision instructions for both originals.

Real-mode Unicorn stands in for valid DPMI far pointers; native code executes
both footprint and resolver without hooked callees. Not a whole DOS session.
"""
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn import x86_const as reg

def fixture(model):
    flags=[0]*256;classes=[0]*256
    for o in model['objects']: flags[o['glyph']]=o['flags'];classes[o['glyph']]=o['class']
    maps=bytearray(b''.join(bytes.fromhex(p) for p in model['maps'][:3]))
    # Footprint tests run after wrapper removes self from MMap.
    glyph=model['objects'][model['player_index']-1]['glyph']
    maps[:len(maps)//3]=maps[:len(maps)//3].replace(bytes([glyph]),b' ')
    return (model['settings'][0],model['settings'][2],model['settings'][3],model['settings'][7],glyph),flags,classes,maps

def host(f,vectors):
    h,flags,classes,maps=f
    data=struct.pack('<5H',*h)+struct.pack('<256H',*flags)+struct.pack('<256H',*classes)+bytes(maps)
    data+=b''.join(struct.pack('<5i',*v) for v in vectors)
    result=subprocess.run([ROOT/'build/native/dok-collision-probe.exe'],input=data,stdout=subprocess.PIPE,check=True).stdout
    assert len(result)==1040*len(vectors)
    return [struct.unpack_from('<260i',result,i*1040) for i in range(len(vectors))]

class Original:
    def __init__(self,language,f):
        self.ru=language=='ru';self.cpu=Uc(UC_ARCH_X86,UC_MODE_16)
        c=self.cpu;c.mem_map(0,0x100000)
        c.mem_write(0x60000,(ROOT/'build/collision'/language/'world-segment.bin').read_bytes())
        h,flags,classes,maps=f;self.h=h
        header=bytearray(0x750)
        struct.pack_into('<3h',header,0,h[0],1,h[1]);struct.pack_into('<h',header,10,h[3])
        struct.pack_into('<h',header,0x738,h[2]);struct.pack_into('<i',header,0x742,h[0])
        for glyph in range(256):
            off=0x1000+glyph*0x80
            struct.pack_into('<HH',header,0x30e+glyph*4,off,0x4000)
            obj=bytearray(0x80);struct.pack_into('<H',obj,0x12,classes[glyph]);struct.pack_into('<H',obj,0x15,flags[glyph])
            c.mem_write(0x40000+off,bytes(obj))
        c.mem_write(0x40000,bytes(header))
        base=0x608a if self.ru else 0x613a
        for p in range(3):c.mem_write(0x30000+base+p*4356,bytes(maps[p*h[0]*h[1]:(p+1)*h[0]*h[1]]))
    def run(self,v):
        op,x,z,nx,nz=v;c=self.cpu
        c.reg_write(reg.UC_X86_REG_SS,0x3000);c.reg_write(reg.UC_X86_REG_CS,0x6000)
        c.reg_write(reg.UC_X86_REG_SP,0xf000);c.reg_write(reg.UC_X86_REG_BP,0xf800)
        c.mem_write(0x4f000,struct.pack('<ii',x,z))
        if op==0:
            args=struct.pack('<8H2i',0xfff0,0x6000,0,0x4000,0xf000,0x4000,0xf004,0x4000,nx,nz)
            entry=0x901 if self.ru else 0x9e5
        else:
            args=struct.pack('<4H2ih',0xfff0,0x6000,0,0x4000,x,z,self.h[3])
            entry=0x448 if self.ru else 0x52c
        c.mem_write(0x3f000,args)
        c.emu_start(0x60000+entry,0x6fff0,count=20000)
        assert c.reg_read(reg.UC_X86_REG_SP)==0xf004
        if op==0:return struct.unpack('<ii',c.mem_read(0x4f000,8))
        return c.reg_read(reg.UC_X86_REG_AX)

class Wrapper(Original):
    def __init__(self,language,f):
        super().__init__(language,f)
        import re
        trig=list(map(int,re.findall(r'-?\d+',(ROOT/'src/renderer_trig.inc').read_text().split('{',1)[1])))
        self.cpu.mem_write((0x1180 if self.ru else 0x10c0)*16,struct.pack('<722i',*trig))
        self.actor=Uc(UC_ARCH_X86,UC_MODE_16);self.actor.mem_map(0,0x100000)
        self.actor.mem_write(0,bytes(self.cpu.mem_read(0,0x100000)))
        self.actor.mem_write(0x50000,(ROOT/'build/collision'/language/'actor-segment.bin').read_bytes())
        self.actor.mem_write(0x30000+(0x266a if self.ru else 0x271a),struct.pack('<HH',0,0x4000))
        self.actor.hook_add(UC_HOOK_CODE,self.hook)
    def hook(self,c,address,size,context):
        entry=0xe13 if self.ru else 0xef7
        if address!=(0x1098 if self.ru else 0x1030)*16+entry:return
        self.cpu.mem_write(0x30000,bytes(c.mem_read(0x30000,0xc000)))
        self.cpu.mem_write(0x4e000,bytes(c.mem_read(0x4e000,0x100)))
        self.cpu.reg_write(reg.UC_X86_REG_SS,0x3000);self.cpu.reg_write(reg.UC_X86_REG_CS,0x6000)
        self.cpu.reg_write(reg.UC_X86_REG_SP,0xf000);self.cpu.reg_write(reg.UC_X86_REG_BP,0xf800)
        sp=c.reg_read(reg.UC_X86_REG_SP)
        args=bytes(c.mem_read(0x30000+sp+4,10))
        self.cpu.mem_write(0x3f000,struct.pack('<HH',0xfff0,0x6000)+args)
        self.cpu.emu_start(0x60000+entry,0x6fff0,count=20000)
        c.mem_write(0x4e026,bytes(self.cpu.mem_read(0x4e026,12)))
        ip,cs=struct.unpack('<HH',c.mem_read(0x30000+sp,4))
        c.reg_write(reg.UC_X86_REG_SP,sp+4);c.reg_write(reg.UC_X86_REG_CS,cs);c.reg_write(reg.UC_X86_REG_IP,ip)
    def move(self,x,z,distance,heading,strafe=0):
        c=self.actor;data=bytearray(0x100)
        struct.pack_into('<iii',data,0x26,x,-300,z);struct.pack_into('<h',data,0x32,heading)
        data[0x5c]=self.h[4];c.mem_write(0x4e000,bytes(data))
        c.reg_write(reg.UC_X86_REG_SS,0x3000);c.reg_write(reg.UC_X86_REG_CS,0x5000)
        c.reg_write(reg.UC_X86_REG_SP,0xf000);c.reg_write(reg.UC_X86_REG_BP,0xf800)
        c.mem_write(0x3f000,struct.pack('<6HhH',0xfff0,0x5000,0xe000,0x4000,distance&65535,strafe,0,0))
        c.emu_start(0x50000+(0xeb1 if self.ru else 0x13bb),0x5fff0,count=50000)
        assert c.reg_read(reg.UC_X86_REG_SP)==0xf004
        x,_,z=struct.unpack('<iii',c.mem_read(0x4e026,12))
        base=0x608a if self.ru else 0x613a
        mmap=bytes(c.mem_read(0x30000+base,self.h[0]*self.h[1]));hash=2166136261
        for b in mmap:hash=((hash^b)*16777619)&0xffffffff
        counters=[struct.unpack('<h',c.mem_read(0x41000+g*0x80+0x56,2))[0] for g in range(256)]
        return x,z,hash,counters

def main():
    rng=random.Random(808);reports=[]
    for language in ('ru','en'):
        manifest=json.loads((ROOT/'build/collision'/language/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['sha256_before']==manifest['sha256_after']==hashlib.sha256((ROOT/manifest['sample']).read_bytes()).hexdigest()
        for level in ('LEVEL1','LEVEL2','LEVEL3'):
            model=json.loads((ROOT/'build/runtime'/language/f'{level}-runtime.json').read_text());f=fixture(model)
            h,flags,classes,maps=f;native=Original(language,f)
            vectors=[]
            # Every cell center, plus random near-edge positions and signed steps.
            for row in range(h[1]):
                for col in range(h[0]):
                    x=col*h[2]+h[2]//2;z=row*h[2]+h[2]//2
                    vectors.extend([(1,x,z,0,0),(0,x,z,x+rng.randint(-180,180),z+rng.randint(-180,180))])
            for _ in range(1000):
                x=rng.randint(-480,h[0]*h[2]+480);z=rng.randint(-480,h[1]*h[2]+480)
                vectors.append((0,x,z,x+rng.randint(-180,180),z+rng.randint(-180,180)))
            actual=host(f,vectors)
            for v,a in zip(vectors,actual):
                n=native.run(v)
                assert (a[:2] if v[0]==0 else a[2])==n,(language,level,v,a[:3],n)
            wrapper=Wrapper(language,f);pose=model['objects'][model['player_index']-1]['actor']
            x,z=pose['x'],pose['z'];walk=[];native_walk=[]
            for i in range(200):
                distance=rng.choice((-240,-120,120,240));heading=rng.randrange(360);strafe=rng.randrange(2)
                walk.append((4+strafe,x,z,distance,heading))
                n=wrapper.move(x,z,distance,heading,strafe);native_walk.append(n);x,z=n[:2]
            got=host(f,walk)
            for v,a,n in zip(walk,got,native_walk):
                assert a[:2]==n[:2] and a[3]&0xffffffff==n[2] and list(a[4:])==n[3],(language,level,v,a[:4],n[:3])
            reports.append(dict(language=language,level=level,vectors=len(vectors),wrapper_steps=len(walk),manifest=manifest))
            print(language,level,len(vectors),'native footprint/resolver passed',flush=True)
    Path('analysis/g08-collision-evidence.json').write_text(json.dumps(dict(task='G08',status='passed',
        cases=reports,script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        artifacts={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for language in ('ru','en') for name in ('evidence.asm','automatic.c','actor-segment.bin','world-segment.bin')
            for p in [ROOT/'build/collision'/language/name]},
        scope='Ghidra x86 footprint/resolver without intercepted callees; wrapper bridges original far world-move call to second CPU; valid real-mode substitute for DPMI pointers',
        whole_dos_session=False),indent=2)+'\n')

if __name__=='__main__':main()
