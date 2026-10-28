"""Execute the original central-plane constructor with both reflection inputs.

Only its array initializer is bypassed: the caller supplies a zeroed record.
The original vertex, projection, reflection and return instructions execute.
"""

import sys as _layout_sys
from pathlib import Path as _LayoutPath
_layout_root = _LayoutPath(__file__).resolve().parents[3]
for _layout_dir in ("scripts", "tools/originals", "tools/reports", "ports/gba/scripts"):
    _layout_sys.path.append(str(_layout_root / _layout_dir))

import hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,'C:/Source/wasdify/build/research-deps')
from unicorn import Uc,UC_ARCH_X86,UC_MODE_16,UC_HOOK_CODE
from unicorn import x86_const as R

sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
    cases=[];evidence=[]
    for lang,segment,delta,focal,helper in [('ru',0x10b0,0,0x934,0x50),('en',0x1040,0xb0,0xa44,0x56)]:
        root=Path('build/gba/g20b-playtest2/original')/lang
        manifest=json.loads((root/'manifest.json').read_text())
        assert sha(manifest['sample'])==manifest['sha256_before']==manifest['sha256_after']
        c=Uc(UC_ARCH_X86,UC_MODE_16);c.mem_map(0,0x100000);c.mem_write(0x50000,(root/'center.bin').read_bytes())
        def hook(cpu,address,size,data):
            if address==0x50000+helper:cpu.reg_write(R.UC_X86_REG_IP,helper+5)
        c.hook_add(UC_HOOK_CODE,hook)
        def put(off,fmt,*v):c.mem_write(0x30000+off,struct.pack(fmt,*v))
        put(0xaa16+delta,'<i',200);put(0xaa1e+delta,'<hh',159,79)
        put(0xaa22+delta,'<4i',0,0,319,159);put(0xaa32+delta,'<h',8);put(focal,'<h',4096)
        for theta in (0,90):
            for heading,sine,cosine in ((0,0,32768),(90,32768,0),(180,0,-32768),(270,-32768,0)):
                # Camera looks towards the origin from outside the door.
                put(0xaa3e+delta,'<5i',sine,cosine,sine*2000//32768,-300,-cosine*2000//32768)
                records=[]
                for reflected in (0,1):
                    c.mem_write(0x40100,bytes(0x80));c.mem_write(0x40200,struct.pack('<iii',0,0,0))
                    c.mem_write(0x40400,struct.pack('<HH',96,128))
                    args=[0xfff0,0x5000,0x100,0x4000,0x200,0x4000,640,480,theta,0,0x400,0x4000,123,reflected]
                    c.mem_write(0x3f000,struct.pack('<14H',*args))
                    for reg,val in ((R.UC_X86_REG_DS,0x3000),(R.UC_X86_REG_SS,0x3000),(R.UC_X86_REG_CS,0x5000),(R.UC_X86_REG_SP,0xf000),(R.UC_X86_REG_BP,0xf800)):
                        c.reg_write(reg,val)
                    c.emu_start(0x50000,0x5fff0,count=100000)
                    assert c.reg_read(R.UC_X86_REG_SP)==0xf004
                    record=bytes(c.mem_read(0x40100,0x4f));assert record[6]&1
                    records.append(struct.unpack_from('<12i',record,0x13))
                a,b=records
                assert b[:2]==a[3:5] and b[3:5]==a[:2] and b[6:8]==a[9:11] and b[9:11]==a[6:8],(lang,theta,heading,a,b)
                assert b[2::3]==a[2::3], 'Native keeps its depth fields in original endpoint order'
                cases.append(dict(language=lang,theta=theta,heading=heading,unreflected_vertices=a,reflected_vertices=b))
        evidence.append(manifest)
    result=dict(status='passed',script_sha256=sha(__file__),original_constructor_cases=cases,evidence=evidence,
        conclusion='Native final argument reflects each placed panel; texture crop alone does not determine its world direction',
        full_dos_session=False)
    Path('build/gba/g20b-playtest2/door-original.json').write_text(json.dumps(result,indent=2)+'\n')
    print('Original x86 central-plane reflection:16 cases passed')
if __name__=='__main__':main()
