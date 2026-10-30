"""Build and verify N20 discovery against original PKR/PKE instructions."""
from __future__ import annotations
from compiler_cache import compiler_environment
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[4]
OUT=ROOT/'build/nds/n20/discovery'
EVIDENCE=ROOT/'research/evidence/nds/n20'
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def pin(path):return dict(path=Path(path).relative_to(ROOT).as_posix(),sha256=sha(path))
def summarize():
    path=EVIDENCE/'source-discovery.json';report=json.loads(path.read_text('utf8'))
    assert report['passed'] and report['complete_source_gate'] and report['source_pins_before_after_equal']
    assert report['originals_before']==report['originals_after']
    for p in report['source_pins']+[report['executable']]:assert sha(ROOT/p['path'])==p['sha256'],p
    for language,row in report['languages'].items():
        p=row['native_manifest'];assert sha(ROOT/p['path'])==p['sha256']
        manifest=json.loads((ROOT/p['path']).read_text('utf-8-sig'))
        assert manifest['sha256_before']==manifest['sha256_after']==report['originals_before'][language]
    result=dict(task='N20 source disclosure',passed=True,report=pin(path),generator=pin(__file__),
        original_executable_sha256=report['originals_before'],native_bounds_cases=report['native_bounds']['cases'],
        geometry_constructor_vectors=sum(r['constructor_vectors'] for r in report['languages'].values()),
        sprite_constructor_vectors=sum(r['sprite_constructor_vectors'] for r in report['languages'].values()),
        synthetic_column_cases=sum(r['column_cases'] for r in report['languages'].values()),
        synthetic_column_records=sum(r['column_records'] for r in report['languages'].values()),
        combined_actual_update_cases=report['combined_producer']['cases'],
        combined_records=sum(r['records'] for r in report['combined_producer']['rows']),
        sprite_provenance_negative_cases=report['sprite_provenance_negative_cases'],
        projectile_negative_update_cases=report['global_projectile_corner_fix']['nds_negative_update_cases'],
        source_pins=report['source_pins'],native_manifests=[r['native_manifest'] for r in report['languages'].values()],
        workspace_bytes=35268,persistent_bytes=0,
        scope=report['combined_producer']['scope'],
        explicit_correction=report['global_projectile_corner_fix'],
        whole_live_DOS_session=False,physical_DS_tested=False)
    target=ROOT/'analysis/nds-n20-discovery-source.json';target.write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps({k:result[k] for k in ('passed','geometry_constructor_vectors','sprite_constructor_vectors',
        'combined_actual_update_cases','synthetic_column_cases')}))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summarize-only',action='store_true',help='verify current full gate pins and update compact summary')
    parser.add_argument('--zig',type=Path,default=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe')
    args=parser.parse_args()
    if args.summarize_only:summarize();return
    OUT.mkdir(parents=True,exist_ok=True)
    env=compiler_environment(ROOT)
    sources=['ports/nds/tests/map_discovery_dump.c','ports/nds/ui/map_discovery.c',
        'ports/nds/render/sprites.c','ports/nds/render/scene.c','ports/nds/render/geometry.c',
        'ports/nds/core/world.c','ports/nds/core/sha256.c']
    options=['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O2','-I','ports/nds/include',
        '-I','build/nds/n07/generated']+sources
    commands=[]
    for suffix,target in [('exe',[]),('linux-x86_64',['-target','x86_64-linux-musl','-static'])]:
        binary=OUT/('map_discovery_dump.'+suffix if suffix=='exe' else 'map_discovery_dump-'+suffix)
        cmd=[str(args.zig)]+options+target+['-o',str(binary)]
        subprocess.run(cmd,cwd=ROOT,env=env,check=True);commands.append(dict(argv=cmd,binary=pin(binary),exit_code=0))
    EVIDENCE.mkdir(parents=True,exist_ok=True)
    (EVIDENCE/'source-build.json').write_text(json.dumps(dict(passed=True,commands=commands,
        zig_version=subprocess.check_output([str(args.zig),'version'],text=True).strip(),
        compiler=pin(args.zig) if args.zig.is_relative_to(ROOT) else dict(path=str(args.zig),sha256=sha(args.zig)),
        source_pins=[pin(ROOT/p) for p in sources],linux_executed=False),indent=2)+'\n','utf8')
    subprocess.run([sys.executable,'-B','-X','utf8',str(ROOT/'ports/nds/tools/map/audit_discovery.py'),
        '--dump',str(OUT/'map_discovery_dump.exe')],cwd=ROOT,check=True)
    summarize()
if __name__=='__main__':main()
