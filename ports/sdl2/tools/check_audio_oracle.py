"""SP13 OG live-loop/zero-IRQ vectors execute fresh original x86 instructions.

Hardware begin/stop are explicit stubs; IRQ IN/OUT ports are recorded. This is
separate from actual DOS WAV capture and does not claim analog equivalence.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INSN
from unicorn import x86_const as R

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp13-audio'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Oracle:
    def __init__(self, count, loop):
        self.cpu = c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        folder = OUT / 'ghidra/old-gold'
        manifest = json.loads((folder / 'manifest.json').read_text())
        for name, address, length in manifest['ranges']:
            raw = (folder / (name + '.bin')).read_bytes()
            assert sha(folder / (name + '.bin')) == manifest['bytes_sha256'][name]
            segment, offset = (int(part, 16) for part in address.split(':'))
            c.mem_write(segment * 16 + offset, raw)
        self.dac = 128
        self.ports = []
        c.hook_add(UC_HOOK_CODE, self.hook)
        c.hook_add(UC_HOOK_INSN, lambda *args: 0, None, 1, 0, R.UC_X86_INS_IN)
        c.hook_add(UC_HOOK_INSN, self.output, None, 1, 0, R.UC_X86_INS_OUT)
        c.mem_write(0x32550 + 0xac, struct.pack('<I', 0x90000000))
        c.mem_write(0x90000, struct.pack('<H', 0x200))
        c.mem_write(0x32550 + 0x214, struct.pack('<I', 0x70000000))
        c.mem_write(0x32550 + 0x218, struct.pack('<I', 0x70000010))
        c.mem_write(0x32550 + 0xd5e, struct.pack('<H', 0x22c))
        samples = bytes(32 + 32 * i for i in range(count)) + b'\0'
        c.mem_write(0x93000, samples)
        for pointer in [0x91000, 0x92000]:
            c.mem_write(pointer + 7, b'\x09')
            c.mem_write(pointer + 0x14, struct.pack('<II', len(samples), 0x90003000))
        self.play(1, loop)

    def ret(self):
        c = self.cpu
        sp = c.reg_read(R.UC_X86_REG_SP)
        ip, cs = struct.unpack('<HH', c.mem_read(0x30000 + sp, 4))
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def hook(self, c, address, size, unused):
        if address == 0x7fff0:
            c.emu_stop()
        elif address in [0x70000, 0x70010]:
            self.ret()

    def output(self, c, port, size, value, unused):
        self.ports.append((port, value))
        if port == 0x22c and value != 0x10:
            self.dac = value

    def invoke(self, segment, offset, args=b'', irq=False):
        c = self.cpu
        stack = struct.pack('<HH', 0xfff0, 0x7000) + (b'\x02\x00' if irq else args)
        c.mem_write(0x3f000, stack)
        for register, value in [(R.UC_X86_REG_CS, segment), (R.UC_X86_REG_DS, 0x3255),
                                (R.UC_X86_REG_SS, 0x3000), (R.UC_X86_REG_SP, 0xf000),
                                (R.UC_X86_REG_BP, 0), (R.UC_X86_REG_EFLAGS, 2)]:
            c.reg_write(register, value)
        c.emu_start(segment * 16 + offset, 0, count=30000)
        assert c.reg_read(R.UC_X86_REG_IP) == 0xfff0, 'Original audio handler did not return'

    def play(self, identity, loop):
        self.invoke(0x18a6, 0x22a if loop else 0x1f7, struct.pack('<I', 0x90000000 + identity * 0x1000))

    def active(self):
        pointer = struct.unpack('<I', self.cpu.mem_read(0x32550 + 0xa0, 4))[0]
        if not pointer:
            return 0
        linear = (pointer >> 16) * 16 + (pointer & 65535)
        return (1 if pointer == 0x90001000 else 2) if self.cpu.mem_read(linear + 7, 1)[0] & 0x80 else 0

    def tick(self):
        if self.active():
            self.invoke(0x2b41, 0x77, irq=True)
        identity = self.active()
        # Native stopped DAC retains its voltage. Port's deliberate silence after
        # logical EOF is compared separately; it is not an analog DAC replica.
        return [identity, (self.dac - 128) * 256 if identity else 0]


def native_period(profile, frequency):
    c = Uc(UC_ARCH_X86, UC_MODE_16)
    c.mem_map(0, 0x100000)
    folder = OUT / 'ghidra' / profile
    manifest = json.loads((folder / 'manifest.json').read_text())
    for name, address, length in manifest['ranges']:
        segment, offset = (int(part, 16) for part in address.split(':'))
        if profile == 'old-gold':
            c.mem_write(segment * 16 + offset, (folder / (name + '.bin')).read_bytes())
        elif name == 'hardware':
            c.mem_write(0x50000, (folder / (name + '.bin')).read_bytes())
    outputs = []

    def code(cpu, address, size, unused):
        if address == 0x7fff0:
            cpu.emu_stop()
        elif profile == 'old-gold' and address in [0x2a846, 0x11151]:
            sp = cpu.reg_read(R.UC_X86_REG_SP)
            ip, cs = struct.unpack('<HH', cpu.mem_read(0x30000 + sp, 4))
            cpu.reg_write(R.UC_X86_REG_SP, sp + 4)
            cpu.reg_write(R.UC_X86_REG_CS, cs)
            cpu.reg_write(R.UC_X86_REG_IP, ip)

    c.hook_add(UC_HOOK_CODE, code)
    c.hook_add(UC_HOOK_INSN, lambda *args: 0, None, 1, 0, R.UC_X86_INS_IN)
    c.hook_add(UC_HOOK_INSN, lambda cpu, port, size, value, unused: outputs.append((port, value)), None, 1, 0, R.UC_X86_INS_OUT)
    if profile == 'old-gold':
        segment, offset, ds = 0x180b, 2, 0x3255
        c.mem_write(0x32550 + 0x9e, b'\x01\x00')
        args = struct.pack('<HHH', 0x77, 0x2b41, frequency)
    else:
        segment, offset, ds = 0x5000, 0x222, 0x3000
        delta = 0 if profile == 'ru' else 0xa0
        for at, value in [(0xd2c, 0x22c), (0xd2e, 0x22e), (0xd30, 0x22e)]:
            c.mem_write(0x30000 + at + delta, struct.pack('<H', value))
        args = struct.pack('<HHHH', 0, 0x9000, 32, frequency)
    c.mem_write(0x3f000, struct.pack('<HH', 0xfff0, 0x7000) + args)
    for register, value in [(R.UC_X86_REG_CS, segment), (R.UC_X86_REG_DS, ds),
                            (R.UC_X86_REG_SS, 0x3000), (R.UC_X86_REG_SP, 0xf000),
                            (R.UC_X86_REG_BP, 0), (R.UC_X86_REG_EFLAGS, 2)]:
        c.reg_write(register, value)
    c.emu_start(segment * 16 + offset, 0, count=30000)
    assert c.reg_read(R.UC_X86_REG_IP) == 0xfff0, 'Original rate handler did not return'
    if profile == 'old-gold':
        divisor = [value for port, value in outputs if port == 0x40]
        assert len(divisor) == 2 and divisor[1] == 0
        return divisor[0] or 65536
    dsp = [value for port, value in outputs if port == 0x22c]
    assert dsp[0] == 0x40, outputs
    return (-dsp[1]) & 255 or 256


def main():
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())['profiles']
    before = {p['sample']: sha(ROOT / p['sample']) for p in inventory}
    result = subprocess.run([str(ROOT / 'build/sdl2/windows/dok_audio_probe.exe'), '--native-matrix'],
                            text=True, capture_output=True, check=True)
    (OUT / 'native-cpp.csv').write_text(result.stdout)
    vectors = []
    for line in result.stdout.splitlines():
        values = list(map(int, line.split(',')))
        count, initial, request, identity = values[:4]
        native = Oracle(count, bool(initial))
        expected = []
        for tick in range(7):
            if tick == 1 and request >= 0:
                native.play(identity + 1, bool(request))
            expected += native.tick()
        vectors.append(dict(input=values[:4], native=expected, port=values[4:], equal=expected == values[4:]))
    after = {p: sha(ROOT / p) for p in before}
    assert before == after
    rates_result = subprocess.run([str(ROOT / 'build/sdl2/windows/dok_audio_probe.exe'), '--rate-matrix'],
                                 text=True, capture_output=True, check=True)
    (OUT / 'rate-cpp.csv').write_text(rates_result.stdout)
    rates = []
    for line in rates_result.stdout.splitlines():
        og, frequency, port = map(int, line.split(','))
        for profile in (['old-gold'] if og else ['ru', 'en']):
            native = native_period(profile, frequency)
            rates.append(dict(profile=profile, frequency=frequency, port=port, native=native, equal=port == native))
    report = dict(vectors=vectors, count=len(vectors), mismatches=sum(not v['equal'] for v in vectors),
                  rates=rates, rate_count=len(rates), rate_mismatches=sum(not v['equal'] for v in rates),
                  sha256_before=before, sha256_after=after,
                  boundary='Actual OG wrappers/admission/IRQ instructions; hardware begin/stop stubs, IN ready, recorded OUT bytes; modern silence at inactive DAC boundary')
    (OUT / 'oracle-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(report['count'], 'OG wrapper/IRQ vectors;', report['mismatches'], 'mismatches')
    for vector in vectors:
        if not vector['equal']:
            print(vector)
            break
    assert not report['mismatches']
    assert not report['rate_mismatches'], [v for v in rates if not v['equal']]


if __name__ == '__main__':
    main()
