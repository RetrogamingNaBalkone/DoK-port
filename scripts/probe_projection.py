"""G04: independent integer oracle for the explicitly reconstructed scalar math.

This verifies C widths/rounding/defined overflow, not a DOS image match.
Does not open/disassemble EXEs. Uses saved runtime values once available.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def s32(n):
    return (n + 2**31) % 2**32 - 2**31


def oracle(row):
    x0, y0, z0, sine, cosine, distance, scale, focal, cx, cy, x, y, z = row
    dx, dy, dz = s32(x-x0), s32(y-y0), s32(z-z0)
    lateral = s32(s32(dx*cosine) + s32(dz*sine)) // 32768
    depth = s32(s32(dz*cosine) - s32(dx*sine)) // 32768
    numerator = s32(s32(scale*distance)*focal)
    denominator = s32(depth+distance)
    if denominator == 0 or (numerator == -2**31 and denominator == -1):
        return [lateral, depth, dy, 0, 123, 456, 789]
    factor = (abs(numerator)//abs(denominator)) * (-1 if (numerator < 0) != (denominator < 0) else 1)
    return [lateral, depth, dy, 1,
            s32(s32(lateral*factor)//32768+cx),
            s32(s32(dy*factor)//32768+cy), factor]


def artifact(path):
    return {'path': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--zig', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe')
    args = parser.parse_args()
    output = ROOT/'research/evidence/render-projection'
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-cache'))
    common = [str(args.zig), 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2', '-I', 'include']
    subprocess.run(common+['src/projection.c', 'tools/projection_probe.c', '-o', str(output/'probe.exe')],
                   cwd=ROOT, env=env, check=True)
    subprocess.run(common+['-target', 'thumb-freestanding-eabi', '-mcpu=arm7tdmi', '-ffreestanding',
                          '-c', 'src/projection.c', '-o', str(output/'projection-thumb.o')],
                   cwd=ROOT, env=env, check=True)
    rows = []
    # Chosen singular IDIV/overflow/negative SAR boundaries, not mirror examples.
    base = [0, 0, 0, 0, 32768, 200, 8, 4096, 159, 79]
    for point in ((0,0,0), (0,-300,480), (480,-300,480), (-1,-1,1),
                  (0,0,-200), (0,0,-201), (2**31-1,-2**31,2**31-1)):
        rows.append(base+list(point))
    rows.append([0,0,0,0,32768,-2,-32768,-32768,159,79,0,0,1])
    assert oracle(rows[0]) == [0,0,0,1,159,79,32768]
    assert oracle(rows[1]) == [0,480,-300,1,159,-10,9637]
    assert oracle(rows[4])[3] == 0 and oracle(rows[-1])[3] == 0
    rng = random.Random(0xD04)
    for _ in range(12000):
        rows.append([rng.randrange(-2**31,2**31) for _ in range(6)] +
                    [rng.randrange(-32768,32768) for _ in range(4)] +
                    [rng.randrange(-2**31,2**31) for _ in range(3)])
    # These sample the proven original startup pose/ranges, not synthetic trig.
    scene_rows = {}
    for language, run, delta, focal_offset in [('ru','ru-03',0,0x934), ('en','en-02',0xb0,0xa44)]:
        import struct
        report = json.loads((ROOT/f'research/evidence/reference/{run}/report.json').read_text(encoding='utf-8'))
        memory = (ROOT/f'research/evidence/reference/{run}/level1-memory.bin').read_bytes()
        ds = int(report['runtime_linear_data_base'],16)
        value = lambda fmt, at: struct.unpack_from(fmt,memory,ds+at+delta)[0]
        camera = [value('<i',at) for at in (0xaa46,0xaa4a,0xaa4e,0xaa3e,0xaa42,0xaa16)]
        camera += [value('<h',0xaa32),struct.unpack_from('<h',memory,ds+focal_offset)[0],
                   value('<h',0xaa1e),value('<h',0xaa20)]
        assert camera[:3] == report['player_start']['xyz']
        assert camera[3:5] == [32768,0] and camera[5:] == [200,8,4096,159,79]
        start = len(rows)
        # +/-12 cells from camera, vertical coordinates spanning floor/ceiling.
        for dx in range(-12*480,12*480+1,240):
            for dz in range(-12*480,12*480+1,240):
                for y in (-960,-480,0,480):
                    rows.append(camera+[camera[0]+dx,y,camera[2]+dz])
        scene_rows[language] = {'camera':camera,'vector_count':len(rows)-start,
                                'source':artifact(ROOT/f'research/evidence/reference/{run}/level1-memory.bin')}
    vectors = ''.join(' '.join(map(str,row))+'\n' for row in rows)
    result = subprocess.run([str(output/'probe.exe')],input=vectors,text=True,capture_output=True,check=True)
    actual = [list(map(int,line.split())) for line in result.stdout.splitlines()]
    expected = [oracle(row) for row in rows]
    assert actual == expected, next(((i,a,e) for i,(a,e) in enumerate(zip(actual,expected)) if a != e), 'row count')
    (output/'vectors.txt').write_text(vectors,encoding='ascii')
    (output/'results.txt').write_text(result.stdout,encoding='ascii')
    summary = {'task':'G04','status':'passed','vectors':len(rows),
               'random_seed':'0xd04','invalid_signed_divisions':sum(row[3]==0 for row in actual),
               'scenes':scene_rows,'inputs':[artifact(ROOT/path) for path in
                    ('include/dok/projection.h','src/projection.c','tools/projection_probe.c','scripts/probe_projection.py')],
               'output_hashes':[artifact(output/name) for name in ('vectors.txt','results.txt')],
               'native_execution':True,'thumb_compilation':True,'thumb_execution':False,
               'limit':'Scalar low32 transform/project oracle; not DOS raster/image equivalence or GBA performance.'}
    (ROOT/'analysis/projection-probe.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'passed','vectors':len(rows),'scenes':scene_rows},ensure_ascii=False))


if __name__ == '__main__':
    main()
