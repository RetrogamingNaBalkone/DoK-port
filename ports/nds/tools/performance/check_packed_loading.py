"""Verify shipped packed frames/PCM against package pixels and original RES PCM."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
from check_audio_capture import original_clip


def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();out=target/'packed-check';out.mkdir(exist_ok=True)
    package=(target/'nitrofs/dok/assets.ndp').read_bytes()
    count=struct.unpack_from('<I',package,16)[0];entries={}
    for i in range(count):
        wire=package[160+i*128:288+i*128]
        asset=struct.unpack_from('<I',wire)[0]
        offset,size=struct.unpack_from('<II',wire,8)
        entries[asset]=package[offset:offset+size]
    data=(target/'nitrofs/dok/frames.dat').read_bytes()
    magic,version,count,size,decoded,*reserved=struct.unpack_from('<8I',data)
    assert (magic,version)==(0x31465044,1) and not any(reserved)
    payload=data[32+count*16:];assert len(payload)==size
    total=0
    for i in range(count):
        asset,offset,n,raw_size=struct.unpack_from('<4I',data,32+i*16)
        packed=payload[offset:offset+n];raw=bytearray();p=0
        while p<len(packed):
            code=packed[p];p+=1;amount=(code&127)+1
            if code&128:raw.extend(bytes([packed[p]])*amount);p+=1
            else:raw.extend(packed[p:p+amount]);p+=amount
        assert p==n and len(raw)==raw_size and raw==entries[asset],hex(asset)
        total+=len(raw)
    assert total==decoded
    zig=Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env=dict(os.environ,ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    dllpath=out/'codec.dll'
    subprocess.run([str(zig),'cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-shared','-Iports/nds/include',
                    'ports/nds/core/audio_codec.c','-o',str(dllpath)],cwd=ROOT,env=env,check=True)
    dll=C.CDLL(str(dllpath));decode=dll.dok_audio_decode_range
    decode.argtypes=[C.c_void_p,C.c_uint32,C.c_uint32,C.c_uint32,C.c_void_p,C.c_uint32]
    sounds=(target/'nitrofs/dok/sounds.dat').read_bytes();magic,version,clips,size=struct.unpack_from('<4I',sounds)
    assert (magic,version,clips)==(0x31415044,1,22)
    offset=16+clips*16;assert len(sounds)-offset==size
    for i in range(clips):
        asset,rate,samples,n=struct.unpack_from('<4I',sounds,16+i*16)
        raw=C.create_string_buffer(samples);packed=sounds[offset:offset+n];offset+=n
        assert decode(packed,n,samples,0,raw,samples)==0
        original=original_clip(asset>>30,(asset>>18)&4095)
        assert raw.raw==bytes(v^128 for v in original['samples']) and rate==original['nominal_rate']
    assert offset==len(sounds)
    exe=out/'catalog.exe'
    subprocess.run([str(zig),'cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-Iports/nds/include',
                    'ports/nds/tests/test_catalog_pages.c','ports/nds/platform/assets/file_reader.c','-o',str(exe)],cwd=ROOT,env=env,check=True)
    cache=json.loads(subprocess.check_output([str(exe),str(target/'nitrofs/dok/assets.ndp')],cwd=ROOT,text=True))
    report=dict(passed=True,frames=count,decoded_bytes=decoded,sound_clips=clips,sound_packed_bytes=size,catalog=cache,
                frames_sha256=hashlib.sha256(data).hexdigest(),sounds_sha256=hashlib.sha256(sounds).hexdigest())
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report))


if __name__=='__main__':main()
