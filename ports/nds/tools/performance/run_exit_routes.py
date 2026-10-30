"""Native-input saved policy with a recorded exit preference AFTER all map keys.

This changes only the host navigation intention after mandatory key collection.
The frozen N22 policy and N23 metadata derivation remain untouched. It neither
sets inventory nor edits emulated state; collision, enemies and combat stay live.
"""
import importlib.util,sys
from functools import reduce
from operator import or_
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'ports/nds/tools/flow'))
sys.path.insert(0,str(ROOT/'ports/nds/tools'))
import route_profile
install=route_profile.install_route_profiling
def install_exit_preference(cls):
    original_plan=cls.plan_current;original_init=cls.__init__
    def initialize(self,*args,**kwargs):
        original_init(self,*args,**kwargs)
        source=Path(__file__).resolve();item=route_profile.pin(source)
        destination=self.directory/'sources'/(item['sha256']+'-'+source.name)
        destination.write_bytes(source.read_bytes())
        self.report['host_policy_extension']=dict(source=item,snapshot=route_profile.pin(destination),
            rule='Prefer the source exit only after every source-map key mask is actually held; mandatory enemy/key segments are not skipped.')
    def plan_current(self,data,sensor_flags,avoid=()):
        required=reduce(or_,data.keys.values(),0)
        if self.probe and required and self.probe[50]&required==required:
            x,_,z,_=self.last_pose;start=int(z)//data.side*data.width+int(x)//data.side
            inventory=self.probe[50]|sensor_flags|(16 if self.probe[51]else 0)
            path,_=data.route(start=start,inventory=inventory,avoid=avoid)
            self.report.setdefault('exit_preferences',[]).append(dict(video_frame=self.frames,
                required_keys=required,actual_keys=self.probe[50],source_exit_cells=sorted(data.exits),
                planned_path=[data.xy(cell)for cell,flags in path]))
            return path
        return original_plan(self,data,sensor_flags,avoid)
    cls.__init__=initialize;cls.plan_current=plan_current
    install(cls)
route_profile.install_route_profiling=install_exit_preference
spec=importlib.util.spec_from_file_location('n23_saved_route_launcher',Path(__file__).with_name('run_saved_routes.py'))
saved=importlib.util.module_from_spec(spec);spec.loader.exec_module(saved)
saved.__doc__=__doc__
if __name__=='__main__':raise SystemExit(saved.main())
