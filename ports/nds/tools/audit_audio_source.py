"""N18 source-only SVM/catalog and isolated native x86 audio evidence.

Uses existing read-only Ghidra segment exports. No GBA runtime imports, DOS
playback claim, DSP/IRQ simulation, or native-pointer decoding outside fixtures.
"""
from __future__ import annotations
import hashlib
import json
import re
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/nds/n18/source'
SAMPLES = {
    'ru': ('originals/pkr/RUS/PKR.EXE', '12f78afc0eba05f76b5dc0bb99da38d7daa20585024c0ba958fe828aa3864d28'),
    'en': ('originals/pkr/ENG/PKE.EXE', 'cce7bc5993d0734ad87e93db37a70b8b52faedfff14ac48d6e317e5ac996a37b'),
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def file_sha(path):
    return sha((ROOT / path).read_bytes())

def catalog():
    result = {}
    for lang in SAMPLES:
        index = json.loads((ROOT / f'analysis/originals/sounds-{lang}.json').read_text(encoding='utf8'))
        archive_path = 'originals/pkr/RUS/PKR.RES' if lang == 'ru' else 'originals/pkr/ENG/PKE.RES'
        archive = (ROOT / archive_path).read_bytes()
        assert sha(archive) == index['archive_sha256']
        entries = []
        for entry in index['entries']:
            rid = entry['resource_index']
            # Independently verify display names against original RES CP866,
            # rather than inheriting potentially misdecoded JSON labels.
            original_name = archive[2 + rid*30:18 + rid*30].split(b'\0', 1)[0].decode('cp866')
            assert entry['name'] == original_name
            path = f'research/evidence/resources/{lang}/{rid:04}.bin'
            raw = (ROOT / path).read_bytes()
            assert sha(raw) == entry['source_sha256']
            assert raw[2:5] == b'SVM' and struct.unpack_from('<H', raw, 5)[0] == 1
            assert raw[7] == 9 and struct.unpack_from('<HH', raw, 8) == (8, 0)
            rate, count = struct.unpack_from('<II', raw, 16)
            assert len(raw) == count + 36
            assert (rate, count) == (entry['sample_rate'], entry['sample_count'])
            assert struct.unpack_from('<HH', raw, 12) == (255, 128)
            assert raw[24:36] == bytes(12)
            pcm = raw[36:]
            signed = bytes(x ^ 128 for x in pcm)
            s16 = b''.join(struct.pack('<h', (x - 128) * 256) for x in pcm)
            assert sha(pcm) == entry['pcm_u8_sha256']
            assert sha(signed) == entry['pcm_s8_sha256']
            assert sha(s16) == entry['pcm_s16le_sha256']
            wav = f'research/evidence/sounds/{lang}/{rid:04}/pcm8.wav'
            assert file_sha(wav) == entry['wav_sha256']
            entries.append(dict(resource_index=rid, name=original_name, name_verified_from_original_cp866=True, source=path,
                source_sha256=sha(raw), sample_rate=rate, sample_count=count,
                duration_seconds=count/rate, pcm_u8_sha256=sha(pcm), pcm_s8_sha256=sha(signed),
                wav=wav, wav_sha256=file_sha(wav)))
        assert len(entries) == 23 and sum(e['sample_count'] for e in entries) == 961526
        result[lang] = entries
    for ru, en in zip(result['ru'], result['en'], strict=True):
        assert ru['source_sha256'] == en['source_sha256']
    return result

class Native:
    def __init__(self):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
        from unicorn import x86_const as R
        self.R = R
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_16)
        self.cpu.mem_map(0, 0x100000)
        self.handlers = {}
        self.calls = []
        self.steps = 0
        self.cpu.hook_add(UC_HOOK_CODE, self.hook)

    def hook(self, c, addr, size, unused):
        self.steps += 1
        cs, ip = c.reg_read(self.R.UC_X86_REG_CS), c.reg_read(self.R.UC_X86_REG_IP)
        if (cs, ip) == (0x6000, 0xfff0):
            c.emu_stop()
        elif (cs, ip) in self.handlers:
            self.calls.append(f'{cs:04x}:{ip:04x}')
            self.handlers[cs, ip]()
        elif cs != 0x7000:
            raise AssertionError(f'Unintercepted native boundary {cs:04x}:{ip:04x}')

    def ret(self, result=0):
        c, R = self.cpu, self.R
        sp = c.reg_read(R.UC_X86_REG_SP)
        ip, cs = struct.unpack('<HH', c.mem_read(0x30000 + sp, 4))
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_AX, result & 65535)
        c.reg_write(R.UC_X86_REG_DX, result >> 16)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def call(self, ip, *args, stop=None):
        c, R = self.cpu, self.R
        for r, v in [(R.UC_X86_REG_CS, 0x7000), (R.UC_X86_REG_SS, 0x3000),
                     (R.UC_X86_REG_DS, 0x3000), (R.UC_X86_REG_SP, 0xf000),
                     (R.UC_X86_REG_BP, 0xee00), (R.UC_X86_REG_EFLAGS, 0x202)]:
            c.reg_write(r, v)
        c.mem_write(0x3f000, struct.pack('<HH', 0xfff0, 0x6000) + struct.pack('<' + 'I'*len(args), *args))
        c.emu_start(0x70000 + ip, 0xfffff, count=10000)
        assert c.reg_read(R.UC_X86_REG_CS) == 0x6000 or (stop is not None and
            c.reg_read(R.UC_X86_REG_CS) == 0x7000 and c.reg_read(R.UC_X86_REG_IP) == stop)
        return c.reg_read(R.UC_X86_REG_AX)

    def put(self, address, fmt, *values):
        self.cpu.mem_write(address, struct.pack(fmt, *values))

    def byte(self, address):
        return bytes(self.cpu.mem_read(address, 1))[0]

def native_policy(lang):
    n = Native()
    ru = lang == 'ru'
    folder = ROOT / f'research/evidence/audio/{lang}'
    segment = '10f0' if ru else '1058'
    n.cpu.mem_write(0x70000, (folder / f'{segment}.bin').read_bytes())
    play, stop, identity = (0x5e, 0xea, 0x125) if ru else (0x186a, 0x18f6, 0x1931)
    n.put(0x50000, '<H', 0x1000)
    n.put(0x31014, '<4H', 0, 0x6100, 4, 0x6100)
    hardware = []
    for offset in (0, 4):
        n.handlers[0x6100, offset] = lambda offset=offset: (hardware.append(offset), n.ret())
    a, b, driver = 0x90000000, 0x90000100, 0x50000000
    n.put(0x90014, '<II', 7, 0x90001000)
    n.put(0x90114, '<II', 7, 0x90002000)
    cases = []
    def check(name, predicate):
        assert predicate, name
        cases.append(name)
    check('idle start A', n.call(play, driver, a) == 1 and n.byte(0x90007) == 128 and hardware == [0])
    check('busy same identity rejected', n.call(play, driver, a) == 0 and hardware == [0])
    check('busy distinct identity rejected', n.call(play, driver, b) == 0 and hardware == [0])
    n.call(identity, driver, b)
    check('stop other identity ignored', n.byte(0x90007) == 128 and hardware == [0])
    n.call(identity, driver, a)
    check('stop active identity', n.byte(0x90007) == 0 and hardware == [0, 4])
    check('next identity admitted', n.call(play, driver, b) == 1 and n.byte(0x90107) == 128)
    n.call(stop, driver)
    check('global stop', n.byte(0x90107) == 0 and hardware == [0, 4, 0, 4])
    # Original wrapper flags change even when base admission rejects; result is
    # not a loop-policy oracle until the independent backend latch is checked.
    n.cpu.mem_write(0x70000, (folder / ('1108.bin' if ru else '1060.bin')).read_bytes())
    n.put(0x30000 + (0xbd6 if ru else 0xc8a), '<I', driver)
    n.handlers[int(segment, 16), play] = lambda: n.ret(0)
    for ip, before, after in [(0x1f7 if ru else 0x173d, 0xc9, 0x89),
                              (0x232 if ru else 0x1778, 0x89, 0xc9)]:
        n.put(0x90007, '<B', before)
        check(f'wrapper rejected flags {before:02x}->{after:02x}', n.call(ip, a) == 0 and n.byte(0x90007) == after)
    n.handlers.clear()
    n.cpu.mem_write(0x70000, (folder / ('1118.bin' if ru else '1070.bin')).read_bytes())
    begin, stop_ip, latch = (0x829, 0x86f, 0xd5a) if ru else (0x83d, 0x883, 0xdfa)
    def stop_prefix():
        n.cpu.emu_stop()
    n.handlers[0x7000, stop_ip] = stop_prefix
    for flags in (9, 0x49, 0x89, 0xc9):
        n.put(0x90007, '<B', flags)
        n.call(begin, driver, a, stop=stop_ip)
        value, = struct.unpack('<H', n.cpu.mem_read(0x30000 + latch, 2))
        check(f'backend independent loop latch {flags:02x}', value == bool(flags & 64))
    return dict(cases=cases, count=len(cases), hardware_calls=hardware, instructions=n.steps,
        limits='Hardware begin/stop intercepted; latch prefix stops before DMA fill. Isolated real-mode fixture load7000 is not the Ghidra selector or DOS DPMI layout.')

def native_triggers(lang):
    # Native pointer slots, not serialized LEVEL field locations. These slices
    # verify each real PUSH/CALLF/caller-cleanup sequence with unique identities;
    # they do not claim execution of entire AI/physics/weapon callbacks.
    calls = [
        ('HERO_DEAD',0x1972,0x1f26,'once',0xa6),
        ('SHOT weapon',0x1c3e,0x21f2,'once',0x4c),
        ('DAMAGE hero',0x1c7f,0x2233,'once',0xa6),
        ('PROJECTILE_REMOVED',0x2826,0x2ec9,'stop',0x4c),
        ('PROJECTILE_IMPACT_OR_MISS',0x2ad0,0x3173,'once',0x4c),
        ('PICKUP interrupt',0x2fb5,0x3658,'global_stop',0x4c),
        ('PICKUP',0x2fc5,0x3668,'once',0x4c),
        ('DOOR_OPEN_MOVING',0x32ca,0x397f,'loop',0x4c),
        ('DOOR_OPENED',0x330e,0x39c3,'stop',0x4c),
        ('DOOR_CLOSE_MOVING',0x33be,0x3a73,'loop',0x4c),
        ('DOOR_CLOSED',0x33e7,0x3a9c,'stop',0x4c),
        ('DOOR_LOCKED',0x351f,0x3bd4,'once',0x4c),
        ('AI_WAKE',0x377c,0x3ee5,'once',0x4c),
        ('AI_CONTACT',0x3908,0x4071,'once',0xf0),
        ('AI_RANGED',0x3b30,0x4299,'once',0xf0),
        ('AI_DYING interrupt',0x3da9,0x4512,'global_stop',0xf4),
        ('AI_DYING',0x3dba,0x4523,'once',0xf4),
        ('AI_DEAD',0x3ead,0x4616,'stop',0xf4),
    ]
    ru = lang == 'ru'
    original_segment = '1068' if ru else '1020'
    asm = (ROOT / f'research/evidence/flow/{lang}/evidence.asm').read_text(encoding='utf8')
    instructions = [line.split('\t',1) for line in asm.splitlines() if '\t' in line]
    result = []
    for name, ru_ip, en_ip, action, slot in calls:
        ip = ru_ip if ru else en_ip
        index = next(i for i, (addr, _) in enumerate(instructions) if addr == f'{original_segment}:{ip:04x}')
        start_addr, previous = instructions[index - 1]
        if name == 'SHOT weapon':
            start_addr = f'{original_segment}:{0x1c2e if ru else 0x21e2:04x}'
        else:
            assert previous == f'PUSH dword ptr ES:[BX + 0x{slot:x}]'
        match = re.fullmatch(r'CALLF 0x([a-f0-9]+):([a-f0-9]+)',instructions[index][1])
        assert match
        target = tuple(int(v,16) for v in match.groups())
        n = Native()
        n.cpu.mem_write(0x70000, (ROOT / f'research/evidence/flow/{lang}/{original_segment}.bin').read_bytes())
        unique_sound = 0x90001000 + slot
        n.put(0x90000 + slot, '<I', unique_sound)
        n.cpu.reg_write(n.R.UC_X86_REG_ES,0x9000)
        n.cpu.reg_write(n.R.UC_X86_REG_BX,0)
        observed = []
        def sound_call():
            sp = n.cpu.reg_read(n.R.UC_X86_REG_SP)
            observed.append(struct.unpack('<I',n.cpu.mem_read(0x30000+sp+4,4))[0])
            n.ret()
        n.handlers[target] = sound_call
        end = int(instructions[index+2][0].split(':')[1],16)
        assert instructions[index+1][1] == 'ADD SP,0x4'
        n.handlers[0x7000,end] = lambda: n.cpu.emu_stop()
        n.call(int(start_addr.split(':')[1],16), stop=end)
        assert observed == [unique_sound]
        assert n.cpu.reg_read(n.R.UC_X86_REG_SP) == 0xf000
        result.append(dict(event=name, call=f'{original_segment}:{ip:04x}', action=action,
            native_slot=f'0x{slot:x}', pointer_argument_exact=True, caller_cleanup_exact=True))
    return result

def native_pause(lang):
    n = Native()
    ru = lang == 'ru'
    folder = ROOT / f'research/evidence/ui/{lang}'
    n.cpu.mem_write(0x70000, (folder / ('10c0.bin' if ru else '1050.bin')).read_bytes())
    entry, measure, poll = (0x155d, 0x14ee, 0x14c7) if ru else (0x18c0, 0x1851, 0x182a)
    globals_screen, quit_flag = (0xb48c, 0x2152) if ru else (0xb53c, 0x2202)
    n.put(0x30000 + globals_screen, '<I', 0x90000000)
    n.put(0x90022, '<HH', 8, 8)
    n.put(0x90030, '<II', 320, 200)
    n.put(0x30000 + quit_flag, '<H', 0)
    def measurement():
        sp = n.cpu.reg_read(n.R.UC_X86_REG_SP)
        width, height = struct.unpack('<II', n.cpu.mem_read(0x30000 + sp + 8, 8))
        for far, value in ((width, 5), (height, 1)):
            n.put((far >> 16)*16 + (far & 65535), '<H', value)
        n.ret()
    polls = []
    def poll_input():
        polls.append(len(polls))
        n.put(0x90200, '<I', int(len(polls) == 8))
        n.ret(0x90000200)
    n.handlers[0x7000, measure] = measurement
    n.handlers[0x7000, poll] = poll_input
    external = [(0x1070, 0x55), (0x1058, 0x2ac), (0x1110, 0x2d6)] if ru else [(0x1028, 0x55), (0x1018, 0x1692), (0x1068, 0x2e2)]
    for address in external:
        n.handlers[address] = lambda: n.ret()
    # A separate active sound object survives the actual modal/wait instructions.
    n.put(0x90407, '<B', 0xc9)
    n.call(entry, 0x30000aad if ru else 0x30000b61, 0)
    assert len(polls) == 8 and n.byte(0x90407) == 0xc9
    assert n.cpu.reg_read(n.R.UC_X86_REG_EFLAGS) & 0x200
    return dict(entry=f'{"10c0" if ru else "1050"}:{entry:04x}', polls=len(polls), instructions=n.steps,
        external_calls=n.calls, active_sound_flags_unchanged=True, interrupt_flag_enabled_after=True,
        limits='Actual modal/wait code, controlled text measurement/input/display services. No DSP, IRQ timing, DOS audio capture, or auditory verification. Static absence of audio pause/stop and enabled IF support continuation; actual sample output is not measured.')

def main():
    before = {lang: file_sha(sample) for lang, (sample, expected) in SAMPLES.items()}
    assert all(before[lang] == expected for lang, (_, expected) in SAMPLES.items())
    report = dict(task='N18', status='passed', whole_DOS_session=False, script_sha256=file_sha(__file__),
        address_system='Existing Ghidra NE-loader exports; not file offsets/runtime DPMI selectors.',
        catalog=catalog(), native={lang: native_policy(lang) for lang in SAMPLES},
        pause={lang: native_pause(lang) for lang in SAMPLES},
        triggers={lang: native_triggers(lang) for lang in SAMPLES})
    report['exe_sha256_before'] = before
    report['exe_sha256_after'] = {lang: file_sha(sample) for lang, (sample, _) in SAMPLES.items()}
    assert report['exe_sha256_before'] == report['exe_sha256_after']
    inputs = ['tools/ghidra/InspectAudio.java', 'tools/ghidra/InspectUI.java', 'tools/ghidra/InspectFlow.java']
    for lang in SAMPLES:
        for group in ('audio', 'ui', 'flow'):
            folder = ROOT / f'research/evidence/{group}/{lang}'
            inputs += [str(p.relative_to(ROOT)).replace('\\', '/') for p in folder.iterdir() if p.suffix in ('.bin', '.asm', '.json')]
    report['input_pins'] = {p: file_sha(p) for p in inputs}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    compact = dict(task='N18-source-audio', status='passed', report='research/evidence/nds/n18/source/report.json',
        report_sha256=file_sha(OUT / 'report.json'), script='ports/nds/tools/audit_audio_source.py',
        catalog_count=23, samples_each_language=961526, all_ru_en_blocks_identical=True,
        native_cases_each_language=13, native_pause_polls_each_language=8,
        native_trigger_slices_each_language=18,
        exe_sha256_before=before, exe_sha256_after=report['exe_sha256_after'],
        source_only=True, whole_DOS_session=False, listening=False)
    (ROOT / 'analysis/nds-n18-audio-sources.json').write_text(json.dumps(compact, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
    print('N18 source PCM and native RU/EN policy/pause passed')

if __name__ == '__main__':
    main()
