"""Targeted title retirement + LEVEL1 sound capture; no route or level sweep."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import run_frontend_emulator as front
from check_audio_capture import original_clip,compare_capture

class Observer(front.FrontendObserver):
    def __init__(self,target,directory):
        super().__init__(front.CORE,target,directory)
        self.capture_pcm=bytearray();self.events=[];self.last_start=0
        self.current_at,_=front.transport.elf_symbol(target/'dok_nds_gameplay.elf','current')
    def observe_audio(self):
        raw=bytes(self.pcm);self.capture_pcm.extend(raw)
        super().observe_audio()
        a=self.report.get('last_audio_probe')
        if not a or a[14]==self.last_start:return
        self.last_start=a[14]
        if a[5]==0x8000006e:return
        ptr=struct.unpack('<I',front.render.ram(self,self.current_at,4))[0]
        assert ptr
        pcm,rate,samples,packed_bytes,packed=struct.unpack('<5I',front.render.ram(self,ptr,20))
        index=pcm>>18&4095
        self.events.append(dict(start_sample=len(self.capture_pcm)//4,
            index=index,identity=a[5],samples=samples,period=a[12],rate=rate))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('target',type=Path);args=parser.parse_args()
    target=args.target.resolve();directory=target/'audio-transition';directory.mkdir(exist_ok=True)
    f=Observer(target,directory);f.options['melonds_homebrew_sdcard']='disabled'
    report=dict(passed=False,scope='Title stop and stationary LEVEL1 audio only; no routes')
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1);f.tick(120)
        f.tap(front.A);f.wait_for(2)
        a=f.report['last_audio_probe'];ring=a[28]
        assert not a[4]
        assert not any(front.render.ram(f,ring,65536)), 'Retired title PCM remains in ring'
        report['title_retired_zero_bytes']=65536
        f.tap(front.A);f.wait_for(3);f.tap(front.A);f.text_to(7,8);f.wait_for(8)
        f.tick(150)
        before=f.report['last_audio_probe'];f.joypad=1<<11  # real libretro R shoulder = fire
        f.tick(60);f.joypad=0;f.tick(10)
        after=f.report['last_audio_probe']
        report['firing']=dict(requests=after[18]-before[18],starts=after[14]-before[14],
            busy_rejects=after[15]-before[15],underruns=after[8],stale=after[9])
        # LEVEL1 starts without a weapon: this input cannot validate shots.
        report['firing']['weapon_present']=bool(f.probe[51])
        assert not after[8] and not after[9] and not after[22]
        wav=directory/'transition.wav'
        front.transport.save_wav(wav,f.capture_pcm,f.av.timing.sample_rate)
        report['effects']=[]
        for i,event in enumerate(f.events):
            # Include pre-roll because status is published after a sound starts.
            start=max(0,event['start_sample']-round(.15*f.av.timing.sample_rate))
            end=(f.events[i+1]['start_sample'] if i+1<len(f.events) else len(f.capture_pcm)//4)
            path=directory/f'effect-{i}-{event["index"]}.wav'
            front.transport.save_wav(path,f.capture_pcm[start*4:end*4],f.av.timing.sample_rate)
            wave=compare_capture(path,original_clip(1,event['index']),33513982/(2*event['period']),
                search_seconds=.3,output_rate=f.av.timing.sample_rate)
            assert wave['complete_source_covered'] and wave['whole_coefficient']>.95,(event,wave)
            report['effects'].append(dict(**event,coefficient=wave['whole_coefficient']))
        assert len(report['effects'])>=1
        report['passed']=True
    finally:
        f.close();front.transport.flush_native_stdio()
        report['source_sha256']=front.transport.sha(Path(__file__))
        report['rom_sha256']=front.transport.sha(target/'dok_nds_gameplay.nds')
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(report))

if __name__=='__main__':main()
