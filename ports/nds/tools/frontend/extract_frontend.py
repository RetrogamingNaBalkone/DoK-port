"""N21 offline metadata from unchanged PKR/PKE RES and saved Ghidra exports.

No GBA engine imports. The seven full-screen images stay in the accepted N04
package; frontend.dat contains only original text and background asset IDs.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
from pack_assets import read_sources, asset_id, image
from layout_ui import Assets, paginate, PACKAGE_SHA

OUT = ROOT / 'build/nds/n21'
EVIDENCE = ROOT / 'research/evidence/nds/n21'
TEXT_NAMES = ('README','BINTRO1','EINTRO1','BINTRO2','EINTRO2','BINTRO3','EINTRO3','ENDTXT')
BACKGROUNDS = ('ЗАСТАВКА_СПР','BSPR1','ESPR1','BSPR2','ESPR2','BSPR3','ESPR3','FON')
EXE = {
    1: ('ru','originals/pkr/RUS/PKR.EXE','12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28',51,'1190',(0x191,0x1a0,0x1af)),
    2: ('en','originals/pkr/ENG/PKE.EXE','cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b',27,'10d0',(0x192,0x1a1,0x1ae)),
}

def sha(data): return hashlib.sha256(data).hexdigest()

def pin(path):
    path = Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=sha(path.read_bytes()))

def write(path, data):
    path = Path(path)
    if not any(path.resolve().is_relative_to(p.resolve()) for p in (OUT,EVIDENCE)):
        raise ValueError('Output outside N21 local trees')
    for ancestor in (path,*path.parents):
        if ancestor == ROOT.parent: break
        if ancestor.is_symlink() or ancestor.is_junction(): raise ValueError('Aliased output')
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1): raise ValueError('Aliased output')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data)

def native_constants(language):
    label,relative,expected,index,segment,offsets = EXE[language]
    sample = ROOT / relative
    before = sha(sample.read_bytes())
    if before != expected: raise ValueError('Original EXE hash')
    source = EVIDENCE / 'original' / label
    manifest = json.loads((source/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
    if manifest['sha256_before'] != before or manifest['sha256_after'] != before: raise ValueError('Ghidra provenance')
    if manifest.get('task') != 'N21' or manifest.get('language') != label or manifest.get('sample') != relative or not manifest.get('read_only'):
        raise ValueError('Wrong frontend Ghidra manifest identity')
    for field in ('script','driver'):
        if sha((ROOT/manifest[field]).read_bytes()) != manifest[field+'_sha256']:
            raise ValueError('Changed frontend export tooling')
    if not {'1008.bin',f'{segment}.bin','evidence.asm'}.issubset(manifest['exports']):
        raise ValueError('Missing executed frontend source export')
    for name,digest in manifest['exports'].items():
        if sha((source/name).read_bytes()) != digest: raise ValueError('Changed Ghidra export')
    data = (source/f'{segment}.bin').read_bytes()
    exe = sample.read_bytes()
    ne = struct.unpack_from('<I',exe,0x3c)[0]
    table,shift = (struct.unpack_from('<H',exe,ne+n)[0] for n in (0x22,0x32))
    base = struct.unpack_from('<H',exe,ne+table+(index-1)*8)[0] << shift
    names = []
    for level,offset in enumerate(offsets,1):
        end = data.index(0,offset)
        span = data[offset:end]
        if exe[base+offset:base+end+1] != span+b'\0': raise ValueError('Ghidra/raw EXE constant')
        names.append(dict(level=level,ghidra_address=f'{segment}:{offset:04X}',ne_segment_index=index,
                          file_offset=base+offset,bytes=len(span),sha256=sha(span),cp866_hex=span.hex(),text=span.decode('cp866')))
    asm = (source/'evidence.asm').read_text(encoding='utf8')
    title = '1008:01ba' if language==1 else '1008:023e'
    selector = '1008:02f7' if language==1 else '1008:044b'
    stop = 'CALLF 0x1108:026d' if language==1 else 'CALLF 0x1060:17b3'
    once = 'CALLF 0x1108:01f7' if language==1 else 'CALLF 0x1060:173d'
    body = asm[asm.index('FUNCTION '+title):asm.index('FUNCTION '+selector)]
    if once not in body or stop not in body or body.index(once)>body.index(stop) or 'RETF' not in body:
        raise ValueError('Title once/stop ASM')
    after = sha(sample.read_bytes())
    if before != after: raise ValueError('Original EXE changed')
    return names,dict(language=language,sample=relative,sha256_before=before,sha256_after=after,
        project=manifest['project'],new_ghidra_analysis=False,fresh_read_only_export=pin(source/'analysis-manifest.json'),
        code=pin(source/'1008.bin'),asm=pin(source/'evidence.asm'),data=pin(source/f'{segment}.bin'),address_system='Ghidra NE loader, not file offset/NE segment index/DPMI selector',
        title_function=title,level_selector_function=selector,title_play_once_then_stop=True,far_pointers_offset_segment=True,caller_cleanup=True)

def build():
    archives = read_sources()
    packaged = Assets()
    records, rows, originals = [], [], []
    for archive in archives:
        lang = archive['language']
        resources = {e['name']:e for e in archive['entries']}
        names,evidence = native_constants(lang)
        originals.append(evidence)
        for text_index,name in enumerate(TEXT_NAMES):
            entry = resources[name]
            payload = entry['payload']
            if packaged.get(lang,name,16)['payload'] != payload: raise ValueError('N04 text mismatch')
            plan = paginate(payload,30,9)
            records.append((1,lang*16+text_index,payload))
            rows.append(dict(kind='text',language=lang,text_index=text_index,name=name,source_index=entry['index'],
                file_offset=entry['offset'],bytes=len(payload),sha256=sha(payload),cp866_hex=payload.hex(),
                text=payload.decode('cp866'),plan=plan))
        for row in names:
            records.append((2,lang*16+row['level'],bytes.fromhex(row['cp866_hex'])))
            rows.append(dict(kind='level_name',language=lang,**row))
        for bg_index,name in enumerate(BACKGROUNDS):
            source = resources[name]
            palette_name = 'ЗАСТАВКА_ПАЛ' if bg_index==0 else 'МЕНЮ_ПАЛ' if bg_index==7 else 'GAMEP'+name[-1]
            palette = resources[palette_name]
            rep = 2 if bg_index==7 else 6
            imid = asset_id(lang,source['index'],rep)
            palid = asset_id(lang,palette['index'],7)
            entry = packaged.get(lang,name,rep)
            if entry['id'] != imid: raise ValueError('N04 image ID')
            raw = image(source['payload'])
            if rep==6:
                expected = bytes(raw['pixels'][((2*y+1)*200//384)*320+((2*x+1)*320//512)] for y in range(192) for x in range(256))
                if expected != entry['payload'] or entry['param0'] != palid: raise ValueError('N06 corrected static image')
            else:
                if entry['payload'] != raw['pixels']: raise ValueError('Original FON tile')
            records.append((3,lang*16+bg_index,struct.pack('<II',imid,palid)))
            rows.append(dict(kind='background',language=lang,background_index=bg_index,name=name,source_index=source['index'],
                file_offset=source['offset'],source_bytes=len(source['payload']),source_sha256=source['sha'].hex(),
                image_id=imid,palette_id=palid,representation=rep,width=entry['width'],height=entry['height'],
                pixels_sha256=entry['sha256'],palette_name=palette_name,palette_source_index=palette['index'],
                palette_source_sha256=palette['sha'].hex(),palette_file_offset=palette['offset']))
        after = sha((ROOT/archive['path']).read_bytes())
        if after != archive['sha'].hex(): raise ValueError('Original RES changed')
        originals.append(dict(language=lang,sample=archive['path'],sha256_before=archive['sha'].hex(),sha256_after=after))
    count = len(records)
    blob = bytearray(32+16*count)
    for i,(kind,key,data) in enumerate(records):
        struct.pack_into('<4I',blob,32+16*i,kind,key,len(blob),len(data))
        blob.extend(data)
    struct.pack_into('<8s6I',blob,0,b'N21UI01\0',1,count,32,len(blob),0,0)
    destination = OUT/'frontend.dat'
    write(destination,blob)
    include = ('/* Generated locally from unchanged original assets; do not commit. */\n'
               f'#define DOK_FRONTEND_FILE_BYTES {len(blob)}u\n'
               f'#define DOK_FRONTEND_FILE_ENTRIES {count}u\n'
               'static const unsigned char dok_frontend_file_sha256[32] = {'+
               ','.join(f'0x{byte:02x}' for byte in hashlib.sha256(blob).digest())+'};\n')
    digest_file = OUT/'generated/frontend_hash.inc'
    write(digest_file,include.encode('ascii'))
    report = dict(task='N21',passed=True,scope='Original assets and ASM constants only; no frontend runtime acceptance',
        inputs=[pin(Path(__file__)),pin(ROOT/'build/nds/assets/dok-assets.ndspak')],package_sha256=PACKAGE_SHA,
        originals=originals,rows=rows,output=pin(destination),generated_hash=pin(digest_file),format=dict(header_bytes=32,entry_bytes=16,
        entries=count,kinds={'1':'Original text bytes, id=16*language+textIndex','2':'Original level name, id=16*language+level',
        '3':'Two u32 N04 asset IDs: INDEX8 image and RGB555 palette; id=16*language+backgroundIndex'}),
        adaptations=['Full-screen 320x200 images center-nearest scaled offline to256x192 perN06; allsource pixels remain inRAW_SOURCE',
        'Main menu/pause/language/story item are NDS interface hierarchy; not claims about DOS menu',
        'Original FON46x46 stays a small tile; runtime repeats immutable source pixels only on changed screens'],
        limits=['No new original DOS process capture','No physical DS review','Original asset payload stays local'])
    write(EVIDENCE/'assets.json',(json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
    print(f'N21 frontend metadata: {count} records, {len(blob)} bytes, SHA256 {sha(blob)}')

if __name__=='__main__': build()
