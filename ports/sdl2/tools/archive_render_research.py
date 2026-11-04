"""Retain SP07 raw evidence in a hash-verified archive; remove isolated game copies."""
from pathlib import Path
import sys,json,hashlib,shutil
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'scripts'))
from storage_archive import pack
base=(ROOT/'research/evidence/source-port/sp07-render').resolve()
paths=[p for p in base.glob('*/memory*.bin')]+[p for p in base.glob('*/boot-memory.bin')]
archive=ROOT/'research/experiments/storage-archives/sp07-cold-memory-20261104.zip'
if paths:pack(archive,paths,True)
removed=[]
for report in base.glob('*/report.json'):
 p=(report.parent/'game').resolve()
 if not p.exists():continue
 if not p.is_relative_to(base) or p.name!='game' or p.parent!=report.parent.resolve():raise RuntimeError('Unsafe duplicate copy path')
 shutil.rmtree(p);removed.append(str(p.relative_to(ROOT)))
# Old intermediate OG frames can be identical. Keep the last indexed/picture pair.
p=base/'og-dos-02'
for suffix in ['idx','png']:
 files=sorted(p.glob('enter-*.'+suffix),reverse=True);seen=set()
 for f in files:
  digest=hashlib.sha256(f.read_bytes()).hexdigest()
  if digest in seen:f.unlink();removed.append(str(f.relative_to(ROOT)))
  else:seen.add(digest)
(base/'storage-report.json').write_text(json.dumps(dict(archive=str(archive.relative_to(ROOT)),packed_memory_files=len(paths),removed_reproducible_copies=removed),indent=2))
print('archived RAM',len(paths),'removed copies',len(removed))
