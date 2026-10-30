"""Decode bounded sprite/cache evidence from a read-only ARM9 RAM snapshot.

Field consistency checks are useful evidence; they cannot prove dirty ARM9
D-cache lines are coherent. Published probes provide independent frame bounds.
"""
import argparse
import json
import hashlib
from pathlib import Path
import struct
from plan_routes import pin


def decode(memory, layout):
    raw = memory.read_bytes()
    assert len(raw) == 4 * 1024 * 1024
    base = 0x02000000

    def u32(address):
        assert base <= address <= base + len(raw) - 4
        return struct.unpack_from('<I', raw, address-base)[0]

    def u64(address):
        assert base <= address <= base + len(raw) - 8
        return struct.unpack_from('<Q', raw, address-base)[0]

    def u16(address):
        return struct.unpack_from('<H', raw, address-base)[0]

    def s16(address):
        return struct.unpack_from('<h', raw, address-base)[0]

    def data(address, count):
        assert base <= address <= base+len(raw)-count
        return raw[address-base:address-base+count]

    types, symbols = layout['types'], layout['symbols']

    def fields(name, address):
        return {key: u32(address+offset) for key, offset in types[name]['members'].items()
                if key not in ('records', 'images', 'poses', 'bindings', 'sequences')}

    storage = u32(symbols['storage']['address'])
    offsets = types['SpriteWorld']['members']
    cache_address = storage+offsets['cache']
    cache = fields('dok_texture_cache', cache_address)
    assert cache['count'] <= 256
    records = []
    for i in range(cache['count']):
        address = cache_address+types['dok_texture_cache']['members']['records']+i*24
        records.append(dict(key=u64(address), offset=u32(address+8), bytes=u32(address+12),
                            age=u32(address+16), pinned=u32(address+20)))
    cache['records'] = records
    cache['pinned_bytes'] = sum(r['bytes'] for r in records if r['pinned'])
    frame_address = storage+offsets['frames']
    frames = fields('dok_frame_store', frame_address)
    bank_address = storage+offsets['bank']
    image_count = u32(bank_address+types['dok_sprite_asset_bank']['members']['image_count'])
    assert image_count <= 256
    images = {}
    for i in range(image_count):
        address = bank_address+types['dok_sprite_asset_bank']['members']['images']+120*i
        images[u32(address)] = dict(id=u32(address), bytes=u32(address+12),
                                   sha256=data(address+40, 32).hex())
    list_address = storage+offsets['list']
    poses = fields('dok_sprite_list', list_address)
    assert poses['count'] <= 512
    poses['items'] = []
    for i in range(poses['count']):
        address = list_address+32*i
        image = u32(address+24)
        gpu_id = (image & ~0x3fc00) | (5 << 10)
        poses['items'].append(dict(index=i, image=image, gpu_id=gpu_id,
            source_id=u32(address+28), bytes=images.get(gpu_id, {}).get('bytes'),
            glyph=struct.unpack_from('<H', raw, address-base+18)[0]))
    visible = {p['gpu_id']: p['bytes'] for p in poses['items'] if p['bytes'] is not None}
    poses['unique_image_bytes'] = sum(visible.values())
    poses['unique_image_count'] = len(visible)
    game = storage+offsets['game']
    combat = game+types['NdsGameplay']['members']['combat']
    items = u32(combat+types['dok_combat']['members']['items'])
    inventory = items+types['dok_items']['members']['inventory']
    slot = u16(inventory+types['dok_inventory']['members']['selected'])
    assert slot < 9
    weapon_reference = [u32(inventory+types['dok_inventory']['members']['weapons']+12*slot+4*i) for i in range(3)]
    world = u32(items)
    world_members = types['dok_initial_world']['members']
    seeds = world+world_members['objects']
    weapon_seed = next(seeds+40*i for i in range(u16(world+world_members['object_count']))
                       if [u32(seeds+40*i+4*j) for j in range(3)] == weapon_reference)
    definition = u32(world)
    definition_members = types['dok_level_definition']['members']
    definition_index = u16(weapon_seed+types['dok_world_object_seed']['members']['definition_index'])
    weapon_glyph = data(definition+definition_members['objects']+360*definition_index+10, 1)[0]
    presentation = u32(combat+types['dok_combat']['members']['presentation'])
    localized = u32(presentation+types['dok_level_presentation']['members']['localized'])
    localized_index = u16(localized+definition_members['glyph_index']+2*weapon_glyph)
    assert localized_index > 0
    animation_span = localized+definition_members['objects']+360*(localized_index-1)+24
    animation_name = data(u32(localized)+u32(animation_span), u32(animation_span+4))
    presentation_members = types['dok_level_presentation']['members']
    resources = presentation+presentation_members['resources']
    animation_id = next(u32(resources+24*i) for i in range(u32(presentation+presentation_members['resource_count']))
                        if data(resources+24*i+4, 16).split(b'\0')[0] == animation_name)
    bank_members = types['dok_sprite_asset_bank']['members']
    animation_index = next(i for i in range(u32(bank_address+bank_members['animation_count']))
                           if u32(bank_address+bank_members['animations']+4*i) == animation_id)
    animation = bank_address+bank_members['sequences']+12*animation_index
    animation_metadata, animation_bytes, animation_count = (u32(animation+4*i) for i in range(3))
    assert animation_bytes == 8+16*animation_count
    runtime = combat+types['dok_combat']['members']['weapons']+8*slot
    state, shift, cursor = (s16(runtime+2*i) for i in range(3))
    frame_index = max(0, min(animation_count-1, cursor))
    animation_images = []
    for i in range(animation_count):
        image = u32(animation_metadata+8+16*i)
        gpu_id = (image & ~0x3fc00) | (5 << 10)
        animation_images.append(dict(index=i, image=image, gpu_id=gpu_id,
                                     bytes=images[gpu_id]['bytes']))
    weapon = dict(slot=slot, reference=weapon_reference, glyph=weapon_glyph,
        state=state, shift=shift, cursor=cursor, animation_name_cp866=animation_name.hex(),
        animation_id=animation_id, visible=state != 4, frames=animation_images,
        current=animation_images[frame_index])
    weapon['current_extra_bytes'] = (weapon['current']['bytes']
        if weapon['visible'] and weapon['current']['gpu_id'] not in visible else 0)
    weapon['visible_set_plus_current_bytes'] = poses['unique_image_bytes']+weapon['current_extra_bytes']
    weapon['visible_set_plus_max_animation_bytes'] = poses['unique_image_bytes']+max(
        image['bytes'] if image['gpu_id'] not in visible else 0 for image in animation_images)
    demand_count = u32(symbols['demand_count']['address'])
    assert demand_count <= 128
    demands = []
    for i in range(demand_count):
        address = symbols['demand']['address']+40*i
        demands.append(dict(key=u64(address), asset_id=u32(address+8), pixels=u32(address+12),
                            bytes=u32(address+16)))
    sprite_probe = fields('NdsSpriteProbe', symbols['g_nds_sprite_probe']['address'])
    effect = [u32(symbols['g_nds_effect_probe']['address']+4*i) for i in range(32)]
    checks = dict(capacity_bound=cache['used'] <= cache['capacity'],
        record_bytes_match=sum(r['bytes'] for r in records) == cache['used'],
        unique_record_keys=len({r['key'] for r in records}) == len(records),
        record_ranges=all(r['offset']+r['bytes'] <= cache['used'] for r in records),
        pinned_boolean=all(r['pinned'] in (0, 1) for r in records),
        all_pose_images_resolved=len(visible) == len({p['gpu_id'] for p in poses['items']}),
        sprite_probe_address=sprite_probe['list_address'] == list_address)
    pixel_checks = []
    for record in records:
        payload = data(cache['pool']+record['offset'], record['bytes'])
        actual = hashlib.sha256(payload).hexdigest()
        pixel_checks.append(dict(key=record['key'], actual_sha256=actual,
            expected_sha256=images.get(record['key'], {}).get('sha256'),
            matches=actual == images.get(record['key'], {}).get('sha256')))
    return dict(memory=pin(memory), storage=storage, cache=cache, frames=frames, weapon=weapon,
                list=poses, demands=demands, sprite_probe=sprite_probe, effect_probe=effect,
                consistency_checks=checks, cached_pixel_hash_checks=pixel_checks,
                limitation='Read-only model RAM, no injected cache flush; dirty ARM9 D-cache coherency is not established.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--memory', type=Path, required=True)
    parser.add_argument('--layout', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    layout = json.loads(args.layout.read_text('utf8'))
    result = decode(args.memory.resolve(), layout)
    result['layout'] = pin(args.layout.resolve())
    args.output.write_text(json.dumps(result, indent=2)+'\n', 'utf8')
    print(json.dumps({key: result[key] for key in ('consistency_checks', 'effect_probe')}))
    print(json.dumps({key: result['cache'][key] for key in ('capacity', 'used', 'count', 'age', 'pinned_bytes')}))
    print(json.dumps({key: result['list'][key] for key in ('count', 'unique_image_bytes', 'unique_image_count')}))


if __name__ == '__main__':
    main()
