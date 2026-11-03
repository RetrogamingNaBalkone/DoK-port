"""Independent byte-regex scanner for original LEVEL definitions.

Schema sources: docs/originals/levels.md and OG MZ Ghidra 1ae1:21ea,
1ae1:0699. This verifies extraction, not runtime behavior.
"""
import re

SURFACE=[('w',0x54),('i',0x56),('i',0x5a),('i',0x5e),('i',0x62)]
MOTION=[('l',0x38),('w',0x32),('w',0x34)]
SCHEMAS={
    0:SURFACE,1:MOTION+[('w',n) for n in range(0x5e,0x74,2)]+[('s',0xa2),('s',0xa6),('w',0x76),('w',0x78),('w',0x7a)],
    2:[],3:[('w',0x54),('w',0x56),('t',0x58)],4:[],5:MOTION,6:[],
    7:[('w',0x5c),('w',0x5e),('l',0x66)],
    8:[('l',0x3c),('w',0x5a),('a',0x77),('w',0x5c),('a',0x7b)],
    9:MOTION+[('a',0xe8),('a',0xec),('s',0xf0),('s',0xf4),('w',0xa4),('l',0xba),('c',0xe7),('w',0xa6),('w',0xa8),('w',0xae),('t',0xbe)],
    10:[('w',0x81),('w',0x54),('w',0x56),('t',0x58)],
    12:SURFACE+[('w',0x66),('w',0x68)],13:SURFACE+[('c',0x66),('l',0x67)]
}

def parse(data,name,old_gold=False):
    section=b'';settings={};maps={n:[] for n in (b'TMap',b'MMap',b'FCMap')}
    sprite=animation=sound=b'';sprite_flags=0;palette=light=b'';ignored=0;objects=[]
    for line in re.split(rb'\r\n|\r|\n',data):
        if not line or line.startswith(b'#'):continue
        if line.startswith(b'['):section=line[1:-1];continue
        if section in maps:maps[section].append(line);continue
        if section==b'Game Ini':
            key,val=line.split(b'=',1);key=key.strip()
            if key==b'Step Ver':ignored+=1
            else:settings[key]=int(val)
        elif section in (b'Palette',b'Light'):
            val=line.split(b'=',1)[1].lstrip(b' ')
            if section==b'Palette':palette=val
            else:light=val
        elif section==b'Game Object':
            if line.startswith(b'Sprite'):
                sprite,flags=line.split(b'=',1)[1].split();sprite_flags=int(flags);continue
            if line.startswith(b'Flic'):animation=line.split(b'=',1)[1].lstrip(b' ');continue
            if line.startswith(b'Sound'):sound=line.split(b'=',1)[1].lstrip(b' ');continue
            m=re.match(rb'(\S)\s+(\d+)\s+(\d+)\s+([+-]?\d+)\s+([+-]?\d+)\s+([+-]?\d+)',line)
            assert m,line
            glyph,cls,flags,width,height,marker=m.groups();cls,flags,width,height,marker=map(int,(cls,flags,width,height,marker))
            tail=line[m.end():];fields=[]
            schema=[('l',0x3c),('w',0x58),('w',0x5a)] if old_gold and cls==8 else SCHEMAS[cls]
            for kind,offset in schema:
                if kind in ('w','l'):
                    m=re.match(rb'\s*([+-]?\d+)',tail);assert m
                    val=int(m[1]);tail=tail[m.end():]
                elif kind=='c':tail=tail.lstrip();val=tail[0];tail=tail[1:]
                elif kind=='t':val=tail[:39].hex();tail=b''
                else:
                    m=re.match(rb'\s*(\S+)',tail);assert m
                    val=m[1].hex();tail=tail[m.end():]
                fields.append(['wlciast'.index(kind),offset,val])
            assert not tail.strip() or (old_gold and cls==12),tail
            objects.append(dict(glyph=32 if glyph==b'_' else glyph[0],**{'class':cls},flags=flags,width=width,height=height,marker=marker&255,sprite_flags=sprite_flags,sprite=sprite.hex(),animation=animation.hex(),sound=sound.hex(),fields=fields))
    keys=(b'Level Width',b'Level Height',b'Level Length',b'Cell Side',b'Cell Height',b'Floor Color',b'Ceiling Color',b'Min Dis')
    return dict(name=name,settings=[settings[k] for k in keys],palette=palette.hex(),light=light.hex(),ignored_step_ver=ignored,objects=objects,maps=[b''.join(maps[n]).hex() for n in (b'TMap',b'MMap',b'FCMap')])
