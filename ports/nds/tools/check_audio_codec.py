"""N18 lossless codec proof against independent original RES bytes."""
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import sys

from check_audio_capture import original_clip

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n18/codec"
EVIDENCE = ROOT / "research/evidence/nds/n18"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    zig = Path.home()/"AppData/Local/Programs/Zig/0.13.0/zig.exe"
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT/"build/zig-global-cache"),
               ZIG_LOCAL_CACHE_DIR=str(ROOT/"build/zig-local-cache"))
    command = [str(zig), "cc", "-std=c11", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
               "-O2", "-shared", "-Iports/nds/include", "ports/nds/core/audio_codec.c", "-o", str(OUT/"codec.dll")]
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    dll = C.CDLL(str(OUT/"codec.dll"))
    pack, decode = dll.dok_audio_pack, dll.dok_audio_decode_range
    pack.argtypes = [C.c_void_p,C.c_uint32,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]
    decode.argtypes = [C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32]
    report = dict(task="N18", passed=False, command=command, clips=[], checks=0,
                  scope="Independent original RES PCM byte comparisons, malformed streams and boundary/range cases; not emulator audio")
    def require(value):
        report["checks"] += 1
        if not value:
            raise AssertionError("Audio codec check failed")

    for language in (1,2):
        for resource in range(86,108):
            clip = original_clip(language,resource)
            # PCM8 signed/unsigned are both byte streams; modulo delta coding
            # must reproduce the signed bank representation byte-for-byte.
            raw = bytes(v^128 for v in clip["samples"])
            n = len(raw)
            source = C.create_string_buffer(raw)
            encoded = C.create_string_buffer(n+(n+255)//256*4+8)
            size = C.c_uint32()
            require(pack(source,n,encoded,len(encoded),C.byref(size)) == 0)
            full = C.create_string_buffer(n)
            require(decode(encoded,size.value,n,0,full,n) == 0)
            require(full.raw == raw)
            starts = sorted({0,1,255,256,257,max(0,n-32768),n-1,n})
            for start in starts:
                if start > n:
                    continue
                for amount in (0,1,255,256,257,32768):
                    count = min(amount,n-start)
                    part = C.create_string_buffer(max(1,count))
                    require(decode(encoded,size.value,n,start,part,count) == 0)
                    require(part.raw[:count] == raw[start:start+count])
            require(decode(encoded,size.value-1,n,0,full,n) != 0)
            require(decode(encoded,size.value,n+1,0,full,n) != 0)
            corrupted = C.create_string_buffer(encoded.raw[:size.value])
            corrupted[8] = 255
            require(decode(corrupted,size.value,n,0,full,n) != 0)
            report["clips"].append(dict(language=language,resource_index=resource,
                                        samples=n,packed_bytes=size.value,
                                        source_pcm_sha256=clip["pcm_sha256"]))
    # Controls exercise wrap, raw fallback, constant blocks and each boundary
    # around the block size, independently of properties of actual sounds.
    for n in (1,2,255,256,257,512,513):
        for raw in (bytes([128])*n,bytes(i%256 for i in range(n)),
                    bytes((i*137+i*i*19)%256 for i in range(n))):
            encoded=C.create_string_buffer(n+(n+255)//256*4+8);size=C.c_uint32()
            decoded=C.create_string_buffer(n)
            require(pack(raw,n,encoded,len(encoded),C.byref(size))==0)
            require(decode(encoded,size.value,n,0,decoded,n)==0 and decoded.raw==raw)
            require(pack(raw,n,encoded,7,C.byref(size))!=0)
    report["bytes_per_language"] = sum(r["packed_bytes"] for r in report["clips"] if r["language"]==1)
    report["passed"] = True
    (EVIDENCE/"codec-check.json").write_text(json.dumps(report,indent=2)+"\n","utf8")
    print(json.dumps({k:v for k,v in report.items() if k not in ("clips","command")}))


if __name__ == "__main__":
    main()
