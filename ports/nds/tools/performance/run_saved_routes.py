"""Run a frozen N22 adaptive policy on N23 with an explicit metadata-only derivation.

No frozen source is edited. Only the old run_routes task label and output-root
assertion change in a separate derived copy. Navigation/input code stays exact.
"""
from __future__ import annotations
import argparse,hashlib,json,sys,types
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
sys.path.insert(0,str(ROOT/'ports/nds/tools/flow'))
def pin(p):
    raw=p.read_bytes();return dict(path=p.relative_to(ROOT).as_posix(),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
def verify(p,row):
    actual=pin(p);assert all(actual[k]==row[k]for k in ('bytes','sha256')),(p,actual,row)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--check-policy',action='store_true',help='Verify/freeze the policy derivation without creating an emulator')
    args,remaining=parser.parse_known_args();record=args.record.resolve()
    assert any(record.is_relative_to(ROOT/'research/evidence/nds'/x)or record.is_relative_to(ROOT/'build/nds'/x)for x in ('n22','n23'))
    report=json.loads(record.read_text('utf8'));originals={Path(x['original']['path']).name:x for x in report['source_snapshots']}
    folder=ROOT/'research/evidence/nds/n23/saved-policies'/pin(record)['sha256'];folder.mkdir(parents=True,exist_ok=True)
    derived=[];loaded={}
    for name in ('plan_routes','analyze_tactics','tactical_controller','run_routes'):
        row=originals[name+'.py'];expected=row['snapshot']
        candidates=[ROOT/expected['path'],record.parent/'sources'/Path(expected['path']).name]
        source=next((p for p in candidates if p.is_file() and pin(p)['sha256']==expected['sha256']),None)
        assert source is not None,('Frozen policy source unavailable',expected)
        verify(source,expected);raw=source.read_bytes();changes=[]
        if name=='run_routes':
            old=b"    assert target.is_relative_to(ROOT/'build/nds/n22')"
            new=b"    assert target.is_relative_to(ROOT/'build/nds/n23')"
            assert raw.count(old)==1;raw=raw.replace(old,new);changes.append(dict(old=old.decode(),new=new.decode(),occurrences=1))
            old=b"task='N22'";new=b"task='N23'";count=raw.count(old)
            assert count>0;raw=raw.replace(old,new);changes.append(dict(old=old.decode(),new=new.decode(),occurrences=count))
        destination=folder/(hashlib.sha256(raw).hexdigest()+'-'+name+'.py')
        if destination.exists():assert destination.read_bytes()==raw
        else:destination.write_bytes(raw)
        derived.append(dict(module=name,policy_original=row['original'],policy_snapshot=expected,
            resolved_original=pin(source),executed_source=pin(destination),changes=changes))
        module=types.ModuleType(name);module.__file__=str(destination);module.__package__='';sys.modules[name]=module
        exec(compile(raw,str(destination),'exec'),module.__dict__)
        if hasattr(module,'ROOT'):module.ROOT=ROOT
        if hasattr(module,'OUT'):module.OUT=ROOT/'research/evidence/nds/n23'
        loaded[name]=module
    manifest=dict(task='N23',policy_record=pin(record),launcher=pin(Path(__file__)),sources=derived,
        scope='Frozen adaptive native-input policy; only N22 task labels and allowed output root are derived. No game rules, input logic or emulated state are patched.')
    manifest_path=folder/'manifest.json';encoded=json.dumps(manifest,indent=2)+'\n'
    if manifest_path.exists():assert manifest_path.read_text('utf8')==encoded
    else:manifest_path.write_text(encoded,'utf8')
    if args.check_policy:
        print(json.dumps(dict(passed=True,manifest=pin(manifest_path),modules=len(derived),changed_modules=sum(bool(x['changes'])for x in derived))));return 0
    route=loaded['run_routes'];base=route.RouteObserver
    class ArchivedObserver(base):
        def __init__(self,core,target,directory):
            super().__init__(core,target,directory);self.report['task']='N23';snapshots=[]
            for item in derived:
                source=ROOT/item['executed_source']['path'];destination=directory/'sources'/source.name
                destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(source.read_bytes())
                snapshots.append(dict(original=item['executed_source'],snapshot=pin(destination)))
            self.report.update(source_snapshots=snapshots,archived_policy_source=pin(record),archived_policy_derivation=pin(manifest_path),archived_policy_launcher=pin(Path(__file__)))
            destination=directory/'sources'/(pin(Path(__file__))['sha256']+'-run_saved_routes.py');destination.write_bytes(Path(__file__).read_bytes())
            self.report['archived_policy_launcher_snapshot']=pin(destination)
    route.RouteObserver=ArchivedObserver
    from route_profile import install_route_profiling
    install_route_profiling(ArchivedObserver)
    sys.argv=['run_routes.py',*remaining];return route.main()
if __name__=='__main__':raise SystemExit(main())
