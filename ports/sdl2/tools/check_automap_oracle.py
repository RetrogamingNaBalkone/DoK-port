"""Execute original map x86; replace graphics device calls, never map traversal."""
from pathlib import Path
import hashlib,json,struct,sys
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'research/evidence/source-port/sp15-map'
def fixture(path):
    raw=path.read_bytes();w,h,n,hero,reveal,hx,hz,heading=struct.unpack_from('<8H',raw);at=16;objects=[]
    for i in range(n):
        glyph,cls,marker,state=struct.unpack_from('<4H',raw,at);at+=8;b=bytearray(0x180);struct.pack_into('<H',b,0x12,cls);b[0x1b]=marker;struct.pack_into('<h',b,0xa2,state)
        objects.append(dict(glyph=glyph,cls=cls,marker=marker,bytes=b.hex()))
    maps=[raw[at+i*w*h:at+(i+1)*w*h] for i in range(3)];at+=3*w*h
    return dict(w=w,h=h,objects=objects,hero=hero,reveal=reveal,hx=hx,hz=hz,heading=heading,maps=maps,visited=raw[at:at+w*h])
class OldGold:
    def __init__(self):
        folder=OUT/'old-gold';manifest=json.loads((folder/'manifest.json').read_text());assert hashlib.sha256((ROOT/manifest['sample']).read_bytes()).hexdigest()==manifest['sha256_before']==manifest['sha256_after']
        self.cpu=c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000);c.mem_write(0x21b8f,(folder/'world-code.bin').read_bytes());c.mem_write(0x32550,(folder/'data.bin').read_bytes());c.hook_add(UC_HOOK_CODE,self.hook)
    def ret(self,value=0):
        c=self.cpu;sp=c.reg_read(R.UC_X86_REG_SP);ip,cs=struct.unpack('<HH',c.mem_read(0x50000+sp,4));c.reg_write(R.UC_X86_REG_AX,value&65535);c.reg_write(R.UC_X86_REG_DX,(value>>16)&65535);c.reg_write(R.UC_X86_REG_SP,sp+4);c.reg_write(R.UC_X86_REG_CS,cs);c.reg_write(R.UC_X86_REG_IP,ip)
    def hook(self,c,address,size,unused):
        if address==0x2fff0:c.emu_stop();return
        sp=c.reg_read(R.UC_X86_REG_SP);args=bytes(c.mem_read(0x50000+sp+4,24))
        if address==0x1ebe2:
            glyph=struct.unpack_from('<H',args,4)[0];self.ret((0x7000<<16)|self.ptr[glyph]);return
        if address==0x1ecd7:
            off,seg=struct.unpack_from('<HH',args);self.ret(struct.unpack('<H',c.mem_read(seg*16+off+0x12,2))[0]);return
        if address==0x1ef04:self.ret(0);return
        if address==0x28592:self.ret();return # graphics clipping
        if address==0x17d07:
            self.color=args[4];self.ret();return
        if address==0x28e1a:
            x,y,x2,y2=struct.unpack_from('<4i',args,4);self.rect(x,y,x2,y2,self.color);self.ret();return
        # Device virtual rectangle (vtable+34) and original clipping wrappers.
        if address==0x80100:
            x,y,x2,y2=struct.unpack_from('<4i',args,4);self.rect(x,y,x2,y2,self.color);self.ret();return
        if address==0x2481f: # 21b8:2c9f vertical wrapper
            x,y,y2=struct.unpack_from('<3i',args,4);self.rect(x,y,x,y2,self.color);self.ret();return
        if address==0x248f0: # 21b8:2d70 horizontal wrapper
            x,y,x2=struct.unpack_from('<3i',args,4);self.rect(x,y,x2,y,self.color);self.ret();return
        if not (0x21b8f<=address<0x24cfb):raise RuntimeError(f'Unhandled OG native callee {address:x}')
    def rect(self,x,y,x2,y2,color):
        x,x2=sorted((x,x2));y,y2=sorted((y,y2))
        for z in range(max(0,y),min(199,y2)+1):
            for a in range(max(0,x),min(319,x2)+1):self.frame[z*320+a]=color
    def draw(self,f):
        c=self.cpu;self.frame=bytearray(64000);self.color=0;self.ptr={};world=0x60000
        c.mem_write(world,bytes(0x800));c.mem_write(world,struct.pack('<3H',f['w'],0,f['h']));c.mem_write(world+0xc,struct.pack('<H',f['reveal']));c.mem_write(world+0x742,struct.pack('<i',f['w']))
        for i,o in enumerate(f['objects']):
            ptr=0x100+i*0x180;self.ptr[o['glyph']]=ptr;c.mem_write(0x70000+ptr,bytes.fromhex(o['bytes']));c.mem_write(world+0x30e+o['glyph']*4,struct.pack('<HH',ptr,0x7000))
        for i,plane in enumerate(f['maps']):c.mem_write(0x32550+0x6faa+i*4356,plane)
        c.mem_write(0x32550+0xa2b6,f['visited'])
        c.mem_write(0x32550+0xcae1,struct.pack('<HH',0,0x8000));c.mem_write(0x32550+0x1ffa,struct.pack('<HH',0,0x8000));c.mem_write(0x80000,struct.pack('<H',0x200));c.mem_write(0x32550+0x200+0x34,struct.pack('<HH',0x100,0x8000))
        for off,val in [(0x1fc6,0),(0x1fca,0),(0x1fce,319),(0x1fd2,169)]:c.mem_write(0x32550+off,struct.pack('<i',val))
        c.mem_write(0x5f000,struct.pack('<4H',0xfff0,0x2000,0,0x6000))
        for reg,val in [(R.UC_X86_REG_CS,0x2000),(R.UC_X86_REG_IP,0x2c94),(R.UC_X86_REG_DS,0x3255),(R.UC_X86_REG_SS,0x5000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)]:c.reg_write(reg,val)
        c.emu_start(0x22c94,0x2fff0,count=10000000);return bytes(self.frame)
def main():
    native=OldGold();results=[]
    for path in sorted((OUT/'host').glob('og-*.fixture')):
        f=fixture(path);expected=native.draw(f);actual=path.with_suffix('.idx').read_bytes();diff=[(i%320,i//320,a,b) for i,(a,b) in enumerate(zip(expected,actual)) if a!=b]
        path.with_suffix('.native.idx').write_bytes(expected);results.append(dict(case=path.stem,differences=len(diff),first=diff[:12]));print(path.stem,len(diff),diff[:4])
    import importlib.util
    spec=importlib.util.spec_from_file_location('original_x86_map',ROOT/'ports/gba/scripts/verify_automap_original.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    for language in ('ru','en'):
        native=module.OriginalMap(language)
        for path in sorted((OUT/'host').glob(language+'-*.fixture')):
            f=fixture(path);objects=[];player=0
            for o in f['objects']:
                entry=dict(glyph=o['glyph'],**{'class':o['cls']},marker=o['marker'],bytes=o['bytes'])
                if o['cls']==1:entry['actor']=dict(x=f['hx']*480,z=f['hz']*480,heading=f['heading']);player=len(objects)+1
                objects.append(entry)
            model=dict(objects=objects,player_index=player,maps=[p.hex() for p in f['maps']]);expected=native.draw(model,heading=f['heading'],revealed=bool(f['reveal']),visited=f['visited']);actual=path.with_suffix('.idx').read_bytes();diff=[(i%320,i//320,a,b) for i,(a,b) in enumerate(zip(expected,actual)) if a!=b]
            path.with_suffix('.native.idx').write_bytes(expected);results.append(dict(case=path.stem,differences=len(diff),first=diff[:12]));print(path.stem,len(diff),diff[:4])
    (OUT/'og-native-report.json').write_text(json.dumps(results,indent=2)+'\n');assert results and all(not r['differences'] for r in results)
if __name__=='__main__':main()
