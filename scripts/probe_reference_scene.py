"""Read-only G04 evidence from saved G00 DOS memory and indexed VGA frames.

Does not execute, disassemble or modify an original EXE. Data offsets are tags
from the project's Ghidra-derived research; raw candidates remain labelled.
Only reports and grayscale visualizations under research/evidence/render-reference are
written. A saved CPU report was taken after input trials, not simultaneously
with the earlier initial-scene memory snapshot.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from verify_gba import png
from analyze_resources import parse
from analyze_levels import reference

ROOT = Path(__file__).resolve().parents[1]
ANCHOR = b'OVERFLOW\0UNDERFLOW\0TLOSS\0PLOSS'
RUNS = {'ru': 'ru-03', 'en': 'en-02'}
OFFSETS = {
    'c_runtime_anchor': (0x1082, 'C runtime string', 'docs/reference-runs.md'),
    'surface_pitch': (0xb52c, 'u16', 'WASDify performance.md; Ghidra NE3:0155'),
    'surface_buffer': (0xb53a, 'far16', 'WASDify profile_kremlin.py; surface global'),
    'screen_geometry': (0xc1fc, 'u16[2]', 'G00 scripts/dos_reference.py'),
}
# Fresh Ghidra camera init/projection evidence supplied by the G04 geometry pass.
# These globals have a shared +0xb0 displacement in ENG; focal is an exception.
CAMERA_OFFSETS = {
    'D': (0xaa16, 'i'), 'width': (0xaa1a, 'h'), 'height_initialization': (0xaa1c, 'h'),
    'center_x': (0xaa1e, 'h'), 'center_y': (0xaa20, 'h'),
    'clip_left': (0xaa22, 'i'), 'clip_top': (0xaa26, 'i'),
    'clip_right': (0xaa2a, 'i'), 'clip_bottom': (0xaa2e, 'i'),
    'viewport_scale': (0xaa32, 'h'), 'sin_q15': (0xaa3e, 'i'), 'cos_q15': (0xaa42, 'i'),
    'camera_x': (0xaa46, 'i'), 'camera_y': (0xaa4a, 'i'), 'camera_z': (0xaa4e, 'i'),
    'camera_buffer': (0xaa56, 'HH'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def artifact(path):
    return {'path': path.relative_to(ROOT).as_posix(), 'size': path.stat().st_size,
            'sha256': sha(path.read_bytes())}


def descriptor_base(raw):
    return int.from_bytes(raw[2:4], 'little') | (raw[4] << 16) | (raw[7] << 24)


def descriptor_limit(raw):
    limit = int.from_bytes(raw[:2], 'little') | ((raw[6] & 15) << 16)
    return (limit << 12) | 4095 if raw[6] & 128 else limit


def ldt_candidates(memory, ds_base, ds_selector, buffer_selector):
    """Require a present LDT system descriptor and matching present DS entry.

    Unlike a selector*16 shortcut, this never invents a protected-mode base.
    No candidate is a valid and explicitly recorded outcome for partial RAM.
    """
    found = {}
    for access in (0x82, 0xa2, 0xc2, 0xe2):
        position = 0
        while True:
            position = memory.find(bytes([access]), position)
            if position < 0:
                break
            raw = memory[max(0, position-5):position+3]
            descriptor_address = position-5
            position += 1
            if len(raw) != 8:
                continue
            table, limit = descriptor_base(raw), descriptor_limit(raw)
            if not (0x100 <= limit < 65536 and 0 < table < len(memory)-limit):
                continue
            if any((selector & ~7) + 7 > limit for selector in (ds_selector, buffer_selector)):
                continue
            entry = memory[table+(ds_selector & ~7):table+(ds_selector & ~7)+8]
            if descriptor_base(entry) != ds_base or not (entry[5] & 0x90 == 0x90):
                continue
            buffer = memory[table+(buffer_selector & ~7):table+(buffer_selector & ~7)+8]
            if not (buffer[5] & 0x90 == 0x90):
                continue
            found[table] = {'table_linear_address': hex(table), 'table_limit': limit,
                            'descriptor_linear_address': hex(descriptor_address),
                            'buffer_base': hex(descriptor_base(buffer)),
                            'buffer_limit': descriptor_limit(buffer)}
    return list(found.values())


def trig_table(memory, output, language):
    pattern = struct.pack('<5i', 0, 571, 1143, 1714, 2285)
    expected = [math.trunc(operation(n*3.14159265359/180)*32768)
                for operation in (math.sin, math.cos) for n in range(361)]
    candidates = []
    position = 0
    while True:
        position = memory.find(pattern, position)
        if position < 0:
            break
        if position+722*4 <= len(memory):
            actual = struct.unpack_from('<722i', memory, position)
            candidates.append({'linear_address': hex(position),
                               'mismatches': sum(a != e for a,e in zip(actual, expected))})
        position += 1
    exact = [candidate for candidate in candidates if not candidate['mismatches']]
    assert len(exact) == 1, 'No unique complete Ghidra-derived sine/cosine table'
    address = int(exact[0]['linear_address'], 16)
    raw = memory[address:address+722*4]
    (output/f'{language}-trig-q15.bin').write_bytes(raw)
    return {'linear_address': hex(address), 'cosine_offset_from_sine': '0x5a4',
            'entries_per_function': 361, 'integer_values_checked': 722,
            'formula': 'trunc0(sin/cos(n*3.14159265359/180)*32768), n=0..360',
            'mismatches': 0, 'sha256': sha(raw), 'search_candidates': candidates,
            'ghidra_table_space': '1180:0000' if language == 'ru' else '10c0:0000',
            'ghidra_initializer': '10c8:003b..00d3' if language == 'ru' else '1058:003b..00d3',
            'method': 'Unique complete-table match in saved linear RAM, not selector*16'}


def i32(value):
    value &= 0xffffffff
    return value if value < 0x80000000 else value - 0x100000000


def s16(value):
    value &= 0xffff
    return value if value < 0x8000 else value - 0x10000


def idiv(numerator, denominator):
    assert denominator
    value = abs(numerator) // abs(denominator)
    return -value if (numerator < 0) != (denominator < 0) else value


def project(point, camera):
    get = lambda key: camera[key]['value']
    dx,dy,dz = [i32(point[n]-get('camera_'+axis)) for n,axis in enumerate(('x','y','z'))]
    depth = i32(i32(dz*get('cos_q15'))-i32(dx*get('sin_q15'))) >> 15
    lateral = i32(i32(dx*get('cos_q15'))+i32(dz*get('sin_q15'))) >> 15
    numerator = i32(i32(get('viewport_scale')*get('D'))*get('focal_q12'))
    q = idiv(numerator, i32(depth+get('D')))
    return [i32(get('center_x')+(i32(lateral*q) >> 15)),
            i32(get('center_y')+(i32(dy*q) >> 15)), depth], q


def door_geometry(memory, pixels, camera, language, output):
    inventory = json.loads((ROOT/f'analysis/inventory-{language}.json').read_text(encoding='utf-8'))
    archive = (ROOT/inventory['resource']['file']).read_bytes()
    assert sha(archive) == inventory['resource']['sha256']
    entries = parse(archive)
    entry = next(e for e in entries if e['name'] == 'LEVEL1')
    source = reference(archive[entry['offset']:entry['offset']+entry['size']])
    definition = next(d for d in source['objects'] if d['glyph'] == 0x64)
    image_entry = next(e for e in entries if e['name_hex'] == definition['sprite'])
    image = archive[image_entry['offset']:image_entry['offset']+image_entry['size']]
    image_width,image_height = struct.unpack_from('<HH', image)
    assert len(image) == 18+image_width*image_height
    assert (definition['class'],image_width,image_height,definition['width'],definition['height']) == (7,96,128,5,0)
    world_width = s16(image_width*definition['width'])
    world_height = s16(image_height*definition['width'])
    side,length = source['settings'][3],source['settings'][2]
    source_map = bytes.fromhex(source['maps'][0])
    quads = []
    for source_x,source_row in ((24,16),(24,17)):
        assert source_map[source_row*64+source_x] == 0x64
        origin = [source_x*side, -definition['height'], (length-1-source_row)*side]
        # Fresh center-plane theta90 constructor: x=originX+width/2;
        # z=originZ..originZ+width; y=originY-height..originY.
        x = i32(origin[0]+(world_width >> 1))
        a_top,q = project([x,origin[1]-world_height,origin[2]],camera)
        b_top,q2 = project([x,origin[1]-world_height,origin[2]+world_width],camera)
        a_bottom,_ = project([x,origin[1],origin[2]],camera)
        b_bottom,_ = project([x,origin[1],origin[2]+world_width],camera)
        assert q == q2 and a_top[2] == b_top[2] == 3360 and q == 1840
        matches = []
        for order,vertices in (('forward',[a_top,b_top,b_bottom,a_bottom]),
                               ('reverse',[b_top,a_top,a_bottom,b_bottom])):
            pattern = b''.join(struct.pack('<iii',*v) for v in vertices)
            at = 0
            while True:
                at = memory.find(pattern,at)
                if at < 0:
                    break
                record = at-0x13
                if record >= 0 and record+0x4b <= len(memory):
                    vtable = struct.unpack_from('<H',memory,record)[0]
                    texture_u = list(struct.unpack_from('<ii',memory,record+0x43))
                    if vtable == (0x966 if language == 'ru' else 0x937) and texture_u == [0,95]:
                        matches.append({'record_linear_address': hex(record), 'vertex_linear_address': hex(at),
                                        'vtable_offset': hex(vtable), 'vertex_order': order,
                                        'vertices_xy_depth': vertices, 'texture_u_range': texture_u,
                                        'record_sha256': sha(memory[record:record+0x4b])})
                at += 1
        assert len(matches) == 1, 'Expected one complete projected door record in the saved DOS memory'
        xs,ys = [a_top[0],b_top[0]],[a_top[1],a_bottom[1]]
        quads.append({'source_cell_xy': [source_x,source_row],
                      'runtime_cell_xz': [source_x,length-1-source_row],
                      'origin_xyz': origin, 'world_plane_x': x,
                      'world_width_height': [world_width,world_height], 'depth': 3360, 'q': q,
                      'projected_bounds_inclusive': [min(xs),min(ys),max(xs),max(ys)],
                      'memory_match': matches[0]})
    x0 = min(q['projected_bounds_inclusive'][0] for q in quads)
    x1 = max(q['projected_bounds_inclusive'][2] for q in quads)
    y0,y1 = 59,95
    crop = b''.join(pixels[y*320+x0:y*320+x1+1] for y in range(y0,y1+1))
    png(output/f'{language}-door-reference-crop.png',x1-x0+1,y1-y0+1,
        b''.join(bytes((v,v,v)) for v in crop))
    return {'glyph_hex': '64', 'class': 7, 'sprite_name_hex': definition['sprite'],
            'image_resource_index': image_entry['index'], 'image_payload_sha256': sha(image),
            'image_size': [image_width,image_height], 'source_scale': definition['width'],
            'low16_scaled_size': [world_width,world_height], 'quads': quads,
            'combined_bounds_inclusive': [x0,y0,x1,y1],
            'frame_crop_indices_sha256': sha(crop), 'projected_records_match': True,
            'evidence': {'scaled_size': 'RU1098:3583..35ac / EN1030:3b29..3b57',
                         'constructor_theta90': 'RU10b0:00aa..00f9 / EN1040 corresponding constructor',
                         'projection': 'RU10b0:01a3..0501 / EN1040 corresponding constructor'},
            'scope': 'Exact geometric vertices in actual saved RAM; opaque texture edges and full framebuffer equivalence not asserted'}


def scene(language, output):
    directory = ROOT / 'research/evidence/reference' / RUNS[language]
    report = json.loads((directory/'report.json').read_text(encoding='utf-8'))
    memory = (directory/'level1-memory.bin').read_bytes()
    pixels = (directory/'level1-start.idx').read_bytes()
    delta = 0 if language == 'ru' else 0xb0
    assert memory.count(ANCHOR) == 1, 'Ambiguous runtime data anchor'
    ds = memory.find(ANCHOR) - 0x1082 - delta
    assert ds == int(report['runtime_linear_data_base'], 16)
    width, height = struct.unpack_from('<HH', memory, ds+0xc1fc+delta)
    assert (width, height) == (320, 200) and len(pixels) == width*height
    assert memory[0xa0000:0xa0000+len(pixels)] == pixels
    expected_frame = next(frame for frame in report['frames'] if frame['name'] == 'level1-start')
    assert sha(pixels) == expected_frame['sha256']
    hero = int(report['player_start']['linear_address'], 16)
    pose = list(struct.unpack_from('<iiih', memory, hero+0x26))
    assert pose[:3] == report['player_start']['xyz'] and pose[3] == report['player_start']['heading']
    pitch = struct.unpack_from('<H', memory, ds+0xb52c+delta)[0]
    offset, selector = struct.unpack_from('<HH', memory, ds+0xb53a+delta)
    registers = report['cpu_final']['registers']
    candidates = ldt_candidates(memory, ds, registers['ds'], selector)
    output.mkdir(parents=True, exist_ok=True)
    trig = trig_table(memory, output, language)
    png(output/f'{language}-level1-start.png', width, height,
        b''.join(bytes((value, value, value)) for value in pixels))
    camera = {}
    for name, (offset_tag, fmt) in CAMERA_OFFSETS.items():
        actual = offset_tag + delta
        value = struct.unpack_from('<'+fmt, memory, ds+actual)
        camera[name] = {'data_segment_offset': hex(actual), 'linear_address': hex(ds+actual),
                        'format': fmt, 'value': value[0] if len(value) == 1 else list(value)}
    focal_offset = 0x934 if language == 'ru' else 0xa44
    camera['focal_q12'] = {'data_segment_offset': hex(focal_offset), 'linear_address': hex(ds+focal_offset),
                           'format': 'h', 'value': struct.unpack_from('<h', memory, ds+focal_offset)[0]}
    camera['surface_width_scale'] = {'data_segment_offset': hex(0xb52e+delta),
                                     'value': struct.unpack_from('<f', memory, ds+0xb52e+delta)[0]}
    camera['surface_height_scale'] = {'data_segment_offset': hex(0xb532+delta),
                                      'value': struct.unpack_from('<f', memory, ds+0xb532+delta)[0]}
    assert [camera['camera_'+axis]['value'] for axis in ('x', 'y', 'z')] == pose[:3]
    assert [camera[name]['value'] for name in ('clip_left','clip_top','clip_right','clip_bottom')] == [0,0,319,159]
    assert camera['focal_q12']['value'] == 4096
    door = door_geometry(memory,pixels,camera,language,output)
    clip_height = camera['clip_bottom']['value'] - camera['clip_top']['value'] + 1
    viewport = pixels[:width*clip_height]
    png(output/f'{language}-raw-viewport-320x160.png', width, clip_height,
        b''.join(bytes((v,v,v)) for v in viewport))
    aspect_preview = bytes(viewport[(y*160//144)*320 + (x*320//240)]
                           for y in range(144) for x in range(240))
    png(output/f'{language}-aspect-reference-240x144.png', 240, 144,
        b''.join(bytes((v,v,v)) for v in aspect_preview))
    log_lines = (directory/'stderr.log').read_text(encoding='utf-8', errors='replace').splitlines()
    aspect_log = [line for line in log_lines if 'graphics mode 13h' in line]
    values = {}
    for name, (tag, kind, evidence) in OFFSETS.items():
        actual = tag + delta
        values[name] = {'data_segment_offset': hex(actual), 'linear_address': hex(ds+actual),
                        'kind': kind, 'evidence': evidence}
    values['surface_pitch']['value'] = pitch
    values['surface_buffer'].update(offset=hex(offset), selector=hex(selector), linear_pointer=None)
    values['screen_geometry']['value'] = [width, height]
    return {'language': language, 'source_run': RUNS[language],
            'inputs': [artifact(directory/name) for name in ('report.json', 'level1-memory.bin', 'level1-start.idx')],
            'runtime_data_base': hex(ds), 'anchor_unique': True,
            'player_linear_address': hex(hero), 'player_xyz_heading': pose,
            'globals': values,
            'camera_globals': camera,
            'runtime_trig_table': trig,
            'door_geometry': door,
            'camera_evidence': 'Fresh G04 Ghidra camera initialization/projection listing; final address pairs in docs/rendering.md',
            'vga': {'linear_address': '0xa0000', 'width': width, 'height': height,
                    'sha256': sha(pixels), 'snapshot_equals_saved_start_frame': True,
                    'palette': 'grayscale index visualization; original DAC not captured'},
            'selectors_from_later_cpu_report': {'ds': hex(registers['ds']), 'ss': hex(registers['ss']),
                                               'simultaneous_with_memory_snapshot': False},
            'ldt_resolution': {'candidates': candidates, 'resolved': len(candidates) == 1,
                               'status': 'No validated descriptor table in the saved partial memory' if not candidates else 'candidate'},
            'configured_guest_ram_bytes': 32*1024*1024, 'captured_memory_bytes': len(memory),
            'viewport_aspect': {'raw_clip_pixels': [320,160], 'source_ui_starts_at_row': 160,
                                'dosbox_mode13_log': aspect_log,
                                'dosbox_pixel_aspect_width_height': [5,6],
                                'aspect_corrected_source_extent': [320,192],
                                'gba_width': 240, 'raw_isotropic_height': 120,
                                'aspect_preserving_height': 144, 'remaining_gba_rows': 16,
                                'preview_status': 'Nearest-neighbor resampled DOS reference; not a GBA renderer'},
            'projection_status': 'Camera/trig and two projected door records agree exactly with source map/image and Ghidra arithmetic'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'research/evidence/render-reference')
    parser.add_argument('--report', type=Path, default=ROOT/'analysis/render-reference.json')
    args = parser.parse_args()
    inventory = {lang: json.loads((ROOT/f'analysis/inventory-{lang}.json').read_text(encoding='utf-8'))
                 for lang in RUNS}
    before = {lang: {name: sha((ROOT/item[name]['file']).read_bytes()) if name == 'resource' else
                    sha((ROOT/item['file']).read_bytes()) for name in ('exe', 'resource')}
              for lang, item in inventory.items()}
    for lang, item in inventory.items():
        assert before[lang] == {'exe': item['sha256'], 'resource': item['resource']['sha256']}
    result = {'task': 'G04', 'kind': 'read-only saved DOS snapshots and VGA index frames',
              'scenes': [scene(language, args.output) for language in RUNS], 'originals': before,
              'ghidra_evidence': [artifact(ROOT/'research/evidence/rendering'/lang/name)
                                  for lang in RUNS for name in ('evidence.asm','analysis-manifest.json')],
              'exe_analysis': 'No original EXE decoding/disassembly; only hashes and prior Ghidra-derived offset tags',
              'status': 'passed_saved_scene_probe',
              'status_scope': 'Saved camera/pose, complete trig tables, VGA identity and exact projected door records; not full renderer pixel equivalence'}
    bounds = [entry['door_geometry']['combined_bounds_inclusive'] for entry in result['scenes']]
    assert [bounds[1][0]-bounds[0][0],bounds[1][1]-bounds[0][1]] == [-27,0]
    crops = []
    for language,extent in zip(RUNS,bounds):
        pixels = (ROOT/'research/evidence/reference'/RUNS[language]/'level1-start.idx').read_bytes()
        x0,y0,x1,y1 = extent
        crops.append(b''.join(pixels[y*320+x0:y*320+x1+1] for y in range(y0,y1+1)))
    result['cross_scene_door_comparison'] = {
        'predicted_eng_minus_rus_translation_pixels': [-27,0],
        'predicted_depth_and_q_equal': True, 'projected_records_checked': 4,
        'crop_pixels': len(crops[0]), 'equal_indices_after_predicted_translation': sum(a==b for a,b in zip(*crops)),
        'scope': 'Index comparison inside projected quads includes background visible through the texture; not a full pixel-renderer oracle'}
    result['runtime_trig_integer_values_checked'] = sum(scene['runtime_trig_table']['integer_values_checked'] for scene in result['scenes'])
    after = {lang: {'exe': sha((ROOT/item['file']).read_bytes()),
                    'resource': sha((ROOT/item['resource']['file']).read_bytes())}
             for lang, item in inventory.items()}
    assert before == after
    result['originals_unchanged'] = True
    args.report.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'scenes': len(result['scenes']), 'status': result['status'],
                      'report': str(args.report)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
