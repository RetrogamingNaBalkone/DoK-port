"""N02: execute the actual diagnostic ROM through the melonDS DS libretro core.

Standard library only. Inputs enter through libretro's controller callbacks;
the exported ARM9 observation symbol is read, never written. No GUI, Nintendo
BIOS/firmware dumps, savestates, game assets or system configuration are used.
Each storage scenario runs in a fresh process and owns its system/save folders.
"""

import argparse
import array
import ctypes as C
import ctypes.util
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import shutil
import statistics
import struct
import subprocess
import sys
import time
import traceback
import wave
import zlib

from emulator_disk_cleanup import register_disk_cleanup


ROOT = Path(__file__).resolve().parents[3]
MARKER = b"DOK_NDS_DIAG_V1\n"
SCENARIOS = ("happy", "missing-marker", "missing-media")
BUTTONS = (
    ("A", 8, 0), ("B", 0, 1), ("X", 9, 10), ("Y", 1, 11),
    ("L", 10, 9), ("R", 11, 8), ("Up", 4, 6), ("Down", 5, 7),
    ("Left", 6, 5), ("Right", 7, 4), ("Start", 3, 3), ("Select", 2, 2),
)
STATE_FIELDS = (
    "magic abi_version size_bytes sequence vblanks loops system_ticks_low "
    "system_ticks_high uptime_ms_low uptime_ms_high frame_us frame_min_us "
    "frame_max_us work_ticks work_us work_max_us missed_vblanks keys_held "
    "keys_down keys_up keys_seen_down keys_seen_up"
).split() + [f"button_down_{i}" for i in range(12)] + [
    f"button_up_{i}" for i in range(12)
] + (
    "touch_held touch_x touch_y touch_raw_x touch_raw_y touch_down_count "
    "touch_up_count storage_result storage_errno storage_mounted "
    "storage_bytes_read storage_read_fnv1a storage_read_us storage_attempts "
    "audio_trigger_count audio_active"
).split()
STATE_SIZE = len(STATE_FIELDS) * 4


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8", newline="\n")


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def fat_fixture(path, with_marker):
    """Deterministic 32 MiB disk, one MBR FAT16 partition and /DOK directory.

    This writes a regular image file, never a device. Two identical FATs,
    512-byte sectors, four sectors/cluster, short 8.3 names only. Files are
    deliberately synthetic diagnostics rather than original game payload.
    """
    sectors, start, fat_sectors, root_sectors = 65536, 2048, 64, 32
    volume_sectors = sectors - start
    data_start = start + 1 + 2 * fat_sectors + root_sectors
    path.parent.mkdir(parents=True, exist_ok=True)
    # A previous auto-created 4GiB card may leave a size index. The new
    # deterministic image must not inherit that unrelated geometry.
    index = path.with_name(path.name + ".idx")
    if index.is_file():
        index.unlink()
    with path.open("wb") as stream:
        stream.truncate(sectors * 512)
        mbr = bytearray(512)
        mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 6,
                                   b"\xfe\xff\xff", start, volume_sectors)
        mbr[510:512] = b"\x55\xaa"
        stream.write(mbr)
        boot = bytearray(512)
        boot[:11] = b"\xeb\x3c\x90DOKNDS02"
        struct.pack_into("<HBHBHHBHHHII", boot, 11, 512, 4, 1, 2, 512,
                         volume_sectors, 0xF8, fat_sectors, 63, 255, start, 0)
        boot[36:39] = b"\x80\x00\x29"
        struct.pack_into("<I", boot, 39, 0x324B4F44)
        boot[43:54] = b"DOK N02    "
        boot[54:62] = b"FAT16   "
        boot[510:512] = b"\x55\xaa"
        stream.seek(start * 512)
        stream.write(boot)
        fat = bytearray(fat_sectors * 512)
        struct.pack_into("<HHH", fat, 0, 0xFFF8, 0xFFFF, 0xFFFF)
        if with_marker:
            struct.pack_into("<H", fat, 6, 0xFFFF)
        for offset in (start + 1, start + 1 + fat_sectors):
            stream.seek(offset * 512)
            stream.write(fat)

        def entry(name, attributes, cluster, length=0):
            row = bytearray(32)
            row[:11] = name
            row[11] = attributes
            # 2026-10-28 00:00:00, fixed for reproducibility.
            struct.pack_into("<H", row, 24, ((2026 - 1980) << 9) | (10 << 5) | 28)
            struct.pack_into("<HI", row, 26, cluster, length)
            return row

        stream.seek((start + 1 + 2 * fat_sectors) * 512)
        stream.write(entry(b"DOK        ", 0x10, 2))
        directory = entry(b".          ", 0x10, 2) + entry(b"..         ", 0x10, 0)
        if with_marker:
            directory += entry(b"NDS-DIAGTXT", 0x20, 3, len(MARKER))
        stream.seek(data_start * 512)
        stream.write(directory)
        if with_marker:
            stream.seek((data_start + 4) * 512)
            stream.write(MARKER)
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": sha(path),
            "format": "MBR/FAT16", "marker": with_marker,
            "marker_path": "/dok/nds-diag.txt", "marker_sha256":
            hashlib.sha256(MARKER).hexdigest() if with_marker else None}


def fixture_load_delta(before, path):
    """Audit host core startup writes before the first retro_run."""
    after = path.read_bytes()
    changed = [i for i in range((max(len(before), len(after)) + 511) // 512)
               if before[i * 512:(i + 1) * 512] != after[i * 512:(i + 1) * 512]]
    ranges = []
    for sector in changed:
        if ranges and ranges[-1][1] == sector * 512:
            ranges[-1][1] += 512
        else:
            ranges.append([sector * 512, (sector + 1) * 512])
    root = (2048 + 1 + 128) * 512
    new_entries = []
    for offset in range(root, root + 32 * 512, 32):
        row = after[offset:offset + 32]
        if row == before[offset:offset + 32] or row[0] in (0, 0xE5) or row[11] == 0x0F:
            continue
        cluster, size = struct.unpack_from("<HI", row, 26)
        new_entries.append({"offset": offset, "short_name": row[:11].decode("ascii", "replace"),
                            "attributes": row[11], "cluster": cluster, "bytes": size})
    return {"before_bytes": len(before), "after_bytes": len(after),
            "changed_sectors": len(changed), "sector_aligned_byte_ranges_end_exclusive": ranges,
            "new_root_entries": new_entries,
            "interpretation": "Core startup injected a padded ROM file; before first retro_run/ARM9 test execution",
            "upstream_mechanism_reference": "https://github.com/melonDS-emu/melonDS/blob/master/src/NDSCart/CartHomebrew.cpp",
            "provenance_limit": "Upstream master explains observed binary delta; exact dependency commit of released core not independently verified"}


def flush_native_stdio():
    """Commit core host stdio buffers before observing the image from Python.

    Released Windows DLL imports fopen/fwrite/fflush from msvcrt.dll (checked
    against its PE import table). This touches only host FILE buffers, never
    emulated memory, input, CPU state or the SD driver's behavior.
    """
    library = "msvcrt.dll" if sys.platform == "win32" else ctypes.util.find_library("c")
    if not library:
        raise RuntimeError("Cannot locate the core's host C stdio runtime")
    runtime = C.CDLL(library)
    runtime.fflush.argtypes = [C.c_void_p]
    runtime.fflush.restype = C.c_int
    if runtime.fflush(None) != 0:
        raise RuntimeError("Host native stdio flush failed")
    return library


def elf_symbol(path, name):
    """Read ELF32 LE symbol table directly, avoiding a host binutils dependency."""
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError("Diagnostic ELF must be ELF32 little endian")
    shoff = struct.unpack_from("<I", data, 32)[0]
    shentsize, shnum = struct.unpack_from("<HH", data, 46)
    sections = [struct.unpack_from("<10I", data, shoff + i * shentsize)
                for i in range(shnum)]
    for section in sections:
        if section[1] != 2:
            continue
        strings = sections[section[6]]
        strtab = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], section[9] or 16):
            nameoff, value, size, _info, _other, _index = struct.unpack_from(
                "<IIIBBH", data, offset)
            end = strtab.find(b"\0", nameoff)
            if strtab[nameoff:end].decode("ascii", "replace") == name:
                return value, size
    raise ValueError(f"ELF symbol {name} not found")


def observation_symbol(args):
    if args.elf:
        return elf_symbol(args.elf, args.symbol)
    text = args.map.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^\s*(0x[0-9a-fA-F]+)\s+" + re.escape(args.symbol) + r"\s*$",
                      text, re.MULTILINE)
    if not match:
        raise ValueError(f"Map symbol {args.symbol} not found")
    return int(match[1], 16), STATE_SIZE


def png(path, width, height, rgb):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(
            ">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3]
                    for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(
        ">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class Variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]


class SystemInfo(C.Structure):
    _fields_ = [("library_name", C.c_char_p), ("library_version", C.c_char_p),
                ("valid_extensions", C.c_char_p), ("need_fullpath", C.c_bool),
                ("block_extract", C.c_bool)]


class Geometry(C.Structure):
    _fields_ = [("base_width", C.c_uint), ("base_height", C.c_uint),
                ("max_width", C.c_uint), ("max_height", C.c_uint),
                ("aspect_ratio", C.c_float)]


class Timing(C.Structure):
    _fields_ = [("fps", C.c_double), ("sample_rate", C.c_double)]


class AVInfo(C.Structure):
    _fields_ = [("geometry", Geometry), ("timing", Timing)]


ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIO_BATCH = C.CFUNCTYPE(C.c_size_t, C.POINTER(C.c_int16), C.c_size_t)
POLL = C.CFUNCTYPE(None)
INPUT = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
FRAME_TIME = C.CFUNCTYPE(None, C.c_int64)


class FrameTimeCallback(C.Structure):
    _fields_ = [("callback", FRAME_TIME), ("reference", C.c_int64)]


class Frontend:
    """A software-only libretro frontend with externally supplied input frames."""
    def __init__(self, core, directory, scenario):
        self.directory = directory
        self.system = directory / "system"
        self.save = directory / "save"
        self.initialized = False
        self.loaded = False
        self._disk_cleanup = register_disk_cleanup(self, ROOT, flush_native_stdio)
        self.system.mkdir(parents=True, exist_ok=True)
        self.save.mkdir(parents=True, exist_ok=True)
        self.variables = {}
        self.variable_defaults = {}
        self.variable_choices = {}
        self.env_calls = {}
        self.shutdown = False
        self.callback_error = None
        self.pixel_format = 0
        self.last_video = None
        self.video_calls = 0
        self.pcm = bytearray()
        self.joypad = 0
        self.touch = None
        self.polls = 0
        self.frames = 0
        self.frame_time = None
        self.states = []
        self.checks = []
        self.options = {
            "melonds_console_mode": "ds", "melonds_sysfile_mode": "builtin",
            "melonds_boot_mode": "direct", "melonds_render_mode": "software",
            "melonds_threaded_renderer": "disabled", "melonds_jit_enable": "disabled",
            "melonds_homebrew_sdcard": "disabled" if scenario == "missing-media" else "enabled",
            "melonds_homebrew_readonly": "enabled",
            "melonds_homebrew_sync_sdcard_to_host": "disabled",
            "melonds_screen_layout1": "top-bottom", "melonds_screen_gap": "0",
            "melonds_number_of_screen_layouts": "1", "melonds_touch_mode": "touch",
            "melonds_show_cursor": "disabled", "melonds_mic_input": "silence",
            "melonds_network_mode": "disabled", "melonds_start_time_mode": "absolute",
        }
        # Keep all callback functions and returned strings alive until retro_deinit.
        self.strings = {"system": str(self.system).encode(), "save": str(self.save).encode(),
                        "core": str(core).encode(), "user": b"N02 QA"}
        self.callbacks = [ENV(self.environment), VIDEO(self.video), AUDIO(self.audio),
                          AUDIO_BATCH(self.audio_batch), POLL(self.poll), INPUT(self.input)]
        self.core = C.CDLL(str(core))
        for name, callback in zip(("environment", "video_refresh", "audio_sample",
                                   "audio_sample_batch", "input_poll", "input_state"), self.callbacks):
            function = getattr(self.core, "retro_set_" + name)
            function.argtypes = [type(callback)]
            function.restype = None
            function(callback)
        self.core.retro_api_version.restype = C.c_uint
        self.core.retro_get_system_info.argtypes = [C.POINTER(SystemInfo)]
        self.core.retro_get_system_av_info.argtypes = [C.POINTER(AVInfo)]
        self.core.retro_load_game.argtypes = [C.POINTER(GameInfo)]
        self.core.retro_load_game.restype = C.c_bool
        self.core.retro_get_memory_data.argtypes = [C.c_uint]
        self.core.retro_get_memory_data.restype = C.c_void_p
        self.core.retro_get_memory_size.argtypes = [C.c_uint]
        self.core.retro_get_memory_size.restype = C.c_size_t
        self.core.retro_set_controller_port_device.argtypes = [C.c_uint, C.c_uint]
        for name in ("retro_init", "retro_deinit", "retro_unload_game", "retro_run"):
            getattr(self.core, name).restype = None
        self.info = SystemInfo()
        self.core.retro_get_system_info(C.byref(self.info))

    def environment(self, command, data):
        self.env_calls[str(command)] = self.env_calls.get(str(command), 0) + 1
        try:
            cmd = command & 0xFFFF
            if cmd in (9, 19, 30, 31, 38):
                key = {9: "system", 19: "core", 30: "system", 31: "save", 38: "user"}[cmd]
                C.cast(data, C.POINTER(C.c_char_p))[0] = self.strings[key]
                return True
            if cmd == 15:  # GET_VARIABLE
                variable = C.cast(data, C.POINTER(Variable)).contents
                key = variable.key.decode()
                if key not in self.variables:
                    value = self.options.get(key, self.variable_defaults.get(key))
                    if value is None:
                        return False
                    if key in self.variable_choices and value not in self.variable_choices[key]:
                        raise ValueError(f"Unsupported core option {key}={value}")
                    self.variables[key] = value.encode()
                variable.value = self.variables[key]
                return True
            if cmd == 16:  # SET_VARIABLES, version-0 options with default first
                rows = C.cast(data, C.POINTER(Variable))
                index = 0
                while rows[index].key:
                    key = rows[index].key.decode()
                    choices = rows[index].value.decode().split("; ", 1)[-1].split("|")
                    self.variable_defaults[key] = choices[0]
                    self.variable_choices[key] = choices
                    index += 1
                return True
            if cmd == 10:  # SET_PIXEL_FORMAT
                self.pixel_format = C.cast(data, C.POINTER(C.c_int))[0]
                return self.pixel_format in (0, 1, 2)
            if cmd == 51:  # GET_INPUT_BITMASKS has no data pointer
                return True
            if cmd in (3, 17, 49, 74):
                C.cast(data, C.POINTER(C.c_bool))[0] = cmd == 3
                return True
            if cmd in (39, 52, 59, 61):
                C.cast(data, C.POINTER(C.c_uint))[0] = 1 if cmd == 61 else 0
                return True
            if cmd == 24:
                C.cast(data, C.POINTER(C.c_uint64))[0] = (1 << 1) | (1 << 5) | (1 << 6)
                return True
            if cmd == 47:
                C.cast(data, C.POINTER(C.c_int))[0] = 3
                return True
            if cmd == 50:
                C.cast(data, C.POINTER(C.c_float))[0] = 59.8261
                return True
            if cmd == 2:
                C.cast(data, C.POINTER(C.c_bool))[0] = False
                return True
            if cmd == 7:
                self.shutdown = True
                return True
            if cmd == 21:
                self.frame_time = FrameTimeCallback.from_buffer_copy(
                    C.string_at(data, C.sizeof(FrameTimeCallback)))
                return True
            # Hints that require no frontend action. HW/VFS/mic/log/perf
            # interfaces return false so the core uses its normal fallbacks.
            if cmd in (1, 6, 11, 18, 32, 34, 35, 36, 37, 42, 44, 55, 60, 63, 65, 69):
                return True
            return False
        except Exception:
            self.callback_error = traceback.format_exc()
            return False

    def video(self, data, width, height, pitch):
        try:
            if not data:
                return  # libretro duplicate frame
            if data == C.c_void_p(-1).value:
                raise RuntimeError("Unexpected hardware framebuffer in software-only test")
            self.last_video = (width, height, pitch, self.pixel_format,
                               C.string_at(data, height * pitch))
            self.video_calls += 1
        except Exception:
            self.callback_error = traceback.format_exc()

    def audio(self, left, right):
        self.pcm.extend(struct.pack("<hh", left, right))

    def audio_batch(self, data, frames):
        self.pcm.extend(C.string_at(data, frames * 4))
        return frames

    def poll(self):
        self.polls += 1

    def input(self, port, device, index, identifier):
        if port or index:
            return 0
        if device == 1:
            return self.joypad if identifier == 256 else int(bool(self.joypad & (1 << identifier)))
        if device == 6 and self.touch:
            x, y = self.touch
            if identifier == 2:
                return 1
            # Pointer coordinates refer to the complete 256x384 top/bottom
            # viewport; the core transforms the lower half to touch pixels.
            if identifier == 0:
                return round((x + 0.5) * 65534 / 256 - 32767)
            if identifier == 1:
                return round((y + 192.5) * 65534 / 384 - 32767)
        return 0

    def check(self, name, passed, detail=None):
        row = {"name": name, "passed": bool(passed)}
        if detail is not None:
            row["detail"] = detail
        self.checks.append(row)
        if not passed:
            raise AssertionError(f"{name}: {detail}")

    def load(self, rom):
        self.check("libretro API v1", self.core.retro_api_version() == 1)
        self.core.retro_init()
        self.initialized = True
        self.check("frontend initialization callbacks", self.callback_error is None,
                   self.callback_error)
        content = rom.read_bytes()
        self.rom_buffer = C.create_string_buffer(content)
        self.game_info = GameInfo(str(rom).encode(), C.cast(self.rom_buffer, C.c_void_p), len(content), None)
        self.loaded = bool(self.core.retro_load_game(C.byref(self.game_info)))
        self.check("frontend loading callbacks", self.callback_error is None,
                   self.callback_error)
        self.check("ROM loaded by emulator", self.loaded)
        self.core.retro_set_controller_port_device(0, 1)
        self.av = AVInfo()
        self.core.retro_get_system_av_info(C.byref(self.av))

    def state(self, address):
        pointer = self.core.retro_get_memory_data(2)  # RETRO_MEMORY_SYSTEM_RAM
        size = self.core.retro_get_memory_size(2)
        offset = address - 0x02000000
        if not pointer or not 0 <= offset <= size - STATE_SIZE:
            raise RuntimeError(f"Observation 0x{address:08x} outside main RAM ({size} bytes)")
        before_sequence = C.c_uint32.from_address(pointer + offset + 12).value
        raw = C.string_at(pointer + offset, STATE_SIZE)
        after_sequence = C.c_uint32.from_address(pointer + offset + 12).value
        state = dict(zip(STATE_FIELDS, struct.unpack("<" + "I" * len(STATE_FIELDS), raw)))
        if before_sequence != after_sequence or state["sequence"] != after_sequence:
            state["sequence"] |= 1  # discard incoherent read, no emulated write
        state["system_ticks"] = state["system_ticks_low"] | (state["system_ticks_high"] << 32)
        state["uptime_ms"] = state["uptime_ms_low"] | (state["uptime_ms_high"] << 32)
        return state

    def step(self, count, address):
        observations = []
        for _ in range(count):
            before = time.perf_counter_ns()
            if self.frame_time and self.frame_time.callback:
                self.frame_time.callback(round(1000000 / self.av.timing.fps))
            self.core.retro_run()
            self.frames += 1
            if self.callback_error:
                raise RuntimeError(self.callback_error)
            if self.shutdown:
                raise RuntimeError("Core requested shutdown")
            state = self.state(address)
            state["frontend_frame"] = self.frames
            state["host_run_ns"] = time.perf_counter_ns() - before
            if state["magic"] == 0x324B4F44 and state["loops"]:
                if state["sequence"] & 1:
                    # retro_run may stop inside publish_observation. Skip a
                    # torn snapshot; never alter PC, RAM or ROM to obtain it.
                    continue
                if self.states:
                    previous = self.states[-1]
                    if state["system_ticks"] < previous["system_ticks"]:
                        raise AssertionError("Hardware system counter went backwards")
                    if state["vblanks"] < previous["vblanks"]:
                        raise AssertionError("VBlank counter went backwards")
                self.states.append(state)
                observations.append(state)
        return observations

    def close(self):
        if self.loaded:
            self.core.retro_unload_game()
            self.loaded = False
        if self.initialized:
            self.core.retro_deinit()
            self.initialized = False

    def rgb(self):
        width, height, pitch, pixel_format, raw = self.last_video
        rgb = bytearray(width * height * 3)
        for y in range(height):
            for x in range(width):
                if pixel_format == 1:  # XRGB8888, little-endian B G R X
                    b, g, r = raw[y * pitch + x * 4:y * pitch + x * 4 + 3]
                else:
                    value = struct.unpack_from("<H", raw, y * pitch + x * 2)[0]
                    if pixel_format == 2:  # RGB565
                        r, g, b = (value >> 11) & 31, (value >> 5) & 63, value & 31
                        r, g, b = r * 255 // 31, g * 255 // 63, b * 255 // 31
                    else:
                        r, g, b = (value >> 10) & 31, (value >> 5) & 31, value & 31
                        r, g, b = r * 255 // 31, g * 255 // 31, b * 255 // 31
                offset = (y * width + x) * 3
                rgb[offset:offset + 3] = bytes((r, g, b))
        return width, height, bytes(rgb)

    def capture(self, stem):
        if not self.last_video:
            return []
        width, height, rgb = self.rgb()
        full = self.directory / (stem + ".png")
        png(full, width, height, rgb)
        paths = [full]
        if width == 256 and height == 384:
            for name, start in (("top", 0), ("bottom", 192)):
                path = self.directory / f"{stem}-{name}.png"
                png(path, 256, 192, rgb[start * 256 * 3:(start + 192) * 256 * 3])
                paths.append(path)
        return [{"path": str(path), "sha256": sha(path)} for path in paths]


def test_runtime(frontend, address, scenario, boot_frames):
    f = frontend
    for _ in range(boot_frames):
        f.step(1, address)
        if f.states and f.states[-1]["loops"] >= 10:
            break
    f.check("diagnostic booted and publishes coherent state", bool(f.states))
    state = f.states[-1]
    f.check("observation ABI", state["abi_version"] == 1 and state["size_bytes"] == STATE_SIZE,
            {"version": state["abi_version"], "size": state["size_bytes"]})
    expected_result = {"happy": 6, "missing-marker": 2, "missing-media": 1}[scenario]
    f.check("FAT/DLDI storage result", state["storage_result"] == expected_result, state)
    f.check("storage mount state", state["storage_mounted"] == (scenario != "missing-media"))
    if scenario == "happy":
        f.check("exact file contents read by ARM9", state["storage_bytes_read"] == len(MARKER)
                and state["storage_read_fnv1a"] == fnv1a(MARKER))
    else:
        f.check("missing file/media reads no bytes", state["storage_bytes_read"] == 0)
    f.check("real video callback", f.last_video is not None)
    width, height, rgb = f.rgb()
    f.check("both screens have native dimensions", (width, height) == (256, 384), [width, height])
    top, bottom = rgb[:256 * 192 * 3], rgb[256 * 192 * 3:]
    f.check("both screens contain distinct visible output", len(set(top)) > 1 and
            len(set(bottom)) > 1 and top != bottom)
    images = f.capture("boot")

    if scenario == "happy":
        button_results = []
        for i, (name, retro_id, nds_bit) in enumerate(BUTTONS):
            f.joypad = 0
            f.step(4, address)
            before = f.states[-1]
            f.joypad = 1 << retro_id
            pressed = f.step(8, address)
            f.check(f"{name} pressed", bool(pressed) and
                    any(s["keys_held"] & (1 << nds_bit) for s in pressed) and
                    any(s["keys_down"] & (1 << nds_bit) for s in pressed) and
                    pressed[-1][f"button_down_{i}"] == before[f"button_down_{i}"] + 1)
            held = f.step(8, address)
            f.check(f"{name} held without repeated edge", bool(held) and
                    all(s["keys_held"] & (1 << nds_bit) for s in held) and
                    all(s[f"button_down_{i}"] == pressed[-1][f"button_down_{i}"] for s in held))
            f.joypad = 0
            released = f.step(8, address)
            f.check(f"{name} released", bool(released) and
                    any(s["keys_up"] & (1 << nds_bit) for s in released) and
                    released[-1]["keys_held"] & (1 << nds_bit) == 0 and
                    released[-1][f"button_up_{i}"] == before[f"button_up_{i}"] + 1)
            button_results.append({"button": name, "retro_id": retro_id, "nds_bit": nds_bit,
                                   "down": released[-1][f"button_down_{i}"],
                                   "up": released[-1][f"button_up_{i}"]})
        f.check("all twelve buttons observed down/up",
                f.states[-1]["keys_seen_down"] & 0xFFF == 0xFFF and
                f.states[-1]["keys_seen_up"] & 0xFFF == 0xFFF)
        before = f.states[-1]
        for index, (x, y) in enumerate(((32, 24), (128, 96), (224, 168))):
            f.touch = (x, y)
            held = f.step(12, address)
            actual = held[-1]
            f.check(f"touch coordinate {index}", actual["touch_held"] == 1 and
                    abs(actual["touch_x"] - x) <= 2 and abs(actual["touch_y"] - y) <= 2,
                    {"expected": [x, y], "actual": [actual["touch_x"], actual["touch_y"]]})
            f.check(f"touch held edge count {index}", actual["touch_down_count"] == before["touch_down_count"] + 1)
        f.touch = None
        released = f.step(12, address)[-1]
        f.check("touch released", released["touch_held"] == 0 and
                released["touch_up_count"] == before["touch_up_count"] + 1)
        images += f.capture("input-complete")
        # Capture an isolated PSG tone after quiet frames. Read actual emulator
        # output PCM rather than trusting only soundPlayPSG's return value.
        f.step(30, address)
        quiet_start = len(f.pcm)
        f.step(16, address)
        quiet_pcm = bytes(f.pcm[quiet_start:])
        audio_before = f.states[-1]["audio_trigger_count"]
        tone_start = len(f.pcm)
        f.joypad = 1 << 8  # libretro A
        f.step(5, address)
        f.joypad = 0
        f.step(25, address)
        tone_pcm = bytes(f.pcm[tone_start:])
        f.check("PSG triggered through A input", f.states[-1]["audio_trigger_count"] == audio_before + 1)
        audio = audio_metrics(tone_pcm, f.av.timing.sample_rate)
        quiet = audio_metrics(quiet_pcm, f.av.timing.sample_rate)
        f.check("emulated sound captured", audio["rms"] > max(16, quiet["rms"] * 4),
                {"tone": audio, "quiet": quiet})
        f.check("diagnostic tone near 440 Hz", 400 <= audio["positive_crossing_hz_active"] <= 480, audio)
        f.check("tone timeout stop requested", f.states[-1]["audio_active"] == 0)
        tail_bytes = round(f.av.timing.sample_rate * 0.1) * 4
        tail = audio_metrics(tone_pcm[-tail_bytes:], f.av.timing.sample_rate)
        f.check("actual PCM silent after tone timeout", tail["rms"] <= max(8, quiet["rms"] * 2),
                {"tail": tail, "quiet": quiet})
        save_wav(f.directory / "tone.wav", tone_pcm, f.av.timing.sample_rate)
        save_wav(f.directory / "quiet.wav", quiet_pcm, f.av.timing.sample_rate)
    else:
        button_results, audio, quiet = [], None, None

    interval_states = f.step(120, address)
    f.check("runtime progresses after storage/input checks", len(interval_states) > 100 and
            interval_states[-1]["loops"] > interval_states[0]["loops"] + 100)
    f.check("system counter and uptime advance", interval_states[-1]["system_ticks"] >
            interval_states[0]["system_ticks"] and interval_states[-1]["uptime_ms"] >
            interval_states[0]["uptime_ms"])
    intervals = [s["frame_us"] for s in interval_states]
    f.check("VBlank frame intervals measured", min(intervals) > 15000 and max(intervals) < 19000,
            {"min_us": min(intervals), "median_us": statistics.median(intervals), "max_us": max(intervals)})
    f.check("CPU work timer measured", any(s["work_ticks"] > 0 for s in interval_states) and
            all(0 < s["work_us"] < s["frame_us"] for s in interval_states))
    elapsed_ticks = interval_states[-1]["system_ticks"] - interval_states[0]["system_ticks"]
    elapsed_vblanks = interval_states[-1]["vblanks"] - interval_states[0]["vblanks"]
    tick_rate = elapsed_ticks / elapsed_vblanks * f.av.timing.fps
    f.check("counter rate matches BUS_CLOCK/64", abs(tick_rate - 33513982 / 64) < 1000,
            {"observed_ticks_per_second": tick_rate, "expected": 33513982 / 64})
    images += f.capture("final")
    save_wav(f.directory / "session.wav", f.pcm, f.av.timing.sample_rate)
    return {"buttons": button_results, "tone": audio, "quiet": quiet,
            "images": images, "frame_interval_us": {"min": min(intervals),
            "median": statistics.median(intervals), "max": max(intervals)},
            "cpu_work_us": {"min": min(s["work_us"] for s in interval_states),
            "median": statistics.median(s["work_us"] for s in interval_states),
            "max": max(s["work_us"] for s in interval_states)},
            "final_state": f.states[-1]}


def save_wav(path, pcm, sample_rate):
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(round(sample_rate))
        stream.writeframes(pcm)


def audio_metrics(pcm, sample_rate):
    samples = array.array("h")
    samples.frombytes(pcm)
    if sys.byteorder != "little":
        samples.byteswap()
    mono = [(samples[i] + samples[i + 1]) / 2 for i in range(0, len(samples), 2)]
    peak = max((abs(x) for x in mono), default=0)
    rms = math.sqrt(sum(x * x for x in mono) / max(1, len(mono)))
    active = [i for i, value in enumerate(mono) if abs(value) > peak * 0.1] if peak else []
    if len(active) > 1:
        start, end = active[0], active[-1]
        crossings = sum(mono[i - 1] <= 0 < mono[i] for i in range(start + 1, end + 1))
        frequency = crossings * sample_rate / (end - start)
    else:
        frequency = 0
    return {"sample_frames": len(mono), "sample_rate": sample_rate, "peak": peak,
            "rms": rms, "positive_crossing_hz_active": frequency}


def worker(args):
    directory = args.out / args.scenario
    directory.mkdir(parents=True, exist_ok=True)
    address, symbol_size = observation_symbol(args)
    report = {"schema": 1, "task": "N02", "scenario": args.scenario,
              "passed": False, "python": platform.python_version(),
              "rom": {"path": str(args.rom), "sha256": sha(args.rom)},
              "core": {"path": str(args.core), "sha256": sha(args.core)},
              "symbols_file": {"path": str(args.elf or args.map),
                               "sha256": sha(args.elf or args.map)},
              "observation_symbol": {"name": args.symbol, "address": address,
                                      "size": symbol_size, "access": "read-only ARM9 main RAM"},
              "limitations": ["Emulator functional evidence, not physical DS/DS Lite timing or flashcart compatibility.",
                              "Built-in replacement BIOS/firmware; no Nintendo dumps.",
                              "Inputs injected via standard libretro device callbacks, not physical buttons/stylus.",
                              "FAT16 image emulates storage; no physical microSD used.",
                              "Core direct boot injects a ROM copy into FAT before CPU execution; image immutability is checked from after emulator load.",
                              "No gameplay, original assets, renderer or release performance is tested."]}
    frontend = None
    try:
        if symbol_size != STATE_SIZE:
            raise ValueError(f"Unexpected diagnostic symbol size {symbol_size}; expected {STATE_SIZE}")
        frontend = Frontend(args.core, directory, args.scenario)
        report["core"]["library_name"] = frontend.info.library_name.decode()
        report["core"]["library_version"] = frontend.info.library_version.decode()
        if args.scenario != "missing-media":
            # Core 1.4.0 actually opens this in the SAVE subdirectory. Its
            # README's older system-directory description is stale; recorded
            # core logs and the successful ARM9 file read verify this location.
            report["fat_fixture"] = fat_fixture(frontend.save / "melonDS DS" / "dldi_sd_card.bin",
                                                 args.scenario == "happy")
            image_before_load = Path(report["fat_fixture"]["path"]).read_bytes()
        frontend.load(args.rom)
        if "fat_fixture" in report:
            report["fat_fixture"]["host_stdio_runtime"] = flush_native_stdio()
            report["fat_fixture"]["host_stdio_flush_scope"] = (
                "fflush(NULL) return==0 checked; host C FILE buffers only. "
                "No ARM9 memory, registers or diagnostic state are written by the harness.")
            report["fat_fixture"]["sha256_after_emulator_load"] = sha(report["fat_fixture"]["path"])
            report["fat_fixture"]["emulator_load_delta"] = fixture_load_delta(
                image_before_load, Path(report["fat_fixture"]["path"]))
            image_before_load = None
        report["av"] = {"fps": frontend.av.timing.fps,
                         "sample_rate": frontend.av.timing.sample_rate}
        report["runtime"] = test_runtime(frontend, address, args.scenario, args.boot_frames)
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    finally:
        if frontend:
            report["checks"] = frontend.checks
            report["frames_run"] = frontend.frames
            report["video_callbacks"] = frontend.video_calls
            report["input_polls"] = frontend.polls
            report["pcm_sample_frames"] = len(frontend.pcm) // 4
            report["core_options"] = {key: value.decode() for key, value in frontend.variables.items()}
            report["environment_commands"] = frontend.env_calls
            report["last_observation"] = frontend.states[-1] if frontend.states else None
            if not report["passed"]:
                report["failure_images"] = frontend.capture("failure")
                if hasattr(frontend, "av"):
                    save_wav(directory / "failure-session.wav", frontend.pcm,
                             frontend.av.timing.sample_rate)
            write_json(directory / "observations.json", frontend.states)
            try:
                if "fat_fixture" in report:
                    flush_native_stdio()
                    report["fat_fixture"]["sha256_after_run"] = sha(report["fat_fixture"]["path"])
                frontend.close()
            except Exception:
                report["cleanup_error"] = traceback.format_exc()
                report["passed"] = False
            if "fat_fixture" in report:
                fixture = report["fat_fixture"]
                fixture["sha256_after"] = sha(fixture["path"])
                fixture["unchanged"] = fixture["sha256_after"] == fixture["sha256"]
                fixture["runtime_unchanged"] = fixture["sha256_after"] == fixture.get(
                    "sha256_after_emulator_load", fixture["sha256"]) == fixture.get(
                        "sha256_after_run", fixture["sha256_after"])
                fixture["load_mutation_reason"] = "melonDS CartHomebrew::SetupDirectBoot injects ROM into FAT"
                frontend.checks.append({"name": "FAT image unchanged during retro_run and unload",
                                        "passed": fixture["runtime_unchanged"]})
                if not fixture["runtime_unchanged"]:
                    report["passed"] = False
        write_json(directory / "result.json", report)
    print(f"N02 {args.scenario}: {'PASS' if report['passed'] else 'FAIL'}; {directory / 'result.json'}")
    return 0 if report["passed"] else 1


def archive_failed_run(out):
    """Retain compact failed evidence before replacing the current run outputs."""
    failed = []
    for scenario in SCENARIOS:
        report = out / scenario / "result.json"
        if report.is_file():
            result = json.loads(report.read_text(encoding="utf-8"))
            if not result.get("passed"):
                failed.append((scenario, report, result))
    if not failed:
        return
    base = ROOT / "research/evidence/nds/n02/emulator-failures"
    report_hashes = sorted(sha(report) for _scenario, report, _result in failed)
    for manifest in base.glob("attempt-*/manifest.json"):
        archived = json.loads(manifest.read_text(encoding="utf-8"))
        hashes = sorted(item["sha256"] for item in archived["artifacts"]
                        if item["copy"].endswith("-result.json"))
        if hashes == report_hashes:
            return
    index = 1
    while (base / f"attempt-{index:03d}").exists():
        index += 1
    destination = base / f"attempt-{index:03d}"
    destination.mkdir(parents=True)
    artifacts = []
    for scenario, report, result in failed:
        paths = [report, out / f"{scenario}.log"]
        paths += [Path(item["path"]) for item in result.get("failure_images", [])]
        for name in ("failure-session.wav", "tone.wav", "quiet.wav"):
            path = out / scenario / name
            if path.is_file():
                paths.append(path)
        for path in paths:
            if path.is_file() and path.resolve().is_relative_to(out.resolve()):
                target = destination / (scenario + "-" + path.name)
                shutil.copyfile(path, target)
                artifacts.append({"source": str(path), "copy": str(target), "sha256": sha(target)})
    write_json(destination / "manifest.json", {"task": "N02", "artifacts": artifacts,
               "scope": "Failed runtime reports/logs/captures; no ROM/ELF or disk image copies"})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True, help="melonDS DS libretro DLL/SO")
    parser.add_argument("--rom", type=Path, required=True)
    symbols = parser.add_mutually_exclusive_group(required=True)
    symbols.add_argument("--elf", type=Path, help="ARM9 ELF containing g_nds_diag")
    symbols.add_argument("--map", type=Path, help="ARM9 GNU linker map, if no ELF is available")
    parser.add_argument("--symbol", default="g_nds_diag")
    parser.add_argument("--out", type=Path, default=ROOT / "build/nds/n02/emulator")
    parser.add_argument("--boot-frames", type=int, default=600)
    parser.add_argument("--scenario", choices=SCENARIOS, help=argparse.SUPPRESS)
    args = parser.parse_args()
    for field in ("core", "rom", "elf", "map", "out"):
        value = getattr(args, field)
        if value is not None:
            setattr(args, field, value.resolve())
    # Output is deliberately constrained to project build/, including all core
    # system/save data. Never point the fixture writer at a physical device.
    if not args.out.is_relative_to((ROOT / "build").resolve()):
        parser.error("--out must stay inside this repository's build directory")
    if args.scenario:
        return worker(args)
    args.out.mkdir(parents=True, exist_ok=True)
    archive_failed_run(args.out)
    results = []
    for scenario in SCENARIOS:
        command = [sys.executable, str(Path(__file__).resolve()), "--core", str(args.core),
                   "--rom", str(args.rom), "--out", str(args.out), "--symbol", args.symbol,
                   "--boot-frames", str(args.boot_frames), "--scenario", scenario]
        command += ["--elf", str(args.elf)] if args.elf else ["--map", str(args.map)]
        log = args.out / (scenario + ".log")
        path = args.out / scenario / "result.json"
        if path.is_file():
            path.unlink()  # a previous run must never stand in for this worker
        with log.open("w", encoding="utf-8") as stream:
            try:
                completed = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, timeout=240)
                exit_code = completed.returncode
            except subprocess.TimeoutExpired:
                exit_code = -1
                stream.write("\nWorker exceeded 240 second runtime limit and was terminated.\n")
        result = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else {
            "passed": False, "error": "Worker exited without a result"}
        results.append({"scenario": scenario, "passed": result.get("passed", False)
                        and exit_code == 0, "exit_code": exit_code,
                        "report": str(path), "log": str(log)})
        print(f"N02 {scenario}: {'PASS' if results[-1]['passed'] else 'FAIL'}")
    report = {"schema": 1, "task": "N02", "passed": all(row["passed"] for row in results),
              "scenarios": results, "command": sys.argv,
              "rom_sha256": sha(args.rom), "core_sha256": sha(args.core),
              "symbols_sha256": sha(args.elf or args.map),
              "harness_sha256": sha(Path(__file__)), "hardware": "deferred by user until final build"}
    write_json(args.out / "result.json", report)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
