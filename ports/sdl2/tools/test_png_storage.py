"""Decode native PNGs with independent Python zlib; verify pixels and size."""
import os
from pathlib import Path
import struct
import subprocess
import unittest
import zlib

from compiler_cache import ROOT, compiler_cache
from experiment_cleanup import temporary_workspace


class PngStorageTest(unittest.TestCase):
    def test_native_encoder(self):
        zig = Path(os.environ['LOCALAPPDATA']) / 'Programs/Zig/0.13.0/zig.exe'
        with temporary_workspace(ROOT/'build/sdl2/storage-tests', 'png-') as out:
            with compiler_cache():
                subprocess.run([str(zig), 'c++', '-std=c++17', '-O2',
                                '-I'+str(ROOT/'ports/sdl2/include'),
                                str(ROOT/'ports/sdl2/src/png.cpp'),
                                str(ROOT/'ports/sdl2/tests/png_probe.cpp'),
                                '-o', str(out/'probe.exe')], check=True, timeout=180)
                subprocess.run([str(out/'probe.exe'), str(out)], check=True, timeout=30)
            for kind in range(4):
                with self.subTest(kind=kind):
                    data = (out/f'{kind}.png').read_bytes()
                    self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
                    pos, packed = 8, bytearray()
                    while pos < len(data):
                        size = struct.unpack_from('>I', data, pos)[0]
                        tag = data[pos+4:pos+8]; payload = data[pos+8:pos+8+size]
                        self.assertEqual(zlib.crc32(tag+payload), struct.unpack_from('>I', data, pos+8+size)[0])
                        if tag == b'IHDR':
                            w, h, depth, color, comp, filt, inter = struct.unpack('>IIBBBBB', payload)
                            self.assertEqual((depth,color,comp,filt,inter), (8,2,0,0,0))
                        if tag == b'IDAT':
                            packed.extend(payload)
                        pos += size+12
                    raw = zlib.decompress(packed)
                    expected = bytearray(); random = 123456789
                    for y in range(h):
                        expected.append(0)
                        for x in range(w):
                            random ^= (random << 13) & 0xffffffff
                            random ^= random >> 17
                            random ^= (random << 5) & 0xffffffff
                            p = (0x123456 if kind == 0 else 0x112233 if kind == 1 else
                                 ((x*17+y%7)&255)*0x010101 if kind == 2 else random)
                            expected.extend(((p>>16)&255, (p>>8)&255, p&255))
                    self.assertEqual(raw, expected)
                    if kind == 1:
                        self.assertLess(len(data), len(raw)//50)
                    print(f'PNG case {kind}: {len(raw)} raw -> {len(data)} bytes', flush=True)
            self.assertFalse((out/'invalid.png').exists())


if __name__ == '__main__':
    unittest.main()
