"""N09 isolated PKR/PKE x86 motion oracle; no NDS/GBA motion model imported.

Actual movement arithmetic, the three-substep wrapper and trig candidate execute
from read-only Ghidra exports. The stopwatch is injected; collision accepts the
candidate. A controlled 64x64 fixture lets native occupancy writes execute, but
does not prove original collision, door, entity or live DOS-session behavior.
"""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/nds/n09'
import unicorn
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INSN
from unicorn import x86_const as R

SAMPLES = {
    'ru': ('originals/pkr/RUS/PKR.EXE', '12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'),
    'en': ('originals/pkr/ENG/PKE.EXE', 'cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b'),
}
LAYOUT = {
    'ru': dict(actor='1068', world='1098', commands='10c0', keyboard='1070',
               entry=0xa85, wrapper=0xeb1, candidate=0xe13, collision=0x901,
               clock=0x1130, read=0x26f, stop=0x2b6, start=0x239,
               world_pointer=0x266a, tiles=0x718e, occupancy=0x608a,
               input_entry=0x1639, key_read=0x25f, key_edge=0x2ab, trig=0x1180),
    'en': dict(actor='1020', world='1030', commands='1050', keyboard='1028',
               entry=0xf8f, wrapper=0x13bb, candidate=0xef7, collision=0x9e5,
               clock=0x1078, read=0x1905, stop=0x194c, start=0x18cf,
               world_pointer=0x271a, tiles=0x723e, occupancy=0x613a,
               input_entry=0x199c, key_read=0x26b, key_edge=0x2b7, trig=0x10c0),
}
# Signedness is part of the transport, not inferred from Python values.
FIELDS = [
    ('flags', 0x1c, 'I'), ('x', 0x26, 'i'), ('y', 0x2a, 'i'), ('z', 0x2e, 'i'),
    ('heading', 0x32, 'h'), ('angle_parameter', 0x34, 'h'), ('speed', 0x38, 'i'),
    ('distance', 0x3c, 'i'), ('turn_step', 0x7c, 'h'), ('linear_step', 0x7e, 'h'),
    ('last_turn', 0x82, 'h'), ('movement_accumulator', 0x90, 'i'),
    ('turn_accumulator', 0x94, 'i'), ('accel_sign', 0x98, 'h'), ('last_flags', 0x9a, 'I'),
]
INPUT_FIELDS = ['flags', 'elapsed', 'x', 'y', 'z', 'heading', 'angle_parameter', 'speed',
                'movement_accumulator', 'turn_accumulator', 'last_turn', 'last_flags',
                'distance', 'turn_step', 'linear_step', 'accel_sign']
OUTPUT_FIELDS = [name for name, _, _ in FIELDS] + ['moved', 'substep_count']


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pin(path):
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size, sha256=sha(path.read_bytes()))


def sources():
    result = {lang: sha((ROOT / path).read_bytes()) for lang, (path, _) in SAMPLES.items()}
    if any(result[lang] != expected for lang, (_, expected) in SAMPLES.items()):
        raise ValueError('Original executable hash mismatch')
    return result


def output(path):
    path = Path(path).absolute()
    if '..' in path.parts or not path.is_relative_to(OUT) or path == OUT:
        raise ValueError('N09 oracle outputs must remain under research/evidence/nds/n09')
    for ancestor in [path, *path.parents]:
        if ancestor == ROOT.parent:
            break
        if ancestor.is_symlink() or (ancestor.exists() and ancestor.is_junction()):
            raise ValueError('N09 output alias rejected')
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError('N09 output must be an unaliased regular file')
    if path.resolve() != path:
        raise ValueError('N09 output resolution differs')
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def state(**changes):
    result = dict(flags=0, elapsed=416, x=15120, y=-300, z=22800, heading=90,
                  angle_parameter=5, speed=120, distance=0, turn_step=0,
                  linear_step=0, last_turn=0, movement_accumulator=0,
                  turn_accumulator=0, accel_sign=1, last_flags=0)
    result.update(changes)
    return result


class Original:
    def __init__(self, language):
        self.language, self.layout = language, LAYOUT[language]
        c = self.cpu = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        folder = OUT / 'original' / language
        manifest = json.loads((folder / 'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['read_only'] is True
        assert manifest['script_sha256'] == sha((ROOT / 'tools/ghidra/ExportNdsMovement.java').read_bytes())
        assert manifest['driver_sha256'] == sha((ROOT / 'ports/nds/tools/export_movement_evidence.ps1').read_bytes())
        assert manifest['sha256_before'] == manifest['sha256_after'] == SAMPLES[language][1]
        self.pins = [pin(folder / 'analysis-manifest.json'), pin(folder / 'evidence.asm')]
        for role, address in [('actor', 0x50000), ('world', 0x60000), ('commands', 0x70000)]:
            path = folder / (self.layout[role] + '.bin')
            raw = path.read_bytes()
            assert manifest['binary_sha256'][self.layout[role]] == sha(raw)
            c.mem_write(address, raw)
            self.pins.append(pin(path))
        # Independently recorded original runtime table, not a GBA/NDS table.
        ram_path = ROOT / 'research/evidence/reference/ru-03/level1-memory.bin'
        ram = ram_path.read_bytes()
        assert sha(ram) == 'c8ff1347518aae86af31b8ed2c6f6443c4c63c9157b4a52bf76d53c7f5c95629'
        c.mem_write(self.layout['trig'] * 16, ram[0x1ca9b0:0x1ca9b0 + 722 * 4])
        self.trig_pin = pin(ram_path)
        # Controlled free-flight world: dimensions64x64, cell480, stride64,
        # all tile IDs0 -> class0 sentinel. Native wrapper updates occupancy.
        c.mem_write(0x30000 + self.layout['world_pointer'], struct.pack('<HH', 0, 0x9000))
        c.mem_write(0x90000, struct.pack('<H', 64))
        c.mem_write(0x90004, struct.pack('<H', 64))
        c.mem_write(0x9030e, struct.pack('<HH', 0, 0x8000))
        c.mem_write(0x90738, struct.pack('<h', 480))
        c.mem_write(0x90742, struct.pack('<I', 64))
        c.hook_add(UC_HOOK_CODE, self.hook)
        self.elapsed = 0
        self.stopped = False
        self.substeps, self.wrapper_calls, self.clock_calls = [], [], []
        self.keys = self.edges = set()

    def reg(self, name):
        return self.cpu.reg_read(getattr(R, 'UC_X86_REG_' + name))

    def ret(self, value=None):
        c, sp = self.cpu, self.reg('SP')
        ip, cs = struct.unpack('<HH', c.mem_read(0x30000 + sp, 4))
        if value is not None:
            c.reg_write(R.UC_X86_REG_AX, value & 65535)
            c.reg_write(R.UC_X86_REG_DX, (value >> 16) & 65535)
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def hook(self, c, address, size, unused):
        p = self.layout
        if address == 0x5fff0:
            self.stopped = True
            c.emu_stop()
        elif address in [p['clock'] * 16 + p[key] for key in ('stop', 'read', 'start')]:
            role = next(key for key in ('stop', 'read', 'start') if address == p['clock'] * 16 + p[key])
            self.clock_calls.append(role)
            self.ret(self.elapsed if role == 'read' else None)
        elif address == 0x50000 + p['wrapper']:
            sp = self.reg('SP')
            actor_off, actor_seg, distance, strafe = struct.unpack('<HHhH', c.mem_read(0x30000 + sp + 4, 8))
            assert (actor_off, actor_seg) == (0x100, 0x4000)
            self.wrapper_calls.append(dict(distance=distance, strafe=strafe))
        elif address == int(p['world'], 16) * 16 + p['candidate']:
            # Redirect original far target to the same unmodified exported bytes.
            sp = self.reg('SP')
            strafe = struct.unpack('<H', c.mem_read(0x30000 + sp + 12, 2))[0]
            distance = struct.unpack('<i', c.mem_read(0x4013c, 4))[0]
            heading = struct.unpack('<h', c.mem_read(0x40132, 2))[0]
            index = (heading + (90 if strafe else 0) + 32768) % 65536 - 32768
            if index >= 360:
                index -= 360
            if index < 0:
                index += 360
            if not 0 <= index <= 360:
                raise ValueError(f'Unsafe original trig index {index}; scalar-only vector required')
            self.substeps.append(dict(distance=distance, strafe=strafe))
            c.reg_write(R.UC_X86_REG_CS, 0x6000)
            c.reg_write(R.UC_X86_REG_IP, p['candidate'])
        elif address == 0x60000 + p['collision']:
            sp = self.reg('SP')
            xoff, xseg, zoff, zseg, x, z = struct.unpack('<HHHHii', c.mem_read(0x30000 + sp + 8, 16))
            self.substeps[-1].update(x=x, z=z)
            c.mem_write(xseg * 16 + xoff, struct.pack('<i', x))
            c.mem_write(zseg * 16 + zoff, struct.pack('<i', z))
            self.ret()
        elif address in [int(p['keyboard'], 16) * 16 + p[key] for key in ('key_read', 'key_edge')]:
            sp = self.reg('SP')
            scan = struct.unpack('<H', c.mem_read(0x30000 + sp + 8, 2))[0]
            self.ret(int(scan in (self.keys if address == int(p['keyboard'], 16) * 16 + p['key_read'] else self.edges)))

    def call(self, address, arguments, segment=0x5000):
        c = self.cpu
        for reg, value in [('CS', segment), ('SS', 0x3000), ('DS', 0x3000), ('ES', 0x4000),
                           ('SP', 0xf000), ('BP', 0xee00), ('EFLAGS', 2)]:
            c.reg_write(getattr(R, 'UC_X86_REG_' + reg), value)
        c.mem_write(0x3f000, struct.pack('<HH', 0xfff0, 0x5000) + arguments)
        self.stopped = False
        c.emu_start(segment * 16 + address, 0x5fff1, count=100000)
        if not self.stopped or self.reg('SP') != 0xf004:
            raise ValueError('Original execution did not return via balanced far ABI')
        return self.reg('AX')

    def run(self, values):
        data = bytearray(0xde)
        for name, offset, fmt in FIELDS:
            struct.pack_into('<' + fmt, data, offset, values[name])
        data[0x5c] = 0x2a
        self.cpu.mem_write(0x40100, bytes(data))
        self.elapsed = values['elapsed']
        self.substeps, self.wrapper_calls, self.clock_calls = [], [], []
        moved = self.call(self.layout['entry'], struct.pack('<HH', 0x100, 0x4000))
        raw = self.cpu.mem_read(0x40100, 0xde)
        result = {name: struct.unpack_from('<' + fmt, raw, offset)[0] for name, offset, fmt in FIELDS}
        result.update(moved=moved, substep_count=len(self.substeps))
        assert self.clock_calls == ['stop', 'read', 'start']
        assert len(self.substeps) == 3 * len(self.wrapper_calls)
        for index, call in enumerate(self.wrapper_calls):
            for step in self.substeps[index * 3:index * 3 + 3]:
                assert step['distance'] == call['distance'] >> 2 and step['strafe'] == call['strafe']
        return dict(state=result, wrappers=list(self.wrapper_calls), substeps=list(self.substeps))

    def commands(self, keys, edges):
        self.keys, self.edges = set(keys), set(edges)
        self.cpu.mem_write(0x40200, bytes(0x100))
        self.call(self.layout['input_entry'], struct.pack('<HH', 0x200, 0x4000), segment=0x7000)
        return struct.unpack('<I', self.cpu.mem_read(0x40228, 4))[0]


class OriginalKeyboard:
    """Actual IRQ1 and both readers, sharing the original data segment.

    Only hardware port values are injected; no reader/state transitions are
    replaced by Python. Synthetic IRQ return stack is explicit16-bit IRET.
    """
    def __init__(self, language):
        self.language, self.layout = language, LAYOUT[language]
        self.cpu = c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        folder = OUT / 'original' / language
        c.mem_write(0x50000, (folder / (self.layout['keyboard'] + '.bin')).read_bytes())
        c.mem_write(0x70000, (folder / (self.layout['commands'] + '.bin')).read_bytes())
        self.data_segment = 0x1190 if language == 'ru' else 0x10d0
        self.table = self.data_segment * 16 + (0x2155 if language == 'ru' else 0x2205)
        self.irq = 0x109 if language == 'ru' else 0x115
        self.scan = 0
        self.stopped = False
        self.ports = []
        c.hook_add(UC_HOOK_CODE, self.hook)
        c.hook_add(UC_HOOK_INSN, self.port_in, None, 1, 0, R.UC_X86_INS_IN)
        c.hook_add(UC_HOOK_INSN, self.port_out, None, 1, 0, R.UC_X86_INS_OUT)

    def hook(self, c, address, size, unused):
        if address == 0x5fff0:
            self.stopped = True
            c.emu_stop()
        else:
            for role in ('key_read', 'key_edge'):
                if address == int(self.layout['keyboard'], 16) * 16 + self.layout[role]:
                    c.reg_write(R.UC_X86_REG_CS, 0x5000)
                    c.reg_write(R.UC_X86_REG_IP, self.layout[role])

    def port_in(self, c, port, size, unused):
        assert size == 1 and port in (0x60, 0x61)
        self.ports.append(['in', port, self.scan if port == 0x60 else 0])
        return self.scan if port == 0x60 else 0

    def port_out(self, c, port, size, value, unused):
        assert size == 1 and (port, value) in ((0x61,0x80),(0x61,0),(0x20,0x20))
        self.ports.append(['out', port, value])

    def call(self, offset, arguments=b'', segment=0x5000, interrupt=False):
        c = self.cpu
        for reg, value in [('CS',segment),('SS',self.data_segment),('DS',self.data_segment),
                           ('ES',0x9000),('SP',0xf000),('BP',0xee00),('EFLAGS',2)]:
            c.reg_write(getattr(R,'UC_X86_REG_'+reg),value)
        stack = struct.pack('<HH',0xfff0,0x5000)+(struct.pack('<H',2) if interrupt else b'')+arguments
        c.mem_write(self.data_segment*16+0xf000,stack)
        self.stopped = False
        c.emu_start(segment*16+offset,0x5fff1,count=50000)
        assert self.stopped and c.reg_read(R.UC_X86_REG_SP)==0xf000+(6 if interrupt else 4)
        return c.reg_read(R.UC_X86_REG_AX)

    def operation(self, operation):
        if operation[0] == 'irq':
            self.scan = operation[1]
            self.call(self.irq, interrupt=True)
            return dict(operation=list(operation), space=self.cpu.mem_read(self.table+0x39,1)[0],
                        enter=self.cpu.mem_read(self.table+0x1c,1)[0])
        self.cpu.mem_write(0x90200, bytes(0x100))
        self.call(self.layout['input_entry'],struct.pack('<HH',0x200,0x9000),segment=0x7000)
        return dict(operation=['poll'],commands=struct.unpack('<I',self.cpu.mem_read(0x90228,4))[0],
                    space=self.cpu.mem_read(self.table+0x39,1)[0],enter=self.cpu.mem_read(self.table+0x1c,1)[0])


def keyboard_proof(language):
    sequences = []
    for name, operations, expected in [
            ('space-held-release',[('irq',0x39),('poll',),('poll',),('irq',0xb9),('poll',),('poll',)],[0,0,0x800,0]),
            ('enter-held-release',[('irq',0x1c),('poll',),('poll',),('irq',0x9c),('poll',),('poll',)],[0,0,0x800,0]),
            ('both-held-release',[('irq',0x39),('irq',0x1c),('poll',),('irq',0xb9),('irq',0x9c),('poll',),('poll',)],[0,0x800,0]),
            ('space-short-tap',[('irq',0x39),('irq',0xb9),('poll',),('poll',)],[0x800,0])]:
        keyboard = OriginalKeyboard(language)
        rows = [keyboard.operation(op) for op in operations]
        assert [row['commands'] for row in rows if 'commands' in row] == expected
        sequences.append(dict(name=name,operations=rows,ports=keyboard.ports))
    states = []
    for value in range(4):
        for role in ('key_read','key_edge'):
            keyboard = OriginalKeyboard(language)
            keyboard.cpu.mem_write(keyboard.table+0x39,bytes([value]))
            result=keyboard.call(keyboard.layout[role],struct.pack('<HHH',0,0,0x39))
            remaining=keyboard.cpu.mem_read(keyboard.table+0x39,1)[0]
            assert result == (value if role == 'key_read' else int(value == 2))
            assert remaining == (value & 1 if role == 'key_read' or value == 2 else value)
            states.append(dict(reader=role,before=value,result=result,after=remaining))
    return dict(sequences=sequences,reader_states=states,
                semantics='IRQ make3; heldUse poll false; break AND2; Use reader consumes state2 on release; no held-repeat',
                keyboard_export=pin(OUT/'original'/language/(LAYOUT[language]['keyboard']+'.bin')))


def cases():
    result = []
    commands = [0, 4, 8, 12, 16, 32, 48, 0x80000, 0x100000, 0x180000,
                0x80010, 0x100020, 0x180030, 20, 24, 0x2010, 0x4000010, 0x4002010]
    for heading, flags, elapsed, accumulator in itertools.product(
            [0, 1, 45, 89, 90, 135, 179, 180, 225, 269, 270, 315, 358, 359],
            commands, [0, 1, 100, 415, 416, 417, 832, 1248, 20000], [0, 10000]):
        result.append(state(flags=flags, heading=heading, elapsed=elapsed,
                            movement_accumulator=accumulator, turn_accumulator=accumulator,
                            last_flags=16, distance=-37, turn_step=-7, linear_step=17))
    for heading, flags in itertools.product(range(361), [16, 32, 0x80000, 0x100000,
                                                         0x80010, 0x100010, 0x80020, 0x100020]):
        result.append(state(flags=flags, heading=heading, movement_accumulator=10000,
                            turn_accumulator=10000))
    rng = random.Random(0x4e3039)
    for _ in range(2500):
        flags = rng.choice(commands) | rng.choice([0, 0x200, 0x800, 0x10000000])
        speed = rng.choice([120, 0, -1, 32767, 2**31 - 1, -2**31])
        result.append(state(flags=flags,
                            elapsed=rng.choice([0, 416, 1000, 0x7fffffff, 0x80000000, 0xffffff, 0xffffffff]),
                            x=rng.choice([-2**31, 2**31 - 1, rng.randrange(-100000, 100000)]),
                            z=rng.choice([-2**31, 2**31 - 1, 22800]), heading=rng.randrange(360),
                            speed=0 if flags & 12 else speed,
                            movement_accumulator=rng.choice([0, 1, 9999, 10000, 2**31 - 1, -2**31]),
                            turn_accumulator=rng.choice([0, 1, 10000, 2**31 - 1, -2**31]),
                            last_turn=rng.randrange(2), last_flags=rng.choice(commands),
                            distance=-123, turn_step=-17, linear_step=15, accel_sign=-1))
    # Invalid table index states are scalar-only: speed0 guarantees no movement.
    for heading, angle, flags in itertools.product([-32768, -1, 32767], [-32768, -1, 32767], [0, 4, 8, 12]):
        result.append(state(heading=heading, angle_parameter=angle, flags=flags,
                            speed=0, turn_accumulator=10000))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, help='Optional independently implemented NDS host probe executable')
    parser.add_argument('--output', type=Path, default=OUT / 'original-motion.json')
    args = parser.parse_args()
    before = sources()
    vectors, languages, reference = cases(), [], None
    for language in ('ru', 'en'):
        oracle = Original(language)
        results = [oracle.run(values) for values in vectors]
        if reference is None:
            reference = results
        else:
            assert results == reference, 'PKR/PKE original-instruction results differ'
        # Exhaustive256 combinations of original movement/run/fire held keys,
        # independently observed original command words. Other UI scans omitted.
        scans = [0x38, 0x4b, 0x4d, 0x48, 0x50, 0x1d, 0x2a, 0x36]
        keyboard = [dict(held=[scan for i, scan in enumerate(scans) if mask & (1 << i)],
                         commands=oracle.commands([scan for i, scan in enumerate(scans) if mask & (1 << i)], []))
                    for mask in range(256)]
        use = [oracle.commands([], edge) for edge in [[], [0x39], [0x1c], [0x39, 0x1c]]]
        assert use == [0, 0x800, 0x800, 0x800]
        if language == 'ru':
            keyboard_reference = keyboard
        else:
            assert keyboard == keyboard_reference
        languages.append(dict(language=language, sample=SAMPLES[language][0], sha256_before=before[language],
                              sha256_after=sources()[language], exports=oracle.pins,
                              vector_count=len(results), executed_substeps=sum(r['state']['substep_count'] for r in results),
                              keyboard_cases=len(keyboard), use_cases=use,
                              actual_keyboard=keyboard_proof(language)))
    host = None
    if args.probe:
        payload = ''.join(' '.join(str(values[name]) for name in INPUT_FIELDS) + '\n' for values in vectors)
        run = subprocess.run([str(args.probe.resolve())], input=payload, text=True, capture_output=True, check=True)
        lines = [list(map(int, line.split())) for line in run.stdout.splitlines()]
        expected = [[result['state'][name] for name in OUTPUT_FIELDS] +
                    [step[name] for step in result['substeps'] for name in ('distance', 'strafe', 'x', 'z')]
                    for result in reference]
        assert len(lines) == len(expected), (len(lines), len(expected))
        for index, (got, want) in enumerate(zip(lines, expected)):
            assert got == want, (index, vectors[index], got, want)
        host = dict(executable=pin(args.probe.resolve()), compared_cases=len(expected), fields=OUTPUT_FIELDS,
                    substep_fields=['distance', 'strafe', 'x', 'z'], substeps_compared=True)
    sequences, sequence_inputs, sequence_reference = [], [], []
    for name, commands in [('start_stop', [16] * 30 + [0] * 30),
                            ('cold_start', [16] * 3),
                            ('left_then_right', [4] * 15 + [8] * 15 + [0] * 3),
                            ('opposites', [0x180030] * 10)]:
        current = state()
        steps = []
        ru, en = Original('ru'), Original('en')
        for index, flags in enumerate(commands):
            current['flags'], current['elapsed'] = flags, (0 if name == 'cold_start' and index == 0 else 416)
            sequence_inputs.append(dict(current))
            result = ru.run(current)
            assert en.run(current) == result, (name, index, 'Original sequence differs')
            sequence_reference.append(result)
            steps.append(result)
            current.update({name: result['state'][name] for name, _, _ in FIELDS})
        sequences.append(dict(name=name, elapsed=416, flags=commands, steps=steps))
    if args.probe:
        payload = ''.join(' '.join(str(values[name]) for name in INPUT_FIELDS) + '\n' for values in sequence_inputs)
        run = subprocess.run([str(args.probe.resolve())], input=payload, text=True, capture_output=True, check=True)
        lines = [list(map(int, line.split())) for line in run.stdout.splitlines()]
        expected = [[result['state'][name] for name in OUTPUT_FIELDS] +
                    [step[name] for step in result['substeps'] for name in ('distance', 'strafe', 'x', 'z')]
                    for result in sequence_reference]
        assert len(lines) == len(expected)
        for index, (got, want) in enumerate(zip(lines, expected)):
            assert got == want, ('sequence', index, sequence_inputs[index], got, want)
        host['sequence_cases'] = len(expected)
    assert sources() == before
    report = dict(task='N09', status='passed', unicorn_version=unicorn.__version__,
                  address_system='Ghidra NE-loader segments; fixture CPU segments5000/6000/7000 are test relocations, not DPMI selectors',
                  input_fields=INPUT_FIELDS, output_fields=OUTPUT_FIELDS, languages=languages,
                  vectors=[dict(input=values, **result) for values, result in zip(vectors, reference)],
                  sequences=sequences, sequence_calls_per_language=len(sequence_inputs),
                  keyboard=keyboard_reference, host=host,
                  trig=dict(source=Original('ru').trig_pin, linear_address='0x1CA9B0', words=722,
                            initializer_executed=False, en_uses_ru_table=True),
                  boundaries=dict(clock='stop/read/start intercepted; uint32 elapsed injected inDX:AX',
                                  collision='accept original candidate x/z; no obstacle resolution',
                                  wrapper='actual original instructions including3substeps and controlled occupancy writes',
                                  fixture='64x64 empty tile0/class0 world, cell480; no door callbacks',
                                  commands='actual original routine; keyboard held/edge readers intercepted',
                                  dos_game_session=False), source=pin(Path(__file__)))
    output(args.output).write_text(json.dumps(report, indent=2) + '\n', encoding='utf8')
    print(f'N09 original x86 oracle passed: {len(vectors)} vectors/language, full wrapper,256 keyboard combinations')


if __name__ == '__main__':
    main()
