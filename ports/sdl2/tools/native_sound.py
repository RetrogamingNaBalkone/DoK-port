"""Stateful execution of original Sound wrappers, admission, SB driver and IRQ.

Original algorithms execute in Unicorn; hooks model port readiness and DOS
interrupt-vector services only. No Sound play/stop/admission function is stubbed.
Ghidra segments are relocated independently for NE, not treated as DOS selectors.
"""
import hashlib
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INSN
from unicorn import x86_const as R

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp13-native-complete'
CONFIG = {
    'ru': dict(wrapper=0x1108, admission=0x10f0, hardware=0x1118,
               once=0x1f7, loop=0x232, stop=0x26d, stop_all=0x298, playing=0x2b9,
               data_start=0xb80, driver=0xbd6, active=0xbca, vtable=0xdd5,
               dma=0xd1c, dma_buffer=0xb512, dma_far=0xb51a, irq=0x8bf, delta=0),
    'en': dict(wrapper=0x1060, admission=0x1058, hardware=0x1070,
               once=0x173d, loop=0x1778, stop=0x17b3, stop_all=0x17de, playing=0x17ff,
               data_start=0xc20, driver=0xc8a, active=0xc7e, vtable=0xe7d,
               dma=0xdbc, dma_buffer=0xb5c2, dma_far=0xb5ca, irq=0x8d3, delta=0xa0),
    'old-gold': dict(wrapper=0x18a6, admission=0x1823, hardware=0x2ad5,
                     once=0x1f7, loop=0x22a, stop=0x25d, stop_all=0x280, playing=0x29d,
                     data_start=0, driver=0xac, active=0xa0, vtable=0xd91, irq=0x77),
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def far_linear(pointer):
    return (pointer >> 16) * 16 + (pointer & 65535)


class NativeSound:
    def __init__(self, profile):
        self.profile = profile
        self.cfg = cfg = CONFIG[profile]
        self.cpu = c = Uc(UC_ARCH_X86, UC_MODE_16)
        c.mem_map(0, 0x200000)
        self.globals = 0x32550 if profile == 'old-gold' else 0x30000
        self.redirect = {}
        self.loaded = []
        self.io = []
        self.platform = []
        self.instructions = 0
        self.sounds = {}
        self.pointers = {}
        self.vectors = {}
        self.dsp = []
        self.dac_bytes=[]
        self.packets=[]
        self.pending_dsp=None
        self.dma_words={2:0,3:0};self.dma_next={2:0,3:0};self.dma_page=0
        folder = OUT / 'ghidra' / profile
        manifest = json.loads((folder / 'manifest.json').read_text())
        for name, address, length in manifest['ranges']:
            raw = (folder / (name + '.bin')).read_bytes()
            assert sha(folder / (name + '.bin')) == manifest['bytes_sha256'][name]
            segment, offset = map(lambda s: int(s, 16), address.split(':'))
            if name == 'audio-data':
                c.mem_write(self.globals + offset, raw)
                continue
            if profile == 'old-gold':
                relocated = segment
            else:
                relocated = {'wrappers': 0x1000, 'admission': 0x1800, 'hardware': 0x2000}[name]
                self.redirect[segment] = relocated
            c.mem_write(relocated * 16 + offset, raw)
            self.loaded.append((relocated * 16 + offset, relocated * 16 + offset + len(raw)))
        c.hook_add(UC_HOOK_CODE, self.hook)
        c.hook_add(UC_HOOK_INSN, lambda *args: 0, None, 1, 0, R.UC_X86_INS_IN)
        c.hook_add(UC_HOOK_INSN, self.output, None, 1, 0, R.UC_X86_INS_OUT)
        self.put32(cfg['driver'], 0xc0000000)
        c.mem_write(0xc0000, struct.pack('<H', cfg['vtable']) + bytes(10))
        c.mem_write(0xc000a, b'\x01\0')
        self.put32(cfg['active'], 0)
        if profile == 'old-gold':
            self.put16(0xd5c, 0x220)
            self.put16(0xd5e, 0x22c)
            self.put16(0x9e, 0)
        else:
            delta = cfg['delta']
            for at, value in [(0xd1c, 0x220), (0xd1e, 7), (0xd20, 1),
                              (0xd2c, 0x22c), (0xd2e, 0x22c), (0xd30, 0x22e),
                              (0xd4a, 0xa), (0xd4c, 0xb), (0xd4e, 0xc),
                              (0xd50, 0x83), (0xd52, 2), (0xd54, 3), (0xd56, 1),
                              (0xd6e, 2048)]:
                self.put16(at + delta, value)
            # Real driver uses two conventional-memory DMA buffers. Fixtures
            # provide these allocations; the original copy/refill runs intact.
            self.put32(cfg['dma_buffer'], 0xd0000000)
            self.put32(cfg['dma_buffer'] + 4, 0xd1000000)
            self.put32(cfg['dma_far'], 0xd0000000)
            self.put32(cfg['dma_far'] + 4, 0xd1000000)

    def put16(self, offset, value):
        self.cpu.mem_write(self.globals + offset, struct.pack('<H', value & 65535))

    def put32(self, offset, value):
        self.cpu.mem_write(self.globals + offset, struct.pack('<I', value & 0xffffffff))

    def u16(self, offset):
        return struct.unpack('<H', self.cpu.mem_read(self.globals + offset, 2))[0]

    def u32(self, offset):
        return struct.unpack('<I', self.cpu.mem_read(self.globals + offset, 4))[0]

    def return_far(self, value=0):
        c = self.cpu
        sp = c.reg_read(R.UC_X86_REG_SP)
        ss = c.reg_read(R.UC_X86_REG_SS) * 16
        ip, cs = struct.unpack('<HH', c.mem_read(ss + sp, 4))
        c.reg_write(R.UC_X86_REG_AX, value & 65535)
        c.reg_write(R.UC_X86_REG_DX, (value >> 16) & 65535)
        c.reg_write(R.UC_X86_REG_SP, sp + 4)
        c.reg_write(R.UC_X86_REG_CS, cs)
        c.reg_write(R.UC_X86_REG_IP, ip)

    def hook(self, c, address, size, unused):
        self.instructions += 1
        if address == 0xefff0:
            c.emu_stop()
            return
        if self.profile!='old-gold' and c.reg_read(R.UC_X86_REG_DS) in (0x1190,0x10d0):c.reg_write(R.UC_X86_REG_DS,0x3000)
        cs = c.reg_read(R.UC_X86_REG_CS)
        if cs in self.redirect:
            ip = c.reg_read(R.UC_X86_REG_IP)
            c.reg_write(R.UC_X86_REG_CS, self.redirect[cs])
            c.reg_write(R.UC_X86_REG_IP, ip)
            return
        if any(begin <= address < end for begin, end in self.loaded):
            return
        # DOS/runtime interrupt-vector services; do not implement an audio
        # algorithm here. PIT/DSP programming remains original instructions.
        if self.profile == 'old-gold' and address in (0x11142, 0x11151):
            sp = c.reg_read(R.UC_X86_REG_SP)
            args = bytes(c.mem_read(0x30000 + sp + 4, 6))
            vector = struct.unpack_from('<H', args)[0]
            if address == 0x11142:
                result = self.vectors.get(vector, 0)
            else:
                result = 0
                self.vectors[vector] = struct.unpack_from('<I', args, 2)[0]
            self.platform.append(dict(service='DOS interrupt vector', address=hex(address)))
            self.return_far(result)
            return
        if self.profile == 'old-gold' and address == 0x2a846:
            # Game-clock PIT accounting callback, not DAC sample generation.
            self.platform.append(dict(service='game clock divisor notification', address=hex(address)))
            self.return_far()
            return
        raise RuntimeError(f'Unimplemented external native target {cs:04x}:{c.reg_read(R.UC_X86_REG_IP):04x}')

    def output(self, c, port, size, value, unused):
        self.io.append((port, size, value))
        if port == 0x22c:
            self.dsp.append(value)
            if self.pending_dsp is not None:
                command,args,remaining=self.pending_dsp;args.append(value);remaining-=1
                if remaining:self.pending_dsp=(command,args,remaining)
                else:
                    self.pending_dsp=None
                    if command==0x10:self.dac_bytes.append(args[0])
                    if command==0x14:
                        count=args[0]+args[1]*256+1;address=(self.dma_page<<16)|self.dma_words[2]
                        raw=bytes(c.mem_read(address,count));self.packets.append(dict(address=address,count=count,sha256=hashlib.sha256(raw).hexdigest(),data=raw))
            elif value in (0x40,0x10,0x14):self.pending_dsp=(value,[],2 if value==0x14 else 1)
        if port==0xc:self.dma_next={2:0,3:0}
        elif port in (2,3):
            shift=self.dma_next[port]*8;self.dma_words[port]=(self.dma_words[port]&~(255<<shift))|(value<<shift);self.dma_next[port]^=1
        elif port==0x83:self.dma_page=value

    def install(self, identity, samples, frequency=16000):
        assert 1 <= identity <= 128 and len(samples) <= 0x7b000
        pointer = 0xc0000000 + identity * 0x100
        linear = far_linear(pointer)
        # Each admitted sound is copied to a common contiguous buffer before
        # invocation, mirroring the input allocation contract, not play logic.
        header = bytearray(36)
        header[2:5] = b'SVM'
        header[7] = 9
        struct.pack_into('<III', header, 16, frequency, len(samples), 0x45000000)
        self.cpu.mem_write(linear, bytes(header))
        self.sounds[identity] = (bytes(samples), frequency)
        self.pointers[pointer] = identity
        return pointer

    def invoke(self, segment, offset, args=b'', irq=False):
        c = self.cpu
        segment = self.redirect.get(segment, segment)
        stack = struct.pack('<HH', 0xfff0, 0xe000) + (b'\x02\0' if irq else args)
        c.mem_write(0x3f000, stack)
        for register, value in [(R.UC_X86_REG_CS, segment), (R.UC_X86_REG_IP, offset),
                                (R.UC_X86_REG_DS, self.globals // 16),
                                (R.UC_X86_REG_SS, 0x3000), (R.UC_X86_REG_SP, 0xf000),
                                (R.UC_X86_REG_BP, 0), (R.UC_X86_REG_EFLAGS, 2)]:
            c.reg_write(register, value)
        c.emu_start(segment * 16 + offset, 0, count=1000000)
        assert c.reg_read(R.UC_X86_REG_CS) == 0xe000 and c.reg_read(R.UC_X86_REG_IP) == 0xfff0, 'Native sound did not return'
        assert c.reg_read(R.UC_X86_REG_SP)==0xf000+(6 if irq else 4),'Native audio stack imbalance'
        return c.reg_read(R.UC_X86_REG_AX)

    def command(self, action, identity=0):
        pointer = 0xc0000000 + identity * 0x100
        if action in ('once', 'loop'):
            samples, frequency = self.sounds[identity]
            if not self.active():self.cpu.mem_write(0x45000, samples)
        args = b'' if action == 'stop_all' else struct.pack('<I', pointer)
        result = self.invoke(self.cfg['wrapper'], self.cfg[action], args)
        return dict(result=result, active=self.active(), loop=self.loop())

    def irq(self):
        return self.invoke(0x2b41 if self.profile=='old-gold' else self.cfg['hardware'],self.cfg['irq'],irq=True)

    def render_source(self,identity=1):
        self.command('once',identity);guard=0
        while self.active():
            self.irq();guard+=1
            assert guard<=len(self.sounds[identity][0])+4,'Native sound IRQ failed to finish'
        return bytes(self.dac_bytes) if self.profile=='old-gold' else b''.join(p['data'] for p in self.packets)

    def active(self):
        pointer = self.u32(self.cfg['active'])
        if pointer and self.cpu.mem_read(far_linear(pointer) + 7, 1)[0] & 128:
            return self.pointers.get(pointer, -1)
        return 0

    def loop(self):
        if not self.active():
            return False
        if self.profile == 'old-gold':
            pointer = self.u32(self.cfg['active'])
            return bool(self.cpu.mem_read(far_linear(pointer) + 7, 1)[0] & 64)
        return bool(self.u16(0xd5a + self.cfg['delta']))


if __name__ == '__main__':
    for profile in CONFIG:
        sound = NativeSound(profile)
        sound.install(1, b'\x20\x40\x60\0')
        sound.install(2, b'\x30\x50\x70\0')
        for action, identity in [('once', 1), ('once', 2), ('loop', 1), ('stop', 2), ('stop', 1), ('loop', 2), ('stop_all', 0)]:
            print(profile, action, identity, sound.command(action, identity), 'outs', len(sound.io))
