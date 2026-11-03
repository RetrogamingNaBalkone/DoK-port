"""Compare native decoders with original bytes and accepted LEVEL evidence."""
import hashlib
import itertools
import json
import struct
import subprocess
import re
from pathlib import Path
from level_reference import parse as reference_level

ROOT=Path(__file__).resolve().parents[3]
EXE=ROOT/'build/sdl2/windows/dok_inspect.exe'
OUT=ROOT/'build/sdl2/checks'
PROFILES={'og':'originals/Old_Gold_3D/OLD_GOLD.RES','ru':'originals/pkr/RUS/PKR.RES','en':'originals/pkr/ENG/PKE.RES'}

def fnv(data):
    value=2166136261
    for b in data: value=((value^b)*16777619)&0xffffffff
    return value

def read_archive(data):
    entries=[]
    for i in range(struct.unpack_from('<H',data)[0]):
        at=2+i*30;size,offset=struct.unpack_from('<II',data,at+16)
        entries.append((data[at:at+16].split(b'\0')[0],data[offset:offset+size]))
    return entries

def native(path):
    return json.loads(subprocess.check_output([str(EXE),'--json',str(path)]))

def expected_assets(entries):
    assets=[]
    def image(b):
        w,h,flags=struct.unpack_from('<HHH',b)
        assert len(b)==18+w*h and struct.unpack_from('<I',b,10)[0]==w*h
        return [w,h,flags,fnv(b[18:])]
    for name,b in entries:
        kind=None;value=None
        if len(b)==768:
            assert max(b)<=63
            kind='palette';value=[]
            for i in range(0,768,3):
                r,g,blue=((v<<2)|(v>>4) for v in b[i:i+3]);value.append(0xff000000|(r<<16)|(g<<8)|blue)
        elif name in (b'FONT8',b'FONT16'):
            assert len(b)==(2048 if name==b'FONT8' else 4096)
            kind='font';value=[len(b),fnv(b)]
        elif name.startswith(b'LIGHT'):
            assert len(b)==4096
            kind='light';value=[len(b),fnv(b)]
        if kind is None and len(b)>=18:
            w,h=struct.unpack_from('<HH',b)
            if w and h and w*h==len(b)-18 and struct.unpack_from('<I',b,10)[0]==w*h:
                kind='image';value=image(b)
        if kind is None and b[2:5]==b'SVM':
            kind='sound';freq,count=struct.unpack_from('<II',b,16);assert count==len(b)-36
            value=[freq,*struct.unpack_from('<HH',b,12),count,fnv(b[36:])]
        if kind is None and len(b)>=8:
            count=struct.unpack_from('<H',b)[0];at=8+10*count
            if 0<count<=32767 and at<=len(b):
                frames=[]
                try:
                    for i in range(count):
                        size=18+struct.unpack_from('<I',b,at+10)[0]
                        frames.append([*struct.unpack_from('<HHH',b,8+10*i),image(b[at:at+size])]);at+=size
                    if at==len(b):kind='animation';value=frames
                except (AssertionError,struct.error):pass
        if kind:assets.append({'name':name.hex(),'kind':kind,'data':value})
    return assets

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    report={'profiles':{},'combination_checks':0,'mutations':0}
    for key,path in PROFILES.items():
        p=ROOT/path;data=p.read_bytes();before=hashlib.sha256(data).hexdigest();entries=read_archive(data)
        actual=native(p);assert actual['assets']==expected_assets(entries),key
        expected_levels=[reference_level(payload,name.decode('ascii'),key=='og') for name,payload in entries if name.startswith(b'LEVEL')]
        level_only=[{k:v for k,v in l.items() if k!='world'} for l in actual['levels']]
        assert level_only==expected_levels,(key,'independent LEVEL byte scanner')
        for level,reference in zip(actual['levels'],expected_levels):
            players=[(i,o) for i,o in enumerate(reference['objects']) if o['class']==1]
            assert len(level['world']['players'])==len(players)
            for state,(index,definition) in zip(level['world']['players'],players):
                words={field[1]:field[2] for field in definition['fields'] if field[0]==0}
                assert state==dict(owner=index+1,glyph=definition['glyph'],
                    attributes=[words[offset] for offset in range(0x5e,0x74,2)],
                    weapons=[0]*9,lives=3,score=0,current=0,pending=0,
                    events=[[416,416,1,0]]),(key,level['name'],'player initialization')
        counts={kind:sum(a['kind']==kind for a in actual['assets']) for kind in ('image','animation','sound')}
        assert counts==({'image':49,'animation':12,'sound':10} if key=='og' else {'image':77,'animation':24,'sound':23}),counts
        if key!='og':
            for i,l in enumerate(actual['levels']):
                ref=json.loads((ROOT/f'research/evidence/levels/{key}/{18+i:04d}.json').read_text(encoding='utf-8'))
                ref.pop('truncated_messages');value=dict(l);value.pop('name');value.pop('world')
                assert value==ref,(key,l['name'])
                world_ref=json.loads((ROOT/f'analysis/originals/worlds-{key}.json').read_text(encoding='utf-8'))[i]
                assert l['world']['actors']==world_ref['actors'],(key,l['name'],'world actors')
                assert l['world']['money']==world_ref['pickup_99_total']
                for n,blob in zip(('TMap','MMap','FCMap'),l['world']['maps']):
                    assert hashlib.sha256(bytes.fromhex(blob)).hexdigest()==world_ref['maps'][n]
        else:
            assert len(actual['levels'])==1 and len(actual['levels'][0]['objects'])==119
            assert actual['levels'][0]['settings']==[64,1,64,336,672,10,143,165]
            assert actual['levels'][0]['world']['money']==105
        # Permute payloads, move directory entries, discard cache/name garbage and append junk.
        entries.reverse();mutated=bytearray(2+30*len(entries));struct.pack_into('<H',mutated,0,len(entries))
        for i,(name,payload) in enumerate(entries):
            at=2+i*30;mutated[at:at+len(name)]=name
            struct.pack_into('<II',mutated,at+16,len(payload),len(mutated)+7)
            mutated+=b'padding'+payload
        mutated+=b'ignored trailing bytes'
        mutation=OUT/f'{key}-relocated.res';mutation.write_bytes(mutated)
        changed=native(mutation)
        assert sorted(changed['assets'],key=lambda x:x['name'])==sorted(actual['assets'],key=lambda x:x['name'])
        assert sorted(changed['levels'],key=lambda x:x['name'])==sorted(actual['levels'],key=lambda x:x['name'])
        for broken in (data[:1],data[:20],data[:-100]):
            mutation.write_bytes(broken)
            r=subprocess.run([str(EXE),str(mutation)],capture_output=True)
            assert r.returncode!=0
        # Same-size changes preserve profile identity and must fail in LEVEL validation.
        for resource_name,payload in entries:
            if not resource_name.startswith(b'LEVEL'):continue
            directory_index=next(i for i in range(struct.unpack_from('<H',data)[0]) if data[2+30*i:18+30*i].split(b'\0')[0]==resource_name)
            offset=struct.unpack_from('<I',data,2+30*directory_index+20)[0]
            sprite_match=re.search(rb'Sprite\s*=\s*(\S+)',payload);assert sprite_match
            size_match=re.search(rb'Level Width\s*=\s*(64)',payload);assert size_match
            variants=[(sprite_match.start(1),b'?'),(size_match.start(1),b'63')]
            for where,replacement in variants:
                broken=bytearray(data);broken[offset+where:offset+where+len(replacement)]=replacement
                mutation.write_bytes(broken)
                r=subprocess.run([str(EXE),'--json',str(mutation)],capture_output=True)
                assert r.returncode!=0,(key,resource_name,'invalid LEVEL accepted')
                report['mutations']+=1
        mutation.unlink();report['mutations']+=4
        assert hashlib.sha256(p.read_bytes()).hexdigest()==before
        all_counts={kind:sum(a['kind']==kind for a in actual['assets']) for kind in ('image','animation','sound','palette','font','light')}
        report['profiles'][key]={'sha256':before,'assets':all_counts,'levels':len(actual['levels']),
            'objects':sum(len(l['objects']) for l in actual['levels']),
            'level_evidence':'all fields/maps vs byte scanner and original stage5 evidence' if key!='og' else 'all fields/maps vs independent byte scanner; runtime equivalence not claimed'}
    for count in (1,2,3):
        for paths in itertools.combinations(PROFILES.values(),count):
            subprocess.run([str(EXE),*(str(ROOT/p) for p in paths)],check=True,stdout=subprocess.DEVNULL)
            report['combination_checks']+=1
    (OUT/'assets-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
