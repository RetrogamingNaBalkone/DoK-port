"""Compare discovery cells with final visible descriptors in coherent original DOS RAM.

Uses source-state captures from SP07, not a geometric model or golden rebaseline.
The original descriptor flag1 is read after native horizontal occlusion. This is
a per-frame cell-set comparison, not equality with cumulative visited history.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'research/evidence/source-port/sp15-map'
CAPTURES = ROOT / 'research/evidence/source-port/sp07-render'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def restore(path):
    if not path.exists():
        subprocess.run([sys.executable, '-B', str(ROOT/'scripts/storage_archive.py'),
                        'restore', path.relative_to(ROOT).as_posix()], check=True, cwd=ROOT)


def capture_cells(folder, heading, old_gold):
    report_path = folder/'report.json'
    report = json.loads(report_path.read_text())
    memory_path = folder/f'heading-{heading}-memory.z'
    state_path = folder/f'state-{heading}.bin'
    restore(memory_path)
    restore(state_path)
    memory = zlib.decompress(memory_path.read_bytes())
    base = int(report['data_base'],16)
    ldt = 0 if old_gold else int(report['surface_address']['ldt_linear'],16)

    def far(at):
        offset, selector = struct.unpack_from('<HH',memory,at)
        if old_gold:
            return selector*16+offset
        descriptor = memory[ldt+(selector&~7):ldt+(selector&~7)+8]
        assert len(descriptor)==8 and descriptor[5]&0x80, hex(selector)
        return int.from_bytes(descriptor[2:4],'little')+(descriptor[4]<<16)+(descriptor[7]<<24)+offset

    delta = 0xb0 if not old_gold and 'en-' in folder.name else 0
    count_offset, array_offset = (0x1fdc,0x1fde) if old_gold else (0xaa38+delta,0xaa3a+delta)
    count = struct.unpack_from('<H',memory,base+count_offset)[0]
    assert 0<count<10000
    array = far(base+array_offset)
    records = []
    for i in range(count):
        at = far(array+i*4)
        cell = struct.unpack_from('<H',memory,at+4)[0]
        flags = memory[at+6]
        records.append(dict(address=hex(at),cell=cell,flags=flags))
    cells = sorted({r['cell'] for r in records if r['flags']&1 and r['cell']<4096})
    visited_offset = 0xa2b6 if old_gold else (0x9446 if 'en-' in folder.name else 0x9396)
    visited = memory[base+visited_offset:base+visited_offset+4096]
    assert all(visited[c] for c in cells), 'Native surviving record not in cumulative visited'
    pose = next(item['pose'] for item in report['control_poses'] if item['heading']==heading)
    if len(pose)==3:
        pose = pose+[heading]
    return dict(capture=str(folder.relative_to(ROOT)),heading=heading,pose=pose,
                records=count,visible_records=sum(bool(r['flags']&1) for r in records),
                native_cells=cells,native_cumulative_visited_count=sum(bool(v) for v in visited),
                capture_report_sha256=sha(report_path),memory_sha256=sha(memory_path),
                state_sha256=sha(state_path)),state_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'build/sdl2/windows/dok_map_discovery_probe.exe')
    parser.add_argument('--extract-only',action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    originals=[ROOT/p for p in (
        'originals/pkr/RUS/PKR.EXE','originals/pkr/RUS/PKR.RES',
        'originals/pkr/ENG/PKE.EXE','originals/pkr/ENG/PKE.RES',
        'originals/Old_Gold_3D/OG.EXE','originals/Old_Gold_3D/OLD_GOLD.RES')]
    before={str(p.relative_to(ROOT)):sha(p) for p in originals}
    cases=[]
    for lang, resource in [('ru','originals/pkr/RUS/PKR.RES'),('en','originals/pkr/ENG/PKE.RES')]:
        for level in (1,2,3):
            cases += [(CAPTURES/f'{lang}-level{level}-vga-08',heading,False,resource,f'LEVEL{level}')
                      for heading in (0,45,135)]
    cases += [(CAPTURES/'og-dos-03',heading,True,'originals/Old_Gold_3D/OLD_GOLD.RES','LEVEL4')
              for heading in (83,0,45,135)]
    rows=[]
    for folder,heading,old_gold,resource,level in cases:
        row,state = capture_cells(folder,heading,old_gold)
        row.update(resource=resource,level=level)
        if not args.extract_only:
            command=[str(args.exe.resolve()),str(ROOT/resource),level,str(heading),
                     '--native-state',str(state),'--pose',*map(str,row['pose'])]
            process=subprocess.run(command,capture_output=True,text=True,timeout=30,cwd=ROOT)
            assert process.returncode==0,(command,process.stderr)
            actual=json.loads(process.stdout)['cells']
            row.update(command=command,actual_cells=actual,
                       missing=sorted(set(row['native_cells'])-set(actual)),
                       extra=sorted(set(actual)-set(row['native_cells'])))
            row['passed']=not row['missing'] and not row['extra']
            print(folder.name,heading,'PASS' if row['passed'] else 'FAIL',
                  'native',len(row['native_cells']),'port',len(actual),
                  'missing',row['missing'],'extra',row['extra'],flush=True)
        rows.append(row)
    after={str(p.relative_to(ROOT)):sha(p) for p in originals}
    assert before==after, 'Original resources changed'
    result=dict(boundary=__doc__,mode='extract_only' if args.extract_only else 'native_comparison',
                cases=rows,comparison_executable_sha256=None if args.extract_only else sha(args.exe),
                original_sha256_before=before,original_sha256_after=after)
    name='discovery-native-expected.json' if args.extract_only else 'discovery-native-report.json'
    (OUT/name).write_text(json.dumps(result,indent=2)+'\n')
    if not args.extract_only:
        assert all(row['passed'] for row in rows), 'Native discovery cell-set mismatch'
    print(len(rows),'original DOS capture cell sets extracted' if args.extract_only else 'native discovery comparisons PASS')


if __name__=='__main__':
    main()
