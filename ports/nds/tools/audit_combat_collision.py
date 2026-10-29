"""Run unchanged N10 source oracle, storing this fresh regression under N15.
Only the task-specific output destination changes; input pins/CPU checks stay.
"""
from pathlib import Path
import audit_collision as source
def destination(path):
    p=Path(path).absolute();allowed=source.ROOT/'research/evidence/nds/n15/original-collision.json'
    if p!=allowed or p.resolve()!=p:raise ValueError('Only N15 regression destination is allowed')
    if any(a.is_symlink() or (a.exists() and a.is_junction()) for a in (p,*p.parents)):raise ValueError('Output alias')
    if p.exists() and (not p.is_file() or p.stat().st_nlink!=1):raise ValueError('Output alias')
    p.parent.mkdir(parents=True,exist_ok=True);return p
if __name__=='__main__':
    source.destination=destination
    source.main()
