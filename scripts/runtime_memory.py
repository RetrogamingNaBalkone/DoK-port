"""Read sizeof/alignment/offset values emitted by the ARM compiler in a probe ELF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from gba_rom import elf_info

NAMES = ('magic', 'version', 'pointer_size',
         'object_size', 'object_align', 'actor_size', 'actor_align',
         'player_size', 'player_align', 'definition_size', 'definition_align',
         'field_size', 'field_align', 'level_size', 'level_align',
         'storage_size', 'storage_align', 'runtime_size', 'runtime_align',
         'native_snapshot_size', 'host_level_object_size',
         'objects_offset', 'actors_offset', 'player_offset', 'maps_offset', 'counts_offset',
         'object_capacity', 'actor_capacity', 'field_capacity', 'text_capacity', 'cell_capacity')
RESERVES = {'g_frame_reserve': 38400, 'g_renderer_reserve': 4096, 'g_texture_reserve': 65536,
            'g_audio_reserve': 8192, 'g_event_reserve': 16384,
            'g_save_reserve': 4096, 'g_scratch_reserve': 8192}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    entry, symbols, sections = elf_info(args.elf)
    blob = args.elf.read_bytes()
    address = symbols['dok_memory_layout']
    section = next(s for s in sections if s['address'] <= address < s['address'] + s['size'])
    # ELF32 little endian: section-file offsets come from the section table.
    shoff = struct.unpack_from('<I', blob, 32)[0]
    shentsize, shnum = struct.unpack_from('<HH', blob, 46)
    file_offset = None
    for i in range(shnum):
        _, _, _, vma, offset, size, *_ = struct.unpack_from('<10I', blob, shoff + i*shentsize)
        if vma == section['address'] and size == section['size']:
            file_offset = offset + address - vma
            break
    assert file_offset is not None
    values = dict(zip(NAMES, struct.unpack_from('<' + 'I'*len(NAMES), blob, file_offset)))
    assert values['magic'] == 0x324d454d and values['version'] == 1
    assert values['pointer_size'] == 4, 'This is an ARM memory measurement'
    static = symbols['__bss_end'] - symbols['__data_start']
    expected = values['runtime_size'] + sum(RESERVES.values()) + 40
    assert static == expected, (static, expected)
    assert static <= 256*1024
    result = {'task': 'G02', 'source_elf': str(args.elf), 'entry': entry,
              'elf_sha256': hashlib.sha256(blob).hexdigest(), 'arm_layout': values,
              'sections': sections,
              'ewram': {'hardware_bytes': 256*1024, 'probe_static_bytes': static,
                        'diagnostic_bytes': 40, 'runtime_bytes': values['runtime_size'],
                        'future_reserves': RESERVES, 'unallocated_bytes': 256*1024-static},
              'iwram': {'hardware_bytes': 32*1024, 'current_stack_bios_reserve': 1024,
                        'runtime_probe_static_bytes': 0, 'stack_high_water_measured': False},
              'symbols': {name: symbols[name] for name in ('g_runtime_memory_probe', *RESERVES)}}
    args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f"ARM runtime {values['runtime_size']} bytes; EWRAM with reserves {static}/262144; free {256*1024-static}")


if __name__ == '__main__':
    main()
