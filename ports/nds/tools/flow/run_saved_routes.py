"""Run an archived N22 route policy without changing current repository tools.

Loads and verifies only the four recorded Python source snapshots. The game
ROM is the chosen current production target. All game input remains physical
joypad/touch callbacks; this launcher does not read or write emulated RAM.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import types
import hashlib

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
sys.path.insert(0,str(ROOT/'ports/nds/tools/flow'))


def artifact(path):
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--record',type=Path,required=True)
    args,remaining=p.parse_known_args()
    record=args.record.resolve()
    assert record.is_relative_to(ROOT/'build/nds/n22') or record.is_relative_to(ROOT/'research/evidence/nds/n22')
    source=json.loads(record.read_text('utf8'))
    entries={Path(row['original']['path']).name:row for row in source['source_snapshots']}
    loaded={}
    for name in ('plan_routes','analyze_tactics','tactical_controller','run_routes'):
        row=entries[name+'.py'];path=ROOT/row['snapshot']['path']
        assert artifact(path)==row['snapshot'],('Archived source changed',path)
        module=types.ModuleType(name);module.__file__=str(path);module.__package__=''
        sys.modules[name]=module
        exec(compile(path.read_bytes(),str(path),'exec'),module.__dict__)
        if hasattr(module,'ROOT'):module.ROOT=ROOT
        if hasattr(module,'OUT'):module.OUT=ROOT/'research/evidence/nds/n22'
        loaded[name]=module
    route=loaded['run_routes'];base=route.RouteObserver
    class ArchivedObserver(base):
        def __init__(self,core,target,directory):
            super().__init__(core,target,directory)
            pins=[]
            for row in entries.values():
                original=ROOT/row['snapshot']['path']
                output=directory/'sources'/original.name
                output.write_bytes(original.read_bytes())
                pins.append(dict(original=row['original'],snapshot=artifact(output)))
            self.report['source_snapshots']=pins
            self.report['archived_policy_source']=artifact(record)
            self.report['archived_policy_launcher']=artifact(Path(__file__))
            out=directory/'sources'/(hashlib.sha256(Path(__file__).read_bytes()).hexdigest()+'-run_saved_routes.py')
            out.write_bytes(Path(__file__).read_bytes());self.report['archived_policy_launcher_snapshot']=artifact(out)
    route.RouteObserver=ArchivedObserver
    from route_profile import install_route_profiling
    install_route_profiling(ArchivedObserver)
    sys.argv=['run_routes.py',*remaining]
    return route.main()

if __name__=='__main__':raise SystemExit(main())
