"""Replay a held production draw to count raster work, without changing its ROM.

mGBA provides real initialized RAM. Unicorn replays dok_renderer_draw with
entry hooks; these are operation counts, never GBA cycle/FPS estimates.
Coverage and public draw statistics must match the held mGBA draw exactly.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
for folder in ('scripts', 'tools/originals', 'tools/reports', 'ports/gba/scripts'):
    sys.path.append(str(ROOT / folder))

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import os
import struct

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn import arm_const as R
from research_gba_fps import GameProbe, freeze
from verify_gba import sha


def replay(probe, level):
    symbols = probe.symbols
    cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    for address, size in ((0x02000000, 0x40000), (0x03000000, 0x8000),
                          (0x04000000, 0x1000), (0x08000000, 0x2000000)):
        cpu.mem_map(address, size)
    cpu.mem_write(0x08000000, probe.rom.read_bytes())
    for address, size in ((0x02000000, 0x40000), (0x03000000, 0x8000)):
        cpu.mem_write(address, probe.frontend.read(address, size))
    read32 = lambda address: struct.unpack('<I', cpu.mem_read(address, 4))[0]
    words = lambda address, count: struct.unpack('<' + 'I' * count, cpu.mem_read(address, 4 * count))
    reg = lambda number: cpu.reg_read(getattr(R, f'UC_ARM_REG_R{number}'))
    totals = Counter()
    primitive = ['other']
    row_steps = defaultdict(Counter)
    row_lights = defaultdict(set)
    division_callers = Counter()
    division_wide = Counter()

    def covered(bits, first, offset):
        row = first + offset
        return bool(read32(bits + (row // 32) * 4) & (1 << (row & 31)))

    def hook(cpu, address, size, unused):
        name = entries[address]
        if name == 'gba_render_stage':
            # Stage timestamps are irrelevant to a functional CPU replay.
            cpu.reg_write(R.UC_ARM_REG_PC, cpu.reg_read(R.UC_ARM_REG_LR))
            return
        if name in ('surface_raster_hw', 'surface_raster_general', 'raster'):
            primitive[0] = 'ceiling' if name == 'surface_raster_hw' else 'vertical'
            totals[name + '_calls'] += 1
            return
        if name in ('dok_gba_plane_divide', '__aeabi_idiv', '__aeabi_uidiv',
                    '__aeabi_idivmod', '__aeabi_uidivmod'):
            totals[primitive[0] + '_' + name] += 1
            if name == 'dok_gba_plane_divide' and reg(0) == 0:
                totals[primitive[0] + '_plane_zero_numerator'] += 1
            caller = (primitive[0], name, cpu.reg_read(R.UC_ARM_REG_LR) & ~1)
            division_callers[caller] += 1
            denominator = reg(1)
            if name in ('__aeabi_idiv', '__aeabi_idivmod') and denominator & 0x80000000:
                denominator = (-denominator) & 0xffffffff
            if name != 'dok_gba_plane_divide' and denominator >= 65536:
                division_wide[caller] += 1
                totals[primitive[0] + '_wide_divisions'] += 1
            return
        if name == 'dok_gba_tiled_surface':
            s = words(reg(0), 15)
            count = (s[6] - s[5]) // 2 + 1
            opened = sum(not (read32(s[12] + k * 16) & s[13]) for k in range(count))
            totals['ceiling_spans'] += 1
            totals['ceiling_samples_requested'] += count
            totals['ceiling_samples_open'] += opened
            totals['ceiling_fully_hidden_spans'] += opened == 0
            totals['ceiling_hidden_span_samples'] += count if not opened else 0
            row_steps[s[7]][s[10:12]] += 1
            row_lights[s[7]].add(s[1])
        elif name == 'dok_gba_tiled_masked_column':
            dst, source, v, step, count, bits, mask, tile_y = words(reg(0), 8)
            first = mask.bit_length() - 1
            covered_count = zero = 0
            for k in range(count):
                if covered(bits, first, k):
                    covered_count += 1
                elif cpu.mem_read(source + (((v + k * step) & 0xffffffff) >> 16), 1)[0] == 0:
                    zero += 1
            totals['masked_columns'] += 1
            totals['masked_samples_requested'] += count
            totals['masked_samples_covered'] += covered_count
            totals['masked_samples_open_zero'] += zero
            totals['masked_samples_open_nonzero'] += count - covered_count - zero
            if 0x03006000 <= source < 0x03008000:
                totals['runtime_lit_columns'] += 1
                totals['runtime_lit_samples_prepared'] += count
                totals['runtime_lit_samples_already_covered'] += covered_count

    names = ('gba_render_stage', 'surface_raster_hw', 'surface_raster_general', 'raster',
             'dok_gba_plane_divide', '__aeabi_idiv', '__aeabi_uidiv',
             '__aeabi_idivmod', '__aeabi_uidivmod',
             'dok_gba_tiled_surface', 'dok_gba_tiled_masked_column')
    entries = {symbols[name] & ~1: name for name in names if name in symbols}
    def invalid(cpu, access, address, size, value, unused):
        raise AssertionError(('Invalid replay memory', hex(cpu.reg_read(R.UC_ARM_REG_PC)), hex(address), size))
    cpu.hook_add(UC_HOOK_MEM_INVALID, invalid)
    for address in entries:
        cpu.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)
    sp, stop = 0x03007d00, 0x03007ff0
    args = (symbols['g_render_frame'], symbols['g_render_world'], symbols['package'], level,
            0, symbols['camera'], symbols['g_render_stats'])
    cpu.reg_write(R.UC_ARM_REG_CPSR, 0x1f)
    for number, value in enumerate(args[:4]):
        cpu.reg_write(getattr(R, f'UC_ARM_REG_R{number}'), value)
    cpu.reg_write(R.UC_ARM_REG_SP, sp)
    cpu.mem_write(sp, struct.pack('<3I', *args[4:]))
    cpu.reg_write(R.UC_ARM_REG_LR, stop)
    cpu.emu_start(symbols['dok_renderer_draw'], stop, count=10000000)
    assert cpu.reg_read(R.UC_ARM_REG_PC) == stop, 'Replay did not return'
    assert reg(0) == 0, ('Renderer status', reg(0))
    assert cpu.reg_read(R.UC_ARM_REG_SP) == sp
    coverage = bytes(cpu.mem_read(symbols['coverage_bits'], 120 * 16))
    assert coverage == probe.frontend.read(symbols['coverage_bits'], len(coverage)), 'Coverage differs'
    stats = bytes(cpu.mem_read(symbols['g_render_stats'], 44))
    assert stats == probe.frontend.read(symbols['g_render_stats'], 44), 'Statistics differ'
    # UI/weapon overlays are written after draw, so this replay checks coverage
    # and statistics. Actual PPU timing is measured by the separate mGBA sweep.
    return dict(counts=dict(totals), coverage_exact=True, stats_exact=True,
                coverage_sha256=hashlib.sha256(coverage).hexdigest(),
                ceiling_rows=[dict(y=y, spans=sum(steps.values()), unique_uv_steps=len(steps),
                                   light_variants=len(row_lights[y])) for y, steps in sorted(row_steps.items())],
                division_callers=[dict(primitive=kind, function=name, return_address=hex(address), calls=count,
                                       wide_divisors=division_wide[kind, name, address])
                                  for (kind, name, address), count in division_callers.most_common()])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--cases', nargs='+', default=['1:520:225', '1:3640:225', '1:2242:90', '2:520:225', '3:520:225'])
    args = parser.parse_args()
    os.chdir(ROOT)
    manifest = json.loads((args.build_dir.parent / 'prototype-build.json').read_text(encoding='utf-8'))
    defines = manifest['variants'][args.build_dir.name]['prototype_defines']
    assert {'HW_COVER4', 'HW_VERTICAL128', 'HW_DIRECT', 'HW_SURFACE_PRELIT'} <= set(defines)
    assert not {'HW_ROWS96', 'HW_ROWS72', 'SECTOR_CLIP', 'COLUMN_PIPELINE', 'STREAM_BSP'} & set(defines)
    os.environ['DOK_PROBE_FAST_VIDEO'] = '1'
    core = Path(os.environ['LOCALAPPDATA']) / 'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    probe = GameProbe(core, args.build_dir)
    rows = []
    try:
        probe.until(lambda d: d['draws'] >= 1)
        probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
        probe.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(probe, True)
        for case in args.cases:
            level, cell, heading = map(int, case.split(':'))
            probe.camera(level % 3, 15120, -300, 22800, 90)
            probe.camera(level - 1, cell % 64 * 480 + 240, -300, cell // 64 * 480 + 240, heading)
            for _ in range(8):
                probe.present()
            row = replay(probe, level - 1)
            row.update(level=level, cell=cell, heading=heading)
            rows.append(row)
            print(case, row['counts'], flush=True)
        result = dict(rom_sha256=sha(probe.rom), elf_sha256=sha(args.build_dir / 'dok-diagnostic.elf'),
                      core_sha256=sha(core), script_sha256=sha(__file__), rows=rows,
                      scope='Frozen RU steady draws; production binary replay; counts only, no cycle or FPS claim.')
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
