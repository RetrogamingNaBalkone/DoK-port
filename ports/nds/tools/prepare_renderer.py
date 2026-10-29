"""N07 diagnostic camera/HUD input and checked mathematical trig constants.

Never bakes world geometry or textures into the ROM/fixture. The runtime reads
the accepted N04 package and constructs original-backed surfaces itself.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'build/nds/n07'
EVIDENCE=ROOT/'research/evidence/nds/n07'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pin(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=sha(data))


def safe_output(path):
    path=Path(path)
    if '..' in path.parts:
        raise ValueError('Parent traversal in N07 output')
    path=path.absolute()
    if not any(path.is_relative_to(root) and path!=root for root in (OUT,EVIDENCE)):
        raise ValueError('N07 output escapes fixed roots')
    current=ROOT
    for part in path.parent.relative_to(ROOT).parts:
        current/=part
        if current.is_symlink() or (hasattr(current,'is_junction') and current.is_junction()):
            raise ValueError('N07 output directory alias')
        if current.exists() and not current.is_dir():
            raise ValueError('N07 output parent is not directory')
    if path.is_symlink() or (hasattr(path,'is_junction') and path.is_junction()) or (path.exists() and (not path.is_file() or path.stat().st_nlink!=1)):
        raise ValueError('N07 output file alias')
    path.parent.mkdir(parents=True,exist_ok=True)
    return path


def checked(path,pin_value):
    data=path.read_bytes()
    if sha(data)!=pin_value:
        raise ValueError(f'Accepted source changed: {path}')
    return data


def make_case(ordinal,name,level,x,z,heading,*,frames=32,flags=0,dx=0,dz=0,dheading=0,kind=0):
    values=(ordinal,level,x,-300,z,heading,dx,dz,dheading,frames,flags,kind,x//480,z//480,0,0)
    if not 1<=level<=3 or not 0<=heading<=360 or not 1<=frames<=360:
        raise ValueError('Diagnostic case range')
    raw=struct.pack('<16I',*(v&0xffffffff for v in values))
    return raw,dict(id=ordinal,name=name,level=level,x=x,y=-300,z=z,heading=heading,
                    dx=dx,dz=dz,dheading=dheading,frames=frames,flags=flags,
                    source_kind=kind,cell_x=x//480,cell_z=z//480,
                    flat=bool(flags&1),moving=bool(flags&2))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provisional',action='store_true',help='bootstrap only, not final heavy-scene acceptance')
    args=parser.parse_args()
    outputs=[OUT/'generated/render_trig.inc',OUT/'scene.dat',OUT/'scene-manifest.json',EVIDENCE/'fixture-report.json']
    for path in outputs:
        safe_output(path)
    source_pins=[]
    ram_path=ROOT/'research/evidence/reference/ru-03/level1-memory.bin'
    camera_report=json.loads((ROOT/'research/evidence/nds/n06/camera-audit.json').read_text('utf8'))
    ram_pin=next(item for item in camera_report['inputs'] if item['path']==ram_path.relative_to(ROOT).as_posix())
    raw=checked(ram_path,ram_pin['sha256']);source_pins.append(pin(ram_path))
    words=struct.unpack_from('<722i',raw,0x1ca9b0)
    generated=tuple(math.trunc(fn(n*3.14159265359/180)*32768) for fn in (math.sin,math.cos) for n in range(361))
    if words!=generated:
        raise ValueError('Mathematical trig disagrees with original 722 runtime words')
    text='/* N07: mathematical trig generator checked against all 722 RU runtime words. */\n'
    for name,values in (('sine',words[:361]),('cosine',words[361:])):
        text+=f'static const int32_t dok_render_{name}[361] = {{\n'
        for i in range(0,361,12):
            text+='    '+','.join(str(v) for v in values[i:i+12])+',\n'
        text+='};\n'
    cases=[]
    def add(*positional,**keyword):
        cases.append(make_case(len(cases)+1,*positional,**keyword))
    levels=[]
    n06=json.loads((ROOT/'analysis/nds-n06-visual.json').read_text('utf8'))
    source_pins.append(pin(ROOT/'analysis/nds-n06-visual.json'))
    for level in (1,2,3):
        directory=ROOT/f'research/evidence/nds/n06/dos/ru/level{level}-clean'
        rpath=directory/'report.json'
        r=json.loads(rpath.read_text('utf8'));source_pins.append(pin(rpath))
        if r['status']!='passed' or not r['originals_unchanged'] or r['clean']['attempt_counter']!=2:
            raise ValueError('Clean original PLAY not established')
        dac=checked(directory/'play-clean.vga6',r['clean']['files']['vga6']['sha256'])
        source_pins.append(pin(directory/'play-clean.vga6'))
        palette=[(dac[i]>>1)|((dac[i+1]>>1)<<5)|((dac[i+2]>>1)<<10) for i in range(0,768,3)]
        frame_name=f'build/nds/n06/frames/v{(level-1)*2:02}top.bin'
        frame_pin=next(row for row in n06['artifacts'] if row['path']==frame_name)
        frame=checked(ROOT/frame_name,frame_pin['sha256']);source_pins.append(pin(ROOT/frame_name))
        colors=struct.unpack('<9728H',frame[154*512:])
        reverse={value:i for i,value in enumerate(palette) if i!=0}
        hud=bytes(reverse[word&32767] for word in colors)
        if not all(hud) or any(palette[index]!=(word&32767) for index,word in zip(hud,colors)):
            raise ValueError('HUD remap changed color or introduced transparent pixels')
        levels.append(struct.pack('<256H',*palette)+hud)
        x,y,z=r['clean']['state_before']['camera_xyz']
        if y!=-300:
            raise ValueError('Unexpected original camera height')
        add(f'level{level}-start-textured',level,x,z,90,kind=1)
        add(f'level{level}-start-flat',level,x,z,90,flags=1,kind=1)
    add('level1-normal-to-low',1,19440,1680,0,frames=64,flags=2,dz=8,kind=2)
    add('level1-low-flat',1,19440,2160,180,flags=1,kind=2)
    add('level2-low-to-normal',2,19920,1200,0,frames=64,flags=2,dz=8,kind=2)
    add('level2-low-flat',2,19920,1200,0,flags=1,kind=2)
    # Bootstrap candidates are independently selected map-data proxies. The
    # final manifest must use measured emitted/clipped-scene ranking instead.
    candidates=[(16,21,0),(30,50,0),(10,45,0)]
    ranking=EVIDENCE/'scene-host-report.json'
    if not args.provisional:
        rank=json.loads(ranking.read_text('utf8'))
        if not rank.get('passed') or not rank.get('heavy'):
            raise ValueError('Actual scene ranking missing')
        candidates=[(row['x']//480,row['z']//480,row['heading']) for row in rank['heavy']]
        if len(candidates)!=3:
            raise ValueError('Heavy ranking must cover all three levels')
        source_pins.append(pin(ranking))
    for level,(x,z,heading) in enumerate(candidates,1):
        x,z=x*480+240,z*480+240
        add(f'level{level}-heavy-textured',level,x,z,heading,kind=3)
        add(f'level{level}-heavy-flat',level,x,z,heading,flags=1,kind=3)
        add(f'level{level}-heavy-turn',level,x,z,heading,frames=120,flags=2,dheading=3,kind=3)
    body=b''.join(item[0] for item in cases)+b''.join(levels)
    header=b'N07SCN1\0'+struct.pack('<6I',1,len(cases),64,10240,64+len(body),0)+hashlib.sha256(body).digest()
    if len(header)!=64 or len(header+body)>65536 or len(cases)>64:
        raise ValueError('Diagnostic input capacity')
    # All paths and all inputs have been validated before publication.
    outputs[0].write_text(text,encoding='ascii')
    outputs[1].write_bytes(header+body)
    manifest=dict(task='N07',passed=True,provisional=args.provisional,
                  scope='diagnostic cameras/HUD only; runtime builds world geometry from N04/N05',
                  packet=pin(outputs[1]),record_bytes=64,level_bytes=10240,header_bytes=64,
                  cases=[item[1] for item in cases],sources=source_pins,
                  trig=dict(words=722,ram_linear_address='0x1CA9B0',generated=pin(outputs[0])),
                  palette='measured N06 clean DAC6 >>1, unchanged indices; HUD uses nonzero equal-color aliases')
    outputs[2].write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    outputs[3].write_text(json.dumps(dict(task='N07',passed=True,provisional=args.provisional,
                          manifest=pin(outputs[2]),packet=pin(outputs[1]),trig=pin(outputs[0])),indent=2)+'\n',encoding='utf8')
    print(json.dumps(dict(passed=True,provisional=args.provisional,cases=len(cases),bytes=64+len(body))))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
