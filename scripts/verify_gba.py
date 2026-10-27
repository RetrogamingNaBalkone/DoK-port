"""Run the diagnostic cartridge in a shared mGBA Libretro core (Python stdlib only).

The callbacks implement a small headless Libretro frontend, not a GBA emulator.
All console instructions, IRQs, VRAM and keypad behavior execute inside mGBA.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct
import zlib


def scene_source_y(y,scene_objects):
    """Decode the established affine8-row scene strips, including quality trials.

    Uses the hardware fixed-point transform at each destination pixel, rather
    than assuming a uniform nearest-neighbor resize of the whole image.
    """
    assert scene_objects in (72,64,48,36),scene_objects
    rows=scene_objects//4;step=144//rows;row=y//step
    pd=2048//step;top=row*step+step//2-8
    if row:
        previous=((y-(top-step)-8)*pd+1024)>>8
        # Earlier OAM entries win when fixed-point rounding makes the preceding
        # strip extend one destination row into this strip (96-row trial).
        if 0<=previous<8:return (row-1)*8+previous
    source=((y-top-8)*pd+1024)>>8
    assert 0<=y<144 and 0<=source<8,(y,scene_objects,source)
    return row*8+source


class SystemInfo(ct.Structure):
    _fields_ = [('name', ct.c_char_p), ('version', ct.c_char_p), ('extensions', ct.c_char_p),
                ('need_fullpath', ct.c_bool), ('block_extract', ct.c_bool)]


class GameInfo(ct.Structure):
    _fields_ = [('path', ct.c_char_p), ('data', ct.c_void_p), ('size', ct.c_size_t), ('meta', ct.c_char_p)]


class Variable(ct.Structure):
    _fields_ = [('key', ct.c_char_p), ('value', ct.c_char_p)]


class MemoryDescriptor(ct.Structure):
    _fields_ = [('flags', ct.c_uint64), ('ptr', ct.c_void_p), ('offset', ct.c_size_t),
                ('start', ct.c_size_t), ('select', ct.c_size_t), ('disconnect', ct.c_size_t),
                ('length', ct.c_size_t), ('addrspace', ct.c_char_p)]


class MemoryMap(ct.Structure):
    _fields_ = [('descriptors', ct.POINTER(MemoryDescriptor)), ('count', ct.c_uint)]


ENV = ct.CFUNCTYPE(ct.c_bool, ct.c_uint, ct.c_void_p)
VIDEO = ct.CFUNCTYPE(None, ct.c_void_p, ct.c_uint, ct.c_uint, ct.c_size_t)
AUDIO = ct.CFUNCTYPE(None, ct.c_int16, ct.c_int16)
BATCH = ct.CFUNCTYPE(ct.c_size_t, ct.POINTER(ct.c_int16), ct.c_size_t)
POLL = ct.CFUNCTYPE(None)
INPUT = ct.CFUNCTYPE(ct.c_int16, ct.c_uint, ct.c_uint, ct.c_uint, ct.c_uint)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def png(path, width, height, rgb):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))
    rows = b''.join(b'\0' + rgb[y*width*3:(y+1)*width*3] for y in range(height))
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


class Frontend:
    def __init__(self, core_path, workdir):
        self.core = ct.CDLL(str(core_path))
        self.directory = os.fsencode(str(workdir.resolve()))
        self.defaults = {}
        self.maps = []
        self.buttons = set()
        self.pixel_format = 0
        self.rgb = b''
        self.deferred_video = None
        self.video_frames = 0
        self.callback_errors = []
        self.width = self.height = 0
        # Retain every callback for the full lifetime of the C core.
        self.callbacks = [ENV(self.environment), VIDEO(self.video), AUDIO(lambda l, r: None),
                          BATCH(lambda data, count: count), POLL(lambda: None), INPUT(self.input)]
        for name, callback in zip(('environment', 'video_refresh', 'audio_sample', 'audio_sample_batch',
                                   'input_poll', 'input_state'), self.callbacks):
            fn = getattr(self.core, 'retro_set_' + name)
            fn.argtypes = [type(callback)]
            fn(callback)
        self.core.retro_get_system_info.argtypes = [ct.POINTER(SystemInfo)]
        self.core.retro_load_game.argtypes = [ct.POINTER(GameInfo)]
        self.core.retro_load_game.restype = ct.c_bool
        self.core.retro_api_version.restype = ct.c_uint
        assert self.core.retro_api_version() == 1
        self.core.retro_init()
        info = SystemInfo()
        self.core.retro_get_system_info(ct.byref(info))
        self.name, self.version = info.name.decode(), info.version.decode()
        assert self.name == 'mGBA', f'Expected mGBA, got {self.name}'

    def environment(self, command, data):
        try:
            if command in (9, 31):  # system/save directory
                ct.cast(data, ct.POINTER(ct.c_char_p))[0] = self.directory
                return True
            if command == 10:  # SET_PIXEL_FORMAT
                self.pixel_format = ct.cast(data, ct.POINTER(ct.c_int))[0]
                return self.pixel_format in (0, 1, 2)
            if command == 15:  # GET_VARIABLE; defaults supplied by the core
                var = ct.cast(data, ct.POINTER(Variable))[0]
                value = self.defaults.get(var.key)
                if value is None:
                    return False
                var.value = value
                return True
            if command == 16:  # SET_VARIABLES
                variables = ct.cast(data, ct.POINTER(Variable))
                i = 0
                while variables[i].key:
                    var = variables[i]
                    self.defaults[var.key] = var.value.split(b'; ', 1)[1].split(b'|', 1)[0]
                    i += 1
                # Use mGBA's replacement BIOS, never a proprietary BIOS dump.
                self.defaults[b'mgba_use_bios'] = b'OFF'
                self.defaults[b'mgba_skip_bios'] = b'ON'
                self.defaults[b'mgba_frameskip'] = b'disabled'
                return True
            if command in (17, 3):  # variable update / can dupe
                ct.cast(data, ct.POINTER(ct.c_bool))[0] = command == 3
                return True
            if command in (39, 52):  # English; legacy core options API
                ct.cast(data, ct.POINTER(ct.c_uint))[0] = 0
                return True
            if command == (36 | 0x10000):  # SET_MEMORY_MAPS; copy ephemeral descriptors
                memory_map = ct.cast(data, ct.POINTER(MemoryMap))[0]
                self.maps = [MemoryDescriptor.from_buffer_copy(memory_map.descriptors[i])
                             for i in range(memory_map.count)]
                return True
            if command in (11, 18):  # input descriptors / support-no-game
                return True
            return False
        except Exception as error:
            self.callback_errors.append(str(error))
            return False

    def input(self, port, device, index, button):
        return int(port == 0 and device == 1 and button in self.buttons)

    def video(self, data, width, height, pitch):
        if not data:
            return
        try:
            bpp = 4 if self.pixel_format == 1 else 2
            raw = b''.join(ct.string_at(data + y*pitch, width*bpp) for y in range(height))
            self.last_video_raw=raw
            if os.environ.get('DOK_PROBE_FAST_VIDEO')=='1':
                # Console rasterization is unchanged. Defer only Python RGB
                # conversion until an actual screenshot needs those bytes.
                self.deferred_video=raw
            else:
                self.decode_video(raw)
            self.width, self.height = width, height
            self.video_frames += 1
        except Exception as error:
            self.callback_errors.append(str(error))

    def decode_video(self,raw):
        rgb=bytearray()
        if self.pixel_format==1:
            for (v,) in struct.iter_unpack('<I',raw):
                rgb.extend(((v>>16)&255,(v>>8)&255,v&255))
        else:
            green_bits=6 if self.pixel_format==2 else 5
            for (v,) in struct.iter_unpack('<H',raw):
                rgb.extend((((v>>(5+green_bits))&31)*255//31,
                            ((v>>5)&((1<<green_bits)-1))*255//((1<<green_bits)-1),
                            (v&31)*255//31))
        self.rgb=bytes(rgb)

    def colors555(self):
        """Normalize the actual PPU callback to native GBA palette colors."""
        raw=self.last_video_raw
        assert len(raw)==240*160*(4 if self.pixel_format==1 else 2)
        if self.pixel_format==1:
            return [((v>>19)&31)|(((v>>11)&31)<<5)|(((v>>3)&31)<<10)
                    for (v,) in struct.iter_unpack('<I',raw)]
        rs,gs=(11,6) if self.pixel_format==2 else (10,5)
        return [((v>>rs)&31)|(((v>>gs)&31)<<5)|((v&31)<<10)
                for (v,) in struct.iter_unpack('<H',raw)]

    def mode2_ui_indices(self):
        """Decode this backend's native normal-OBJ overlay, slots0..63.

        These indices are an attribution aid. A caller must still compare
        each visible source color with actual PPU video before using it.
        """
        layer=bytearray(38400);oam=self.read(0x07000000,512);vram=self.read(0x06000000,98304)
        for n in reversed(range(64)):
            a,b,c=struct.unpack_from('<3H',oam,n*8)
            if a&0x200:continue
            assert not(a&0x100) and a&0x2000 and not(c&0xc00),'Unexpected UI OBJ format'
            shape,size=a>>14,b>>14
            assert (shape,size) in ((0,0),(1,0),(1,1)),('Unexpected UI OBJ size',shape,size)
            width=8 if shape==0 else 16 if size==0 else 32
            left=b&511;top=a&255
            if left>=256:left-=512
            if top>=160:top-=256
            for dy in range(8):
                for dx in range(width):
                    x,y=left+dx,top+dy
                    if not(0<=x<240 and 0<=y<160):continue
                    texel=vram[0x10000+(c&1023)*32+(dx//8)*64+dy*8+(dx&7)]
                    if texel:layer[y*240+x]=texel
        return layer

    def load(self, rom):
        self.rom_buffer = ct.create_string_buffer(rom.read_bytes())
        self.game = GameInfo(os.fsencode(str(rom.resolve())), ct.cast(self.rom_buffer, ct.c_void_p), rom.stat().st_size, None)
        assert self.core.retro_load_game(ct.byref(self.game)), 'mGBA rejected cartridge'

    def run(self, frames, buttons=()):
        self.buttons = set(buttons)
        for _ in range(frames):
            self.core.retro_run()
        assert not self.callback_errors, self.callback_errors

    def pointer(self, address, size):
        for desc in self.maps:
            if desc.ptr and desc.start <= address and address + size <= desc.start + desc.length:
                return desc.ptr + desc.offset + address - desc.start
        raise AssertionError(f'Address not exposed by mGBA: {address:08x}')

    def read(self, address, size):
        return ct.string_at(self.pointer(address, size), size)

    def diag(self, address):
        names = ('magic', 'frames', 'vblanks', 'keys', 'seen', 'fault', 'data_ok', 'bss_ok')
        return dict(zip(names, struct.unpack('<8I', self.read(address, 32))))

    def screenshot(self, path):
        assert (self.width, self.height) == (240, 160)
        if self.deferred_video is not None:
            self.decode_video(self.deferred_video)
        png(path, self.width, self.height, self.rgb)

    def indicator(self, index):
        x, y = 11 + index % 5 * 44, 77 + index // 5 * 22
        offset = (y * self.width + x) * 3
        return self.rgb[offset:offset+3].hex()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--build-dir', type=Path, default=Path('build/gba'))
    parser.add_argument('--report', type=Path, default=Path('analysis/g01-verification.json'))
    parser.add_argument('--task', default='G01')
    args = parser.parse_args()
    build = args.build_dir
    metadata = json.loads((build / 'build-report.json').read_text())
    rom = build / 'dok-diagnostic.gba'
    assert sha(rom) == metadata['rom_sha256'], 'ROM changed after validation'
    frontend = Frontend(args.core.resolve(), build)
    frontend.load(rom)
    address = metadata['symbols']['g_diag']
    frontend.run(60)
    idle = frontend.diag(address)
    assert idle['magic'] == 0x444f4b31 and idle['fault'] == 0, idle
    assert idle['data_ok'] == idle['bss_ok'] == 1, idle
    if args.task == 'G02-memory':
        assert 'gba_runtime_selftest' in metadata['symbols'], 'ARM runtime selftest must be linked'
    frontend.screenshot(build / 'idle.png')
    registers_idle = {name: struct.unpack('<H', frontend.read(register, 2))[0]
                      for name, register in (('DISPCNT', 0x04000000), ('DISPSTAT', 0x04000004),
                                             ('IE', 0x04000200), ('WAITCNT', 0x04000204), ('IME', 0x04000208))}
    off_color = frontend.indicator(0)
    before_irq = struct.unpack('<I', frontend.read(metadata['symbols']['g_vblanks'], 4))[0]
    frontend.run(120)
    advance = frontend.diag(address)
    after_irq = struct.unpack('<I', frontend.read(metadata['symbols']['g_vblanks'], 4))[0]
    assert after_irq - before_irq == 120, (before_irq, after_irq)
    assert advance['frames'] - idle['frames'] >= 30, (idle, advance)
    checks = []
    # Standard Libretro joypad ids in GBA KEYINPUT bit order.
    for bit, (name, button) in enumerate(zip(('A','B','SELECT','START','RIGHT','LEFT','UP','DOWN','R','L'),
                                           (8,0,2,3,7,6,4,5,11,10))):
        frontend.run(8, [button])
        pressed = frontend.diag(address)
        color = frontend.indicator(bit)
        assert pressed['keys'] == 1 << bit, (name, pressed)
        assert color != off_color, (name, color, off_color)
        if bit == 0:
            frontend.screenshot(build / 'button-a.png')
        frontend.run(8)
        released = frontend.diag(address)
        assert released['keys'] == 0 and frontend.indicator(bit) == off_color, (name, released)
        checks.append(dict(button=name, gba_mask=1 << bit, pressed_keys=pressed['keys'],
                           indicator_pressed=color, indicator_released=off_color))
    assert frontend.diag(address)['seen'] == 0x3ff
    frontend.run(8, [8, 0, 11, 10])
    assert frontend.diag(address)['keys'] == 0x303
    frontend.run(8)
    frontend.run(8, [2, 3])  # diagnostic panic chord
    panic = frontend.diag(address)
    assert panic['fault'] == 0x10, panic
    frontend.screenshot(build / 'panic.png')
    frontend.run(8)
    assert frontend.diag(address) == panic, 'Panic should freeze the diagnostic record'
    frontend.core.retro_reset()
    # Poison allocated EWRAM after emulator reset: startup must copy .data and clear .bss.
    ct.memset(frontend.pointer(0x02000000, metadata['ewram_static_bytes']), 0xa5, metadata['ewram_static_bytes'])
    frontend.run(60)
    restart = frontend.diag(address)
    assert restart['fault'] == restart['seen'] == 0 and restart['data_ok'] == restart['bss_ok'] == 1, restart
    assert restart['frames'] > 0
    frontend.screenshot(build / 'restart.png')
    report = dict(task=args.task, emulator=frontend.name, emulator_version=frontend.version,
                  core_path=str(args.core), core_sha256=sha(args.core), rom_sha256=sha(rom),
                  rom_bytes=rom.stat().st_size, ewram_static_bytes=metadata['ewram_static_bytes'],
                  bios='mGBA replacement BIOS (no external BIOS)', pixel_format=frontend.pixel_format,
                  registers_idle=registers_idle,
                  arm_runtime_selftest='passed' if 'gba_runtime_selftest' in metadata['symbols'] else 'not linked',
                  idle=idle, after_120_frames=advance, irq_delta=after_irq-before_irq,
                  button_checks=checks, simultaneous_keys=0x303, panic=panic, restart_after_poison=restart,
                  video_callbacks=frontend.video_frames, physical_gba_tested=False,
                  screenshots={p.name: sha(p) for p in (build/'idle.png', build/'button-a.png', build/'panic.png', build/'restart.png')})
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    (build/'run-log.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    frontend.core.retro_unload_game()
    frontend.core.retro_deinit()
    print(f"mGBA {frontend.version}: 10 keys, indicators, IRQ cadence, panic and poisoned-RAM restart passed")
    print(f"120 emulated frames: {advance['frames'] - idle['frames']} diagnostic updates, {after_irq - before_irq} VBlank IRQs")
    print(f"Report: {args.report}; screenshots/log: {build}")


if __name__ == '__main__':
    main()
