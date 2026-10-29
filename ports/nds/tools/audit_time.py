"""N08 isolated original-instruction scheduler/PIT oracle, not a DOS session.

Scheduler and compiler copy instructions come from read-only Ghidra NE exports.
Time reads and callback bodies are explicitly controlled test boundaries. Neither
the NDS scheduler nor any GBA engine code is imported or used as an oracle.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/nds/n08'
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INSN
from unicorn import x86_const as R

SAMPLES = {
    'ru': ('originals/pkr/RUS/PKR.EXE', '12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'),
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
        folder = OUT / 'original' / language
        self.binary_hashes = {}
        for name, address in [(self.layout['scheduler'], 0x50000),
                              (self.layout['clock'], 0x60000), ('1000', 0x70000)]:
            raw = (folder / (name + '.bin')).read_bytes()
            c.mem_write(address, raw)
            self.binary_hashes[name] = sha(raw)
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
        elif address == 0x15cb3:
            # Execute the actual CLD/REP MOVSW/RETF8 compiler helper.
            c.reg_write(R.UC_X86_REG_CS, 0x7000)
            c.reg_write(R.UC_X86_REG_IP, 0x5cb3)
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
        dict(name='destroy_other_active', actions={'3':dict(once=True,operations=[['destroy',1]])}, operations=[['schedule',o,o,0,100+o] for o in range(1,4)] + [['dispatch',0]]),
        dict(name='not_due_then_equal', operations=[['schedule',1,1,10,101],['now',9],['dispatch',0],['now',10],['dispatch',0]]),
    ]

def pit_vectors(language):
    result = []
    for irq, period, calibration, counter, base in [
        (0,65536,0,0,0),(0,65536,0,65535,0),(1,65536,0,65535,0),
        (123,65536,0,32768,17),(10000,65536,0,12345,100),
        (10001,65536,0,12345,100),(0,65536,42,65535,0),
        (0xffffffff,65536,0,100,0xffffff00),
    ]:
        oracle = Oracle(language)
        address = 0x30000 + oracle.layout['irq_count']
        oracle.cpu.mem_write(address, struct.pack('<IIIH',irq,base,period,calibration))
        oracle.pit_bytes = [counter & 255, counter >> 8]
        actual = oracle.call(oracle.layout['pit_read'],struct.pack('<I',0x90006000),0x6000)
        expected = (((irq*period)&0xffffffff)//119 + ((calibration-counter)&65535)//119 + base)&0xffffffff
        assert actual == expected
        irq_after, base_after = struct.unpack('<II',oracle.cpu.mem_read(address,8))
        assert irq_after == (0 if irq>10000 else irq)
        assert base_after == (actual if irq>10000 else base)
        assert oracle.cpu.reg_read(R.UC_X86_REG_EFLAGS)&0x200
        result.append(dict(irq=irq,period=period,calibration=calibration,counter=counter,base=base,
                           ticks=actual,irq_after=irq_after,base_after=base_after,interrupts_enabled_after=True))
    return result

def stopwatch_vectors(language):
    oracle = Oracle(language)
    pointer = struct.pack('<I', 0x90005000)
    result = []
    sequence = [('init',0),('start',100),('start',120),('read',135),('stop',170),
                ('read',200),('start',250),('read',260),('stop',270),
                ('start',0xfffffff0),('read',0x20)]
    expected = [(0,0,0),(1,100,0),(1,100,0),(1,100,35),(0,100,70),
                (0,100,70),(1,250,70),(1,250,10),(0,250,20),
                (1,0xfffffff0,20),(1,0xfffffff0,48)]
    for (operation, now), state in zip(sequence, expected):
        base = 0x30000 + oracle.layout['irq_count']
        oracle.cpu.mem_write(base, struct.pack('<IIIH',0,now,65536,0))
        oracle.pit_bytes = [0,0]
        function = oracle.layout['clock_read' if operation=='read' else 'stopwatch_'+operation]
        value = oracle.call(function,pointer,0x6000)
        actual = struct.unpack('<HII',oracle.cpu.mem_read(0x95000,10))
        assert actual == state, (language,operation,actual,state)
        result.append(dict(operation=operation,absolute_ticks=now,running=actual[0],
                           start=actual[1],elapsed=actual[2],return_value=value if operation=='read' else None,
                           pit_read=not oracle.pit_bytes))
    return result

def semantic_assertions(results):
    """Small hand-reviewed anchors detect broken callback/clock fixture plumbing."""
    by_name={case['name']:case for case in results}
    assert by_name['capacity_three']['final']['event_count']==3
    assert [row['owner'] for row in by_name['ring_equal']['callbacks']]==[3,1,2]
    assert [row['clock'] for row in by_name['post_callback_clock']['callbacks']]==[10,27]
    assert by_name['post_callback_clock']['final']['owners'][0]['events'][0]['deadline']==44
    assert by_name['compaction_stale_deadline']['final']['owners'][0]['events']==[
        dict(deadline=20,interval=20,callback=2,parameter=102)]
    assert by_name['interval_first_slot']['final']['owners'][0]['events'][0]['interval']==77
    assert by_name['interval_first_slot']['final']['owners'][0]['events'][1]['interval']==20
    assert [row['callback'] for row in by_name['live_append']['callbacks']]==[1,2]
    assert [row['owner'] for row in by_name['shrinking_owner_bound']['callbacks']]==[3,1]
    assert by_name['shrinking_owner_bound']['final']['cursor']==2
    assert len(by_name['unsigned_early_wrap']['callbacks'])==1
    assert not by_name['unsigned_old_deadline_after_wrap']['callbacks']
    assert not by_name['negative_interval_at_zero']['callbacks']
    assert len(by_name['negative_interval_at_ten']['callbacks'])==1
    assert [row['callback'] for row in by_name['insertion_not_deadline_order']['callbacks']]==[1,2,3]
    assert by_name['all_cancel_shrinking']['final']['event_count']==0
    assert by_name['clear_all']['final']['owner_count']==0
    assert [row['owner'] for row in by_name['destroy_other_active']['callbacks']]==[3,2]
    assert len(by_name['not_due_then_equal']['callbacks'])==1

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=OUT/'original-vectors.json')
    args = parser.parse_args()
    before = sources()
    report = dict(task='N08',evidence_kind='isolated original x86 instructions',
                  limitations=['Not a complete DOS/DPMI run; controlled clock samples and callback bodies.',
                               'No GBA or NDS scheduler serves as expected-value oracle.',
                               'Allocator-free boundary records frees without releasing fixture storage.',
                               'Unsafe current-owner destruction/empty-owner activation are ASM findings, not safe conformance vectors.'],
                  address_system='Ghidra NE-loader source addresses; execution segments are fixture relocations, not runtime DPMI selectors',
                  source_sha256_before=before,samples={})
    canonical = None
    for language in ('ru','en'):
        results = []
        for fixture in cases():
            oracle = Oracle(language,fixture.get('actions'))
            for operation in fixture['operations']:
                oracle.operation(operation)
            results.append(dict(**fixture,clock_reads=oracle.reads,callbacks=oracle.callbacks,final=oracle.state()))
        semantic_assertions(results)
        if canonical is None:
            canonical = results
        else:
            assert canonical == results, 'RU/EN native scheduler semantics differ'
        folder = OUT/'original'/language
        manifest = json.loads((folder/'analysis-manifest.json').read_text(encoding='utf-8-sig'))
        assert manifest['sha256_before']==manifest['sha256_after']==before[language]
        assert manifest['read_only'] and manifest['binary_sha256']==oracle.binary_hashes
        for key in ('script','driver'):
            assert sha((ROOT/manifest[key]).read_bytes())==manifest[key+'_sha256']
        report['samples'][language] = dict(export_manifest=manifest,binary_sha256=oracle.binary_hashes,
                                         asm_sha256=sha((ROOT/f'research/evidence/time/{language}/evidence.asm').read_bytes()),
                                         cases=results,pit_vectors=pit_vectors(language),stopwatch_vectors=stopwatch_vectors(language))
    assert report['samples']['ru']['pit_vectors']==report['samples']['en']['pit_vectors']
    assert report['samples']['ru']['stopwatch_vectors']==report['samples']['en']['stopwatch_vectors']
    report['source_sha256_after']=sources()
    report['status']='passed'
    report['counts']=dict(languages=2,scheduler_cases_per_language=len(canonical),pit_cases_per_language=8,
                          stopwatch_steps_per_language=11,
                          callbacks_per_language=sum(len(c['callbacks']) for c in canonical))
    output(args.output).write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print('N08 original x86 oracle passed:',report['counts'])

if __name__=='__main__':
    main()
