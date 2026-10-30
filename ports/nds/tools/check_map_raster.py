"""N20 pixel oracle: original x86 versus independent NDS raster on host."""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import json
import os
import random
import struct
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(Path(__file__).parent / 'map'))
from original_map import OriginalMap

class Glyph(C.Structure):
    _fields_ = [('class_id', C.c_uint16), ('marker', C.c_uint8), ('monster_state', C.c_uint8)]
class Snapshot(C.Structure):
    _fields_ = [('maps', C.POINTER(C.c_uint8) * 3), ('visited', C.POINTER(C.c_uint8)),
                ('glyphs', C.POINTER(Glyph)), ('width', C.c_uint16), ('length', C.c_uint16),
                ('hero_x', C.c_int16), ('hero_z', C.c_int16), ('heading', C.c_uint16),
                ('map_available', C.c_uint8)]

def from_model(model):
    hero = model['objects'][model['player_index'] - 1]['actor']
    return dict(width=64, length=64, maps=[bytes.fromhex(p) for p in model['maps'][:3]],
        visited=bytes([1] * 4096), hero=dict(x=hero['x'] // 480, z=hero['z'] // 480, heading=hero['heading']),
        map_available=False, objects=[dict(glyph=o['glyph'], class_id=o['class'], marker=o['marker'],
        monster_state=struct.unpack_from('<H',bytes.fromhex(o['bytes']),0xa2)[0]) for o in model['objects']])

def actual(dll, s, scale, left, top):
    planes = [(C.c_uint8 * len(p)).from_buffer_copy(p) for p in s['maps']]
    visited = (C.c_uint8 * len(s['visited'])).from_buffer_copy(s['visited'])
    glyphs = (Glyph * 256)()
    for o in s['objects']:
        glyphs[o['glyph']] = Glyph(o['class_id'], o['marker'], o['monster_state'])
    state = Snapshot((C.POINTER(C.c_uint8) * 3)(*planes), visited, glyphs, s['width'], s['length'],
                     s['hero']['x'], s['hero']['z'], s['hero']['heading'], s['map_available'])
    out = (C.c_uint8 * (40960+32))(*([0xa5]*(40960+32)))
    assert dll.dok_map_raster(out, C.byref(state), scale, -left, -top) == 1
    assert bytes(out)[40960:] == bytes([0xa5] * 32), 'button strip clobbered'
    return bytes(out)[:40960]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default='research/evidence/nds/n20/raster-check.json')
    ap.add_argument('--quick', action='store_true')
    args = ap.parse_args()
    build = ROOT / 'build/nds/n20/host'
    build.mkdir(parents=True, exist_ok=True)
    path = build / 'map-raster.dll'
    zig = Path('C:/Users/dspev/AppData/Local/Programs/Zig/0.13.0/zig.exe')
    subprocess.run([str(zig),'cc','-shared','-O2','-Wall','-Wextra','-Werror','-Iports/nds/include',
                    'ports/nds/ui/map_raster.c','-o',str(path)],cwd=ROOT,check=True,env=dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache')))
    dll = C.CDLL(str(path))
    dll.dok_map_raster.argtypes = [C.POINTER(C.c_uint8), C.POINTER(Snapshot), C.c_uint, C.c_int32, C.c_int32]
    rows=[]
    for lang in ('ru','en'):
        native = OriginalMap(lang)
        for level in (1,2,3):
            s = from_model(json.loads((ROOT / f'research/evidence/runtime/{lang}/LEVEL{level}-runtime.json').read_text()))
            for scale in ((3,) if args.quick else (2,3,4,6,8)):
                for reveal in (False,True):
                    s['map_available']=reveal
                    left, top = max(0,64*scale-256)//2, max(0,64*scale-160)//2
                    expected = native.draw(s,scale,left,top)
                    got = actual(dll,s,scale,left,top)
                    errors=[(i%256,i//256,a,b) for i,(a,b) in enumerate(zip(expected,got)) if a!=b]
                    row=dict(language=lang,level=level,scale=scale,map_available=reveal,left=left,top=top,
                             different_pixels=len(errors),first_errors=errors[:12],sha256=hashlib.sha256(expected).hexdigest())
                    rows.append(row)
                    print(lang,level,scale,reveal,len(errors),errors[:4],flush=True)
                    if errors:
                        (build/'expected.idx').write_bytes(expected);(build/'actual.idx').write_bytes(got)
                        raise AssertionError(row)
    # Small synthetic worlds isolate every class, direct/fallback map choice,
    # crossed door neighbours, source order of hero visited, corpse gating,
    # native FCMap exclusions, all angle cutoffs, clipping and non-square sizes.
    if not args.quick:
        def compare(native, lang, state, scale, left, top, role):
            expected = native.draw(state, scale, left, top)
            got = actual(dll, state, scale, left, top)
            errors = [(i%256,i//256,a,b) for i,(a,b) in enumerate(zip(expected,got)) if a != b]
            row = dict(language=lang, role=role, scale=scale, left=left, top=top,
                       different_pixels=len(errors), first_errors=errors[:12],
                       sha256=hashlib.sha256(expected).hexdigest())
            rows.append(row)
            if errors:
                (build/'expected.idx').write_bytes(expected)
                (build/'actual.idx').write_bytes(got)
                raise AssertionError(row)
        for lang in ('ru','en'):
            native = OriginalMap(lang)
            objects = [dict(glyph=32,class_id=0,marker=4,monster_state=0),
                       dict(glyph=219,class_id=0,marker=5,monster_state=0),
                       dict(glyph=100,class_id=0,marker=77,monster_state=0)]
            objects += [dict(glyph=50+i,class_id=i,marker=120+i,monster_state=0) for i in range(14)]
            objects += [dict(glyph=90,class_id=9,marker=199,monster_state=4)]
            rng=random.Random(20261029)
            for variant in range(4):
                width,length=16,13
                planes=[bytearray(width*length) for _ in range(3)]
                for i in range(width*length):
                    planes[0][i] = rng.choice([0,0,52,53,54,56,57,58,59,60,62,63,90])
                    planes[1][i] = rng.choice([0,0,52,54,57])
                    planes[2][i] = rng.choice([32,219,100])
                planes[0][6*width+8]=51
                visited=bytes([1 if variant == 0 else 0 if variant == 1 else rng.randrange(2)
                               for _ in range(width*length)])
                state=dict(width=width,length=length,maps=[bytes(p) for p in planes],
                           visited=visited,objects=objects,hero=dict(x=8,z=6,heading=variant*90),map_available=False)
                for scale in (2,3,4,6,8):
                    for reveal in (False,True):
                        state['map_available']=reveal
                        for left,top in ((-20,-13),(scale*width-10,scale*length-9)):
                            compare(native,lang,state,scale,left,top,f'classes-v{variant}-map{int(reveal)}')
            # 361 exact angles include every signed native boundary. Tiny worlds
            # keep native execution inexpensive; 5 scales exercise fixed glyph.
            width=length=4
            planes=[bytearray(width*length) for _ in range(3)]
            planes[0][5]=51
            state=dict(width=width,length=length,maps=[bytes(p) for p in planes],
                       visited=bytes([1]*16),objects=objects,hero=dict(x=1,z=1,heading=0),map_available=False)
            for h in range(361):
                state['hero']['heading']=h
                compare(native,lang,state,(2,3,4,6,8)[h%5],-4,-7,f'arrow-{h}')
            print(lang,'synthetic classes/visited/clipping/361 angles passed',flush=True)
    # Public telemetry ABI and all native class IDs, including silent 5/11.
    for lang in ('ru','en'):
        native=OriginalMap(lang)
        state=from_model(json.loads((ROOT / f'research/evidence/runtime/{lang}/LEVEL1-runtime.json').read_text()))
        packed=bytearray(1024)
        for o in state['objects']:
            struct.pack_into('<HBB',packed,o['glyph']*4,o['class_id'],o['marker'],o['monster_state'])
        telemetry={k:v for k,v in state.items() if k not in ('objects','hero')}
        telemetry.update(glyphs=bytes(packed),hero_x=state['hero']['x'],hero_z=state['hero']['z'],heading=state['hero']['heading'])
        expected=native.draw(state,3,0,0)
        assert native.draw(telemetry,3,0,0)==expected
        rows.append(dict(language=lang,role='packed-public-ABI',different_pixels=0,
                         sha256=hashlib.sha256(expected).hexdigest()))
        objects=[dict(glyph=i+50,class_id=i,marker=30+i,monster_state=0) for i in range(14)]
        objects.extend([dict(glyph=32,class_id=0,marker=4,monster_state=0),
                        dict(glyph=219,class_id=0,marker=5,monster_state=0)])
        planes=[bytearray(256) for _ in range(3)]
        for i in range(14):
            if i != 1: planes[0][5*16+i+1]=50+i
        planes[0][8*16+8]=51
        state=dict(width=16,length=16,maps=[bytes(p) for p in planes],visited=bytes([1]*256),
                   objects=objects,hero=dict(x=8,z=8,heading=45),map_available=True)
        for scale in (2,3,4,6,8):
            expected=native.draw(state,scale,-8,-8)
            assert actual(dll,state,scale,-8,-8)==expected
            rows.append(dict(language=lang,role='all-class-IDs-0-through-13',scale=scale,different_pixels=0,
                             sha256=hashlib.sha256(expected).hexdigest()))
    report=dict(task='N20',passed=True,checks=len(rows),pixels=len(rows)*40960,rows=rows,
                original_sha256_before={lang:OriginalMap(lang).sha_before for lang in ('ru','en')},
                original_sha256_after={lang:OriginalMap(lang).sha_before for lang in ('ru','en')},
                address_system='Ghidra NE loader; relocated far pointers for x86 execution',
                adaptations=['256x160 translated viewport','explicit scales 2/3/4/6/8 replacing x87 result',
                'hero stale locals initialized; final arrow drawn once'],
                source_sha256={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in [Path(__file__),ROOT/'ports/nds/tools/map/original_map.py',ROOT/'ports/nds/ui/map_raster.c',ROOT/'ports/nds/include/dok/map_raster.h']})
    dest=ROOT/args.out;dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__': main()
