"""Optional read-only N23 diagnostic timing ring; no product timing changes."""
import json
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_diag_emulator as transport
import run_renderer_emulator as render


def install_fine_observer():
    import route_profile
    cls=route_profile.RouteProfile
    initialize,observe,finish=cls.__init__,cls.observe,cls.finish
    def init(self,observer):
        initialize(self,observer)
        try:self.fine_address,_=transport.elf_symbol(observer.target/'dok_nds_gameplay.elf','g_nds_n23_profile')
        except Exception:self.fine_address=None
        self.fine_frames={};self.fine_callbacks={}
        self.fine_inputs=[]
        if self.fine_address:
            for source in (Path(__file__).resolve(),Path(sys.argv[0]).resolve()):
                if not source.is_file()or not source.is_relative_to(route_profile.ROOT):continue
                original=route_profile.pin(source)
                target=observer.directory/'sources'/(original['sha256']+'-'+source.name)
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source.read_bytes())
                self.fine_inputs.append(dict(source=original,snapshot=route_profile.pin(target)))
    def collect(self):
        observe(self)
        if not self.fine_address:return
        values=struct.unpack('<648I',render.ram(self.observer,self.fine_address,2592))
        if values[0]!=0x23504b44:return
        assert values[1]==1
        fields='frame_id compaction_ticks decode_ticks gather_ticks lookup_comparisons moved_bytes evictions decodes lookup_fast_hits callback_start callback_end level ordinal reserved3 reserved4 reserved5'.split()
        for offset in range(8,136,16):
            row=dict(zip(fields,values[offset:offset+16]))
            if row['frame_id']:self.fine_frames[row['frame_id']]=row
        for offset in range(136,648,4):
            number,kind,owner,ticks=values[offset:offset+4]
            if kind:self.fine_callbacks[number]=dict(number=number,kind=kind,owner=owner,ticks=ticks)
    def end(self):
        collect(self)
        if self.fine_address:
            report=dict(task='N23',scope='Diagnostic timers only; excluded from product cadence acceptance',
                observer_inputs=self.fine_inputs,
                timer_hz=33513982,frames=sorted(self.fine_frames.values(),key=lambda row:row['frame_id']),
                callbacks=sorted(self.fine_callbacks.values(),key=lambda row:row['number']))
            missing=[n for row in report['frames'] for n in range(row['callback_start'],row['callback_end']) if n not in self.fine_callbacks]
            report['missing_callbacks']=missing
            assert not missing,('Fine callback ring lost records',missing[:20])
            (self.observer.directory/'fine-profile.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
        finish(self)
    cls.__init__,cls.observe,cls.finish=init,collect,end
