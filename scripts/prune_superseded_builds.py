"""Remove reproducible old compiler outputs, retaining current ROMs and inputs."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time

from compress_build_logs import completed_file
from prune_test_storage import ROOT,local,relative,sha,write
from prune_archived_builds import PROTECTED,BASE_INPUTS

KEEP=tuple(p for p in PROTECTED if p!='build/nds/n23/')+tuple(p+'/' for p in BASE_INPUTS)+(
    'build/nds/n23/audio21-ru/','build/nds/n23/audio21b-ru/',
    'build/nds/n23/crashdiag22-ru/','build/nds/n23/candidate22-ru/',
    'build/nds/n23/candidate22-en/','build/nds/n23/standalone01-ru/')
EXTENSIONS={'.gba','.elf','.o','.obj','.pdb','.a','.lib','.exe','.dll','.so','.nds'}


def candidate(path):
    name=relative(local(path))
    if not name.startswith(('build/gba/','build/nds/')) or name.startswith(KEEP):
        return False
    # Other Makefile inputs (.dat/.inc/.ndspak) remain. NitroFS contents are
    # per-target copies made by the Makefile from those retained shared inputs.
    return (path.suffix.lower() in EXTENSIONS or
            ('/nitrofs/' in name and path.suffix.lower() in {'.ndp','.dat'}))


def plan(manifest):
    if manifest.exists():
        raise ValueError('Use a new manifest')
    tracked=set(subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0'))
    rows=[];cutoff=time.time()-24*3600
    for area in ('build/gba','build/nds'):
        for base,dirs,files in os.walk(ROOT/area,followlinks=False):
            for name in dirs:
                local(Path(base)/name)
            for name in files:
                path=Path(base)/name
                if candidate(path) and relative(path) not in tracked and path.stat().st_mtime<cutoff:
                    rows.append(dict(path=relative(path),bytes=path.stat().st_size,sha256=sha(path)))
    report=dict(schema=1,workspace=str(ROOT),delete=rows,files=len(rows),bytes=sum(r['bytes'] for r in rows),keep=list(KEEP))
    write(manifest,report);print(json.dumps({k:report[k] for k in ('files','bytes')}))


def apply(manifest):
    report=json.loads(manifest.read_text('utf8'))
    if report['schema']!=1 or report['workspace']!=str(ROOT):
        raise ValueError('Wrong workspace/schema')
    tracked=set(subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0'))
    for row in report['delete']:
        path=local(ROOT/row['path'])
        if not candidate(path) or row['path'] in tracked or path.stat().st_size!=row['bytes'] or sha(path)!=row['sha256']:
            raise ValueError(f'Changed or protected output: {path}')
    for row in report['delete']:
        path=local(ROOT/row['path'])
        with completed_file(path):
            if sha(path)!=row['sha256']:
                raise ValueError(f'Changed output: {path}')
            path.unlink()
    result=dict(passed=True,deleted_files=report['files'],deleted_bytes=report['bytes'],manifest_sha256=sha(manifest))
    write(manifest.with_name(manifest.stem+'-verified.json'),result)
    print(json.dumps(result))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=('plan','apply'))
    parser.add_argument('--manifest',type=Path,required=True)
    args=parser.parse_args()
    globals()[args.action](local(args.manifest))
