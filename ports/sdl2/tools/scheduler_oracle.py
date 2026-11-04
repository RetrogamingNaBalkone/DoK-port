"""SP05 isolated original-instruction scheduler/PIT oracle, not a DOS session.

Scheduler and compiler copy instructions come from read-only Ghidra NE/MZ exports.
Harness adapted from the isolated original-code research in ports/nds/tools/audit_time.py;
no NDS/GBA engine module is imported. OG addressing and exports are independent.
Time reads and callback bodies are explicitly controlled test boundaries. Neither
the SDL2 scheduler nor any GBA engine code is imported or used as an oracle.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp05-scheduler'
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INSN
from unicorn import x86_const as R

SAMPLES = {
    'ru': ('originals/pkr/RUS/PKR.EXE', '12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'),
    'old-gold': ('originals/Old_Gold_3D/OG.EXE', '7965e1d67128f196fbcf2bc449d97197f355c6b81cf043608fc9e01103ea043f'),
    'en': ('originals/pkr/ENG/PKE.EXE', 'cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b'),
}
LAYOUT = {
    'ru': dict(scheduler='1038', clock='1130', init=0, destroy=0x5f, schedule=0x108,
               cancel=0x214, clear=0x234, interval=0x265, dispatch=0x284,
               globals=0x3e8, clock_read=0x26f, pit_read=0x15d, stopwatch_init=0x1f5,
               stopwatch_start=0x239, stopwatch_stop=0x2b6, irq_count=0xe70,
               free_segment=0x10a8, free_offset=0xb7),
    'en': dict(scheduler='1010', clock='1078', init=0x13aa, destroy=0x140f, schedule=0x14be,
               cancel=0x15ca, clear=0x15ea, interval=0x161b, dispatch=0x163a,
               globals=0x3f0, clock_read=0x1905, pit_read=0x17f3, stopwatch_init=0x188b,
               stopwatch_start=0x18cf, stopwatch_stop=0x194c, irq_count=0xf20,
               free_segment=0x1038, free_offset=0x902),
    'old-gold': dict(scheduler='2000', clock='2000', schedule=0xa9e2, cancel=0xaaf3, clear=0xab08, interval=0xab36, dispatch=0xab48, globals=0xd4e, clock_read=0xa7e7, free_segment=0x1000, free_offset=0x9a9f)
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def sources():
    result = {lang: sha((ROOT / path).read_bytes()) for lang, (path, _) in SAMPLES.items()}
    if any(result[lang] != expected for lang, (_, expected) in SAMPLES.items()):
        raise ValueError('Original executable hash mismatch')
    return result

def output(path):
    path = Path(path).absolute()
    if '..' in path.parts or not path.is_relative_to(OUT) or path == OUT:
        raise ValueError('N08 oracle outputs must remain in research/evidence/nds/n08')
    for ancestor in [path, *path.parents]:
        if ancestor == ROOT.parent:
            break
        if ancestor.is_symlink() or (ancestor.exists() and ancestor.is_junction()):
            raise ValueError('N08 output alias rejected')
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError('N08 output must be an unaliased regular file')
    if path.resolve() != path:
        raise ValueError('N08 output resolution differs')
    path.parent.mkdir(parents=True, exist_ok=True)
    return path

class Oracle:
    def __init__(self, language, actions=None):
        self.language, self.layout = language, LAYOUT[language]
        self.cpu = c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x100000)
        folder = OUT / language
        manifest = json.loads((folder/'manifest.json').read_text())
        self.binary_hashes = {}
        for name, address in [('scheduler',0x50000+(0xa8d0 if language=='old-gold' else 0)), ('copy',0x70000+(0x1126 if language=='old-gold' else 0x5cb3))]:
            raw=(folder/(name+'.bin')).read_bytes()
            assert sha(raw)==manifest['bytes_sha256'][name]
            c.mem_write(address,raw); self.binary_hashes[name]=sha(raw)
        self.now, self.phase, self.reads, self.callbacks = 0, 'setup', [], []
        self.actions, self.callback_occurrences = actions or {}, {}
        self.stopped = False
        self.free_calls, self.pit_bytes = [], []
        self.owner_ids = [1, 2, 3, 4]
        # Fixture allocations: caller-owned owners and four 16-byte native slots.
        for owner in self.owner_ids:
            c.mem_write(self.owner_address(owner), struct.pack('<HHHH', 0, 4, 0x2000 + owner * 0x100, 0x9000))
        c.hook_add(UC_HOOK_CODE, self.hook)
        c.hook_add(UC_HOOK_INSN, self.port_in, None, 1, 0, R.UC_X86_INS_IN)
        c.hook_add(UC_HOOK_INSN, self.port_out, None, 1, 0, R.UC_X86_INS_OUT)

    def owner_address(self, owner):
        return 0x90000 + 0x1000 + owner * 0x20

    def owner_pointer(self, owner):
        return 0x90000000 | (0x1000 + owner * 0x20)

    def read16(self, address):
        return struct.unpack('<H', self.cpu.mem_read(address, 2))[0]

    def write32(self, address, value):
        self.cpu.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def return_far(self, value=None):
        c = self.cpu
        sp = c.reg_read(R.UC_X86_REG_SP)
        ip, cs = struct.unpack('<HH', c.mem_read(0x30000 + sp, 4))
        if value is not None:
            c.reg_write(R.UC_X86_REG_AX, value & 65535)
            c.reg_write(R.UC_X86_REG_DX, (value >> 16) & 65535)
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def hook(self, c, address, size, unused):
        layout = self.layout
        if address == 0x5fff0:
            self.stopped = True
            c.emu_stop()
        elif address == int(layout['clock'], 16) * 16 + layout['clock_read']:
            self.reads.append(dict(phase=self.phase, value=self.now))
            self.return_far(self.now)
        elif address == (0x11126 if self.language=='old-gold' else 0x15cb3):
            # Execute the actual CLD/REP MOVSW/RETF8 compiler helper.
            c.reg_write(R.UC_X86_REG_CS, 0x7000)
            c.reg_write(R.UC_X86_REG_IP, 0x1126 if self.language=='old-gold' else 0x5cb3)
        elif address == layout['free_segment'] * 16 + layout['free_offset']:
            sp = c.reg_read(R.UC_X86_REG_SP)
            self.free_calls.append(struct.unpack('<I', c.mem_read(0x30000 + sp + 4, 4))[0])
            self.return_far()
        elif 0x80000 <= address < 0x81000 and address % 0x40 == 0:
            callback = (address - 0x80000) // 0x40
            sp = c.reg_read(R.UC_X86_REG_SP)
            pointer = struct.unpack('<I', c.mem_read(0x30000 + sp + 4, 4))[0]
            owner = ((pointer & 65535) - 0x1000) // 0x20
            assert pointer == self.owner_pointer(owner)
            self.callbacks.append(dict(owner=owner, callback=callback, clock=self.now))
            occurrence = self.callback_occurrences.get(callback, 0)
            self.callback_occurrences[callback] = occurrence + 1
            action = self.actions.get(str(callback), {})
            self.now = (self.now + action.get('advance', 0)) & 0xffffffff
            operations = action.get('operations', []) if not action.get('once') or occurrence == 0 else []
            raw = self.callback_code(owner, operations)
            # Redirect to a fresh callback-body address: rewriting a currently
            # translated RETF stub would otherwise execute Unicorn's cached TB.
            body_segment = 0x8200 + (len(self.callbacks) - 1) * 0x10
            c.mem_write(body_segment * 16, raw)
            c.reg_write(R.UC_X86_REG_CS, body_segment)
            c.reg_write(R.UC_X86_REG_IP, 0)

    def callback_code(self, owner, operations):
        raw = bytearray()
        for operation in operations:
            op = operation[0]
            if op == 'cancel':
                args = struct.pack('<I', self.owner_pointer(owner))
            elif op == 'interval':
                args = struct.pack('<II', self.owner_pointer(owner), operation[1] & 0xffffffff)
            elif op == 'schedule':
                target, callback, interval, parameter = operation[1:]
                args = self.schedule_args(target, callback, interval, parameter)
            elif op == 'destroy':
                args = struct.pack('<IH', self.owner_pointer(operation[1]), 0)
            else:
                raise ValueError(op)
            # Test callback body pushes native ABI arguments and calls original API.
            for i in range(len(args) - 2, -1, -2):
                raw += b'\x68' + args[i:i + 2]
            raw += b'\x9a' + struct.pack('<HH', self.layout[op], 0x5000)
            raw += b'\x83\xc4' + bytes([len(args)])
        return bytes(raw + b'\xcb')

    def port_in(self, cpu, port, size, unused):
        assert port == 0x40 and size == 1 and self.pit_bytes
        return self.pit_bytes.pop(0)

    def port_out(self, cpu, port, size, value, unused):
        assert port == 0x43 and size == 1 and value == 0

    def call(self, offset, arguments=b'', segment=0x5000):
        c = self.cpu
        for reg, value in [(R.UC_X86_REG_CS, segment), (R.UC_X86_REG_SS, 0x3000),
                           (R.UC_X86_REG_DS, 0x3000), (R.UC_X86_REG_ES, 0x9000),
                           (R.UC_X86_REG_SP, 0xf000), (R.UC_X86_REG_BP, 0xee00)]:
            c.reg_write(reg, value)
        c.mem_write(0x3f000, struct.pack('<HH', 0xfff0, 0x5000) + arguments)
        self.stopped = False
        c.emu_start(segment * 16 + offset, 0x5fff1, count=100000)
        if not self.stopped:
            raise ValueError('Native instruction execution failed to return')
        return (c.reg_read(R.UC_X86_REG_DX) << 16) | c.reg_read(R.UC_X86_REG_AX)

    def schedule_args(self, owner, callback, interval, parameter):
        self.cpu.mem_write(0x80000 + callback * 0x40, b'\xcb')
        return struct.pack('<IIiI', self.owner_pointer(owner), 0x80000000 | callback * 0x40,
                           parameter, interval & 0xffffffff)

    def operation(self, operation):
        self.phase = operation[0]
        op = operation[0]
        if op == 'now':
            self.now = operation[1] & 0xffffffff
            return
        if op == 'schedule':
            args = self.schedule_args(*operation[1:])
        elif op == 'dispatch':
            args = struct.pack('<I', operation[1])
        elif op == 'clear':
            args = b''
        elif op == 'destroy':
            args = struct.pack('<IH', self.owner_pointer(operation[1]), 0)
        else:
            raise ValueError(op)
        self.call(self.layout[op], args)

    def state(self):
        base = 0x30000 + self.layout['globals']
        owner_count, event_count, cursor = struct.unpack('<HHI', self.cpu.mem_read(base, 8))
        owners = []
        for owner in self.owner_ids:
            address = self.owner_address(owner)
            count, capacity, off, seg, nxt, previous = struct.unpack('<4HII', self.cpu.mem_read(address, 16))
            events = []
            for slot in range(count):
                deadline, interval, callback, parameter = struct.unpack('<4I', self.cpu.mem_read(seg * 16 + off + slot * 16, 16))
                events.append(dict(deadline=deadline, interval=interval,
                                   callback=(callback & 65535) // 0x40, parameter=parameter))
            owners.append(dict(id=owner, count=count, events=events))
        return dict(cursor=((cursor & 65535) - 0x1000) // 0x20 if cursor else 0,
                    owner_count=owner_count, event_count=event_count, owners=owners)

def cases():
    return [
        dict(name='capacity_three', operations=[['schedule',1,c,10,100+c] for c in range(1,5)]),
        dict(name='ring_equal', operations=[['schedule',o,o,10,100+o] for o in range(1,4)] + [['now',10],['dispatch',0]]),
        dict(name='post_callback_clock', actions={'1':dict(advance=7)}, operations=[['schedule',1,1,10,101],['now',10],['dispatch',0],['now',20],['dispatch',0],['now',27],['dispatch',0]]),
        dict(name='compaction_stale_deadline', actions={'1':dict(operations=[['cancel']])}, operations=[['schedule',1,1,10,101],['schedule',1,2,20,102],['now',30],['dispatch',0]]),
        dict(name='interval_first_slot', actions={'2':dict(operations=[['interval',77]])}, operations=[['schedule',1,1,10,101],['schedule',1,2,20,102],['now',30],['dispatch',0]]),
        dict(name='live_append', actions={'1':dict(once=True,operations=[['schedule',1,2,0,102]])}, operations=[['schedule',1,1,0,101],['dispatch',0]]),
        dict(name='shrinking_owner_bound', actions={'3':dict(operations=[['cancel']])}, operations=[['schedule',o,o,0,100+o] for o in range(1,4)] + [['dispatch',0]]),
        dict(name='unsigned_early_wrap', operations=[['now',0xfffffff0],['schedule',1,1,32,101],['dispatch',0]]),
        dict(name='unsigned_old_deadline_after_wrap', operations=[['now',0xffffffe0],['schedule',1,1,16,101],['now',5],['dispatch',0]]),
        dict(name='negative_interval_at_zero', operations=[['schedule',1,1,-1,101],['dispatch',0]]),
        dict(name='negative_interval_at_ten', operations=[['now',10],['schedule',1,1,-1,101],['dispatch',0]]),
        dict(name='insertion_not_deadline_order', operations=[['schedule',1,1,30,101],['schedule',1,2,10,102],['schedule',1,3,20,103],['now',40],['dispatch',0]]),
        dict(name='all_cancel_shrinking', actions={str(o):dict(operations=[['cancel']]) for o in range(1,4)}, operations=[['schedule',o,o,0,100+o] for o in range(1,4)] + [['dispatch',0],['dispatch',0]]),
        dict(name='clear_all', operations=[['schedule',o,o,0,100+o] for o in range(1,4)] + [['clear'],['dispatch',0]]),
        dict(name='not_due_then_equal', operations=[['schedule',1,1,10,101],['now',9],['dispatch',0],['now',10],['dispatch',0]]),
    ]

if __name__=='__main__':
    before=sources(); results={}
    for profile in SAMPLES:
        results[profile]=[]
        for case in cases():
            oracle=Oracle(profile,case.get('actions'))
            for op in case['operations']:oracle.operation(op)
            case.update(callbacks=oracle.callbacks,final=oracle.state())
            results[profile].append(case)
        print(profile,len(results[profile]),'original instruction cases',flush=True)
    assert sources()==before
    (OUT/'vectors.json').write_text(json.dumps(dict(samples=results,sha256=before),indent=2)+'\n')
