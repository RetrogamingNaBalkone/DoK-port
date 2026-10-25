"""Finish a cartridge header and validate the linked ARM ELF without extra packages."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

# Standard cartridge boot logo, required by GBA BIOS, no PKR/PKE data.
LOGO = bytes.fromhex(
    '24ffae51699aa2213d84820a84e409ad11248b98c0817f21a352be199309ce20'
    '10464a4af82731ec58c7e83382e3cebf85f4df94ce4b09c194568ac01372a7fc'
    '9f844d73a3ca9a615897a327fc039876231dc7610304ae56bf38840040a70efd'
    'ff52fe036f9530f197fbc08560d68025a963be03014e38e2f9a234ffbb3e0344'
    '780090cb88113a9465c07c6387f03cafd625e48b380aac7221d4f807'
)


def elf_info(path):
    data = Path(path).read_bytes()
    assert data[:7] == b'\x7fELF\x01\x01\x01', 'Expected 32-bit little endian ELF'
    header = struct.unpack_from('<HHIIIIIHHHHHH', data, 16)
    assert header[0:2] == (2, 40), 'Expected executable ARM ELF'
    entry, shoff, shentsize, shnum, shstrndx = header[3], header[5], header[10], header[11], header[12]
    sections = [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]
    shstr = sections[shstrndx]
    names = data[shstr[4]:shstr[4] + shstr[5]]

    def cstring(blob, offset):
        return blob[offset:blob.index(b'\0', offset)].decode('ascii')

    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        strings = data[strings[4]:strings[4] + strings[5]]
        for off in range(section[4], section[4] + section[5], section[9]):
            name, value, size, info, other, ndx = struct.unpack_from('<IIIBBH', data, off)
            if name:
                symbols[cstring(strings, name)] = value
    return entry, symbols, [dict(name=cstring(names, s[0]), address=s[3], size=s[5], type=s[1])
                            for s in sections if s[2] & 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True)
    parser.add_argument('--rom', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    rom = bytearray(Path(args.rom).read_bytes())
    assert len(LOGO) == 156
    assert len(rom) >= 192
    entry, symbols, sections = elf_info(args.elf)
    assert entry == 0x08000000, hex(entry)
    instruction = struct.unpack_from('<I', rom)[0]
    assert instruction >> 24 == 0xea, 'Cartridge must enter in ARM state via B'
    assert 0x08000008 + ((instruction & 0xffffff) << 2) == symbols['_start']
    assert symbols['gba_main'] & 1, 'C entry must be Thumb'
    assert not symbols['gba_irq'] & 3, 'BIOS IRQ callback must be ARM/aligned'
    rom[4:160] = LOGO
    rom[160:192] = b'\0' * 32
    rom[160:172] = b'DOK GBA DIAG'
    rom[172:176] = b'DK01'
    rom[176:178] = b'00'
    rom[178] = 0x96
    rom[189] = (-sum(rom[160:189]) - 0x19) & 255
    assert (sum(rom[160:190]) + 0x19) & 255 == 0
    rom.extend(b'\xff' * (-len(rom) % 4))
    Path(args.rom).write_bytes(rom)
    report = dict(rom=args.rom, rom_bytes=len(rom), rom_sha256=hashlib.sha256(rom).hexdigest(),
                  entry=entry, symbols={k: v for k, v in symbols.items()
                                      if k.startswith('__') or k.startswith('g_assets_') or k.startswith('g_render_') or k in ('g_diag', 'g_vblanks', 'gba_irq', 'gba_main', '_start', 'gba_runtime_selftest', 'dok_gba_assets', 'dok_gba_assets_end')},
                  sections=sections, header_checksum=rom[189],
                  ewram_static_bytes=symbols['__bss_end'] - symbols['__data_start'])
    Path(args.out).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"ROM: {len(rom)} bytes, SHA-256 {report['rom_sha256']}; static EWRAM: {report['ewram_static_bytes']} bytes")


if __name__ == '__main__':
    main()
