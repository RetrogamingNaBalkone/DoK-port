"""Replay the installed SDK counter instructions with explicit MMIO fixtures.

This tests a hypothesized register-ordering failure, not DraStic itself.
No game state, original EXE, SDK installation or ROM is modified.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_CPSR, UC_ARM_REG_LR, UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from run_diag_emulator import elf_symbol


def main():
    target = Path(sys.argv[1]).resolve()
    elf = target / 'dok_nds_gameplay.elf'
    data = elf.read_bytes()
    address, length = elf_symbol(elf, 'systemCounterGetTicks')
    # ELF32 section table; resolve code by virtual address, not file offset.
    table = struct.unpack_from('<I', data, 32)[0]
    stride, count = struct.unpack_from('<HH', data, 46)
    code = None
    for i in range(count):
        section = struct.unpack_from('<10I', data, table + i * stride)
        if section[1] != 8 and section[3] <= address < section[3] + section[5]:
            offset = section[4] + address - section[3]
            code = data[offset:offset + length]
            break
    assert code and len(code) == 96, (address, length)
    # Literal pool of this pinned SDK ARM implementation.
    software, timer_base = struct.unpack_from('<II', code, 88)
    assert timer_base == 0x04000100

    def sample(base, first, flags, second):
        cpu = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        for start, size in ((0x01000000, 0x10000), (0x02000000, 0x400000), (0x04000000, 0x1000)):
            cpu.mem_map(start, size)
        cpu.mem_write(address, code)
        cpu.mem_write(software, struct.pack('<Q', base))
        reads = []
        values = iter(((0x04000108, 2, first), (0x04000214, 4, flags), (0x04000108, 2, second)))

        def read(uc, access, at, size, value, context):
            if 0x04000000 <= at < 0x04001000:
                expected, width, payload = next(values)
                assert (at, size) == (expected, width)
                uc.mem_write(at, payload.to_bytes(size, 'little'))
                reads.append(dict(address=hex(at), bytes=size, value=payload))

        cpu.hook_add(UC_HOOK_MEM_READ, read)
        cpu.reg_write(UC_ARM_REG_CPSR, 0x13)
        cpu.reg_write(UC_ARM_REG_SP, 0x023ff000)
        cpu.reg_write(UC_ARM_REG_LR, 0x0100fff0)
        cpu.emu_start(address, 0x0100fff0, count=100)
        assert len(reads) == 3
        result = cpu.reg_read(UC_ARM_REG_R0) | cpu.reg_read(UC_ARM_REG_R1) << 32
        return dict(base=hex(base), reads=reads, result=f'{result:016X}'), result

    base = 0x27f20000
    fixtures = (
        ('before_wrap', base, 0xfffc, 0, 0xfffd, base + 0xfffd),
        ('pending_irq', base, 0, 1 << 5, 1, base + 0x10001),
        ('wrap_between_reads', base, 0xffff, 0, 0, base + 0x10000),
        ('serviced_irq', base + 0x10000, 1, 0, 2, base + 0x10002),
        ('hypothesis_delayed_if', base, 0, 0, 0, base),
    )
    rows = {}
    for name, counter, first, flags, second, expected in fixtures:
        row, value = sample(counter, first, flags, second)
        assert value == expected, (name, value, expected)
        rows[name] = row
    backward = int(rows['before_wrap']['result'], 16) - int(rows['hypothesis_delayed_if']['result'], 16)
    assert backward == 65533
    report = dict(passed=True, scope='Actual SDK ARM instructions, synthetic MMIO; not a DraStic reproduction',
                  elf_sha256=hashlib.sha256(data).hexdigest(), function_address=hex(address),
                  function_sha256=hashlib.sha256(code).hexdigest(), fixtures=rows,
                  hypothetical_backward_ticks=backward, hypothetical_backward_seconds=backward * 64 / 33513982)
    out = target / 'sdk-counter-boundary.json'
    out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
