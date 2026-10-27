"""Direct source-art OBJ bring-up in the real PPU; not an FPS benchmark."""
import argparse
from collections import Counter
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct

from gba_world_obj import rectangle, pack, render
from research_gba_fps import GameProbe, ROOT, freeze
from verify_gba import VIDEO, sha, png, scene_source_y
from verify_gba_static_cache import Workspace
from verify_sprites_gba import load_json


def signed(n):
    return (n + 0x80000000) % 0x100000000 - 0x80000000


def capture(build, core):
    p = GameProbe(core, build)
    rows, examples = [], {}
    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d:d['draws'] >= 4 and d['phase'] == 2)
        freeze(p, True)
        for language in (0, 1):
            p.language(language)
            p.camera(2, 15120, -300, 22800, 90)
            for level in (1, 2, 3):
                model = load_json(f'research/evidence/runtime/ru/LEVEL{level}-runtime.json')
                hero = model['objects'][model['player_index'] - 1]['actor']
                cell = hero['z'] // 480 * 64 + hero['x'] // 480
                cases = [(cell, h) for h in (0, 45, 90, 135)]
                cases += {1:[(520,225),(3640,225)],2:[(1592,135),(2871,225)],3:[(1544,315)]}[level]
                for cell, heading in cases:
                    p.camera(level - 1, cell % 64 * 480 + 240, -300, cell // 64 * 480 + 240, heading)
                    for _ in range(4):
                        d = p.present()
                    assert d['guard_ok'] == 1
                    w = Workspace.from_buffer_copy(p.frontend.read(p.symbols['g_render_frame'], ct.sizeof(Workspace)))
                    palette = p.frontend.read(0x05000200, 512)
                    black = p.words('g_hw_tile_black', 1)[0]
                    counts, candidates = Counter(), []
                    for f in w.faces[:w.face_count]:
                        x0,x1,t0,t1,b0,b1,d0,d1,u0,u1,key = map(signed, f.n)
                        if x0 > x1:
                            x0,x1,t0,t1,b0,b1,d0,d1,u0,u1 = x1,x0,t1,t0,b1,b0,d1,d0,u1,u0
                        if x1 < 0 or x0 > 239 or min(t0,t1) > 127 or max(b0,b1) < 0:
                            continue
                        counts['visible_face_bbox'] += 1
                        if d0 != d1 or t0 != t1 or b0 != b1:
                            counts['perspective_not_yet_supported'] += 1
                            continue
                        counts['constant_depth_rectangle'] += 1
                        idx = f.s[0]
                        im = w.images[idx] if idx < 256 else w.instance_images[idx - 256]
                        iw,ih,flags,rid = im.s
                        assert 0 < iw <= 256 and 0 < ih <= 512
                        raw = p.frontend.read(im.p[0], iw * ih)
                        row = min(15, max(0, (d0 - 1920) // 120))
                        lut = bytes(w.lights)[row * 256:(row + 1) * 256]
                        pixels = bytes((lut[v] or (0 if flags & 8 else black)) for v in raw)
                        image = dict(width=iw, height=ih, pixels=pixels, resource=rid,
                                     masked=bool(flags & 8), light_row=row, palette=palette,
                                     raw_pixels=raw,lights=bytes(w.lights),black=black,
                                     source_sha256=hashlib.sha256(raw).hexdigest())
                        v0 = f.s[1] if f.s[1] < 32768 else f.s[1] - 65536
                        box = (x0, t0 * 9 // 8, x1, b0 * 9 // 8)
                        patches, reason = rectangle(image, box, (u0, u1, max(0, v0)))
                        if reason:
                            counts[reason] += 1
                            continue
                        counts['patch_candidate'] += 1
                        candidates.append((key, patches, image, list(box)))
                        if iw == 96 and ih == 192 and not flags & 8:
                            examples.setdefault(f'wall-{language}', image)
                        if flags & 8 and iw >= 60 and ih >= 40 and 0 in pixels and any(pixels):
                            examples.setdefault(f'masked-{language}', image)
                    plans = {}
                    for mode, vr, objs, mats in (('full_obj',32768,128,32),('hybrid',16384,64,31),
                                                ('hybrid_multiplex',16384,120,30)):
                        selected, failures = [], Counter()
                        admitted = 0
                        for _, patches, _, _ in sorted(candidates, key=lambda c:c[0]):
                            result, reason = pack(selected + patches, vr, objs, mats)
                            if reason:
                                failures[reason] += 1
                            else:
                                selected += patches
                                admitted += 1
                        result, _ = pack(selected, vr, objs, mats)
                        plans[mode] = dict(admitted_rectangles=admitted, failures=dict(failures),
                            **{k:result[k] for k in ('objects','matrices','texture_bytes')})
                    rows.append(dict(level=level, language=language, cell=cell, heading=heading,
                                     counts=dict(counts), resource_plans=plans))
        assert len(rows) == 34 and len(examples) == 4, (len(rows), list(examples))
        return dict(rom_sha256=sha(p.rom), rows=rows), examples
    finally:
        p.close()


def ppu_proof(build, core, examples, out):
    p = GameProbe(core, build)
    rows = []
    try:
        p.video_callback = VIDEO(p.frontend.video)
        p.frontend.core.retro_set_video_refresh(p.video_callback)
        p.until(lambda d:d['draws'] >= 1)
        p.write(p.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        p.until(lambda d:d['draws'] >= 4 and d['phase'] == 2)
        assert 'g_world_obj_probe' in p.symbols
        address = p.symbols['g_world_obj_probe']
        p.write(address + 8, struct.pack('<I', 1))
        p.until(lambda d:p.words('g_world_obj_probe',4)[3] == 1)
        for language in (0, 1):
            wall, masked = examples[f'wall-{language}'], examples[f'masked-{language}']
            fixtures = [
                ('wall-native', [(wall,(40,-24,135,167),None)]),
                ('wall-fractional', [(wall,(31,-32,158,219),None)]),
                ('wall-cropped', [(wall,(30,-24,130,175),(23,80,45))]),
                ('masked', [(masked,(70,30,150,118),None)]),
                ('masked-mirror-clip', [(masked,(-26,-15,86,94),(masked['width']-1,0,0))]),
                ('masked-before-wall', [(masked,(69,24,150,116),None),(wall,(35,-24,130,167),None)]),
                ('wall-before-masked', [(wall,(35,-24,130,167),None),(masked,(69,24,150,116),None)]),
                ('masked-behind-cpu-thin', [(masked,(69,24,150,116),None)]),
                ('masked-behind-cpu', [(masked,(69,24,150,116),None)]),
                ('masked-behind-cpu-multiplex', [(masked,(69,24,150,116),None)]),
                ('masked-behind-cpu-multiplex-small', [(masked,(69,36,150,80),None)]),
                ('cpu-only', []),
            ]
            for band in range(16):
                lit = []
                for im in (masked,wall):
                    lut = im['lights'][band*256:(band+1)*256]
                    pixels = bytes(lut[c] or (0 if im['masked'] else im['black']) for c in im['raw_pixels'])
                    lit.append(dict(im,pixels=pixels))
                fixtures.append((f'light-{band:02}',[(lit[0],(69,24,150,116),None),(lit[1],(35,-24,130,167),None)]))
            for name, items in fixtures:
                patches = []
                for image, box, uv in items:
                    parts, reason = rectangle(image, box, uv)
                    assert reason is None, (name, reason)
                    patches += parts
                multiplex = name.startswith('masked-behind-cpu-multiplex')
                hybrid = name in ('masked-behind-cpu-thin','masked-behind-cpu','cpu-only') or multiplex
                batch, reason = (pack(patches,16384,120,30,8,2,512) if multiplex else
                                 pack(patches,16384,64,31,64,1,512) if hybrid else pack(patches))
                assert reason is None, (name, reason)
                palette = wall['palette']
                # Only normal EWRAM is changed by the frontend. Actual ARM code
                # performs every palette/OAM/MMIO write in the diagnostic ROM.
                cpu = bytearray(16384)
                if hybrid:
                    # Real source-art masked foreground plus two solid bands
                    # produces several visible Y intervals. This is a priority
                    # fixture, not a claim of whole-map geometric visibility.
                    for y in range(20,108):
                        if name == 'masked-behind-cpu-thin' and not 40 <= y < 48:continue
                        for x in range(28,90):
                            sx = (x-28)*masked['width']//62
                            sy = (y-20)*masked['height']//88
                            c = masked['pixels'][sy*masked['width']+sx]
                            if 36 <= y < 44 or 80 <= y < 88:
                                c = wall['pixels'][(y%wall['height'])*wall['width']+x%wall['width']]
                            at = (y//8)*1024+(x//8)*64+(y%8)*8+x%8
                            cpu[at] = c
                    tiles = bytes(cpu) + batch['tiles'].ljust(16384,b'\0')
                else:
                    tiles = batch['tiles'].ljust(32768,b'\0')
                stage = tiles + batch['oam'] + palette + struct.pack('<H',int(hybrid)|(2 if multiplex else 0))
                p.write(p.symbols['g_render_frame'] + Workspace.frame.offset, stage)
                request = p.words('g_world_obj_probe',2)[1] + 1
                p.write(address, struct.pack('<I',request))
                p.until(lambda d:p.words('g_world_obj_probe',2)[1] == request)
                p.frontend.run(3)
                dispcnt=struct.unpack('<H',p.frontend.read(0x04000000,2))[0]
                assert dispcnt==(0x3060 if multiplex else 0x3040),hex(dispcnt)
                expected_indices = render(patches)
                if hybrid:
                    composite = bytearray(expected_indices)
                    for y in range(144):
                        sy = scene_source_y(y,64)
                        for x in range(240):
                            at = (sy//8)*1024+(x//16)*64+(sy%8)*8+(x//2)%8
                            if cpu[at]:composite[y*240+x] = cpu[at]
                    expected_indices = bytes(composite)
                pal = struct.unpack('<256H', palette)
                expected = [pal[c] if c and y < 144 else 0x4210
                            for y in range(160) for c in expected_indices[y*240:(y+1)*240]]
                actual = p.frontend.colors555()
                different = sum(a != b for a,b in zip(actual,expected))
                file = out / f'{name}-{language}.png'
                p.frontend.screenshot(file)
                expected_file = out / f'{name}-{language}-expected.png'
                png(expected_file,240,160,bytes((c >> s & 31)*255//31 for c in expected for s in (0,5,10)))
                rows.append(dict(name=name,language=language,objects=batch['objects'],matrices=batch['matrices'],
                    texture_bytes=batch['texture_bytes'],different_native_pixels=different,
                    image=str(file),image_sha256=sha(file)))
                if different:
                    rows[-1]['different_rows'] = dict(Counter(i//240 for i,(a,b) in enumerate(zip(actual,expected)) if a!=b))
                    rows[-1]['scope'] = 'Composition exceeds available OBJ scanline work; native admission must fall back to CPU.'
                rows[-1]['hblank_oam_access_enabled'] = multiplex
                rows[-1]['dispcnt'] = dispcnt
                assert different == 0 or name in ('masked-behind-cpu','masked-behind-cpu-multiplex'), (name,language,different)
        return dict(rom_sha256=sha(p.rom), rows=rows)
    finally:
        p.close()


def main():
    a = argparse.ArgumentParser(description=__doc__)
    a.add_argument('--build-dir',type=Path,default=Path('research/experiments/g20a-columns-v6/hw_v128_entity_rect'))
    a.add_argument('--ppu-build',type=Path,default=Path('research/experiments/g20a-world-obj-probe/hw_v128_world_obj_probe'))
    a.add_argument('--out',type=Path,default=Path('research/experiments/g20a-world-obj-bringup'))
    a = a.parse_args()
    os.chdir(ROOT)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    a.out.mkdir(parents=True,exist_ok=True)
    core = Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    geometry, images = capture(a.build_dir, core)
    ppu = ppu_proof(a.ppu_build, core, images, a.out)
    result = dict(task='G20A',status='source-art-obj-bringup',core_sha256=sha(core),
        scripts={p:sha(ROOT/p) for p in ('scripts/gba_world_obj.py','scripts/probe_gba_world_obj.py')},
        geometry=geometry,ppu=ppu,asset_package_sha256=sha(ROOT/'build/gba/assets/assets.bin'),
        scope='34 frozen real views RU/EN resource planning; 56 source-art PPU fixtures including all 16 LIGHT rows, CPU-layer occlusion, scanline-pressure cases and legal HBlank OAM access. Supersedes V4 probe without DISPCNT bit5. No native CPU offload, live FPS or whole-scene occlusion acceptance.',
        limitations=['Perspective walls not implemented; constant-depth rectangles only.',
                     'Affine canvas enlargement beyond 2x explicitly falls back; no mip or degraded art.',
                     'Full OBJ plans omit CPU ceiling composition; hybrid plans reserve 64 OBJ/16KiB for the CPU scene.',
                     'Resource admission uses face bounding boxes, not final visibility; no projected CPU/FPS savings.',
                     'Quantized patch transforms may differ from a seamless whole-image resize; exact proof is against independent patch sampling.'])
    result['source_images'] = []
    for key, im in images.items():
        source = a.out/(key+'-lit-source.bin')
        source.write_bytes(im['pixels'])
        result['source_images'].append(dict(name=key,width=im['width'],height=im['height'],
            resource=im['resource'],masked=im['masked'],light_row=im['light_row'],
            original_source_sha256=im['source_sha256'],lit_pixels_path=str(source),
            lit_pixels_sha256=sha(source),palette_sha256=hashlib.sha256(im['palette']).hexdigest()))
    result['ppu_exact_cases'] = sum(r['different_native_pixels']==0 for r in ppu['rows'])
    result['ppu_pressure_cases_with_missing_pixels'] = sum(r['different_native_pixels']!=0 for r in ppu['rows'])
    (a.out/'report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    exact = sum(r['different_native_pixels']==0 for r in ppu['rows'])
    print('PPU exact',exact,'/',len(ppu['rows']),';',len(geometry['rows']),'resource-planning views. Pressure-case failures remain explicit.')


if __name__ == '__main__':
    main()
