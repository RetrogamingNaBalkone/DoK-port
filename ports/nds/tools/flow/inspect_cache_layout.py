"""Extract pinned ARM9 sprite/cache layout for read-only fault diagnosis."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from inspect_ai_layout import parse, concrete, integer, member, ref, pin, elf_symbol


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    elf = args.elf.resolve()
    tool = Path.home()/'.local/msys64/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-readelf.exe'
    env = os.environ.copy()
    env['PATH'] = str(Path.home()/'.local/msys64/opt/wonderful/bin')+';'+env['PATH']
    text = subprocess.run([str(tool), '--debug-dump=info', str(elf)], env=env,
                          check=True, capture_output=True, encoding='utf8').stdout
    nodes = parse(text)
    names = ('SpriteWorld', 'dok_texture_cache', 'dok_texture_cache_record',
             'dok_frame_store', 'dok_frame_record', 'dok_sprite_list',
             'dok_sprite_pose', 'dok_sprite_asset_bank', 'dok_asset_entry',
             'dok_nds_stream_texture', 'dok_nds_render_texture', 'NdsSpriteProbe',
             'NdsGameplay', 'dok_combat', 'dok_items', 'dok_inventory',
             'dok_weapon_runtime', 'dok_sprite_animation', 'dok_initial_world',
             'dok_world_object_seed', 'dok_level_definition', 'dok_level_object',
             'dok_level_presentation', 'dok_presentation_resource')
    result = dict(elf=pin(elf), types={}, symbols={})
    for name in names:
        matches = [n for n in nodes.values() if n['tag'] == 'DW_TAG_structure_type'
                   and n['attrs'].get('name') == name and 'byte_size' in n['attrs']]
        if not matches:
            continue
        node = matches[0]
        result['types'][name] = dict(bytes=integer(node['attrs']['byte_size']),
            members={nodes[i]['attrs']['name']: integer(nodes[i]['attrs']['data_member_location'])
                     for i in node['children'] if nodes[i]['tag'] == 'DW_TAG_member'})
    for name in ('storage', 'demand', 'demand_count', 'g_nds_sprite_probe',
                 'g_nds_effect_probe', 'g_nds_render_probe'):
        try:
            address, size = elf_symbol(elf, name)
            result['symbols'][name] = dict(address=address, bytes=size)
        except (AssertionError, ValueError):
            pass
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', 'utf8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
