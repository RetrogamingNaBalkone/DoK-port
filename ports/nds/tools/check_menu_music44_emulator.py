"""Continuous menu music, real input, two source periods and actual mixer PCM."""
import json
import hashlib
import sys
from pathlib import Path
import run_frontend_emulator as front
from check_audio_capture import compare_capture, original_clip


def main():
    target=Path(sys.argv[1]).resolve();story='--story' in sys.argv[2:]
    directory=target/('music45-story' if story else 'music44');directory.mkdir(exist_ok=True)
    f=front.FrontendObserver(front.CORE,target,directory)
    front.write_fixture(f.save/'melonDS DS/dldi_sd_card.bin',None,embedded=True)
    report=dict(passed=False,states=[])
    try:
        f.load(target/'dok_nds_gameplay.nds');f.wait_for(1)
        start=f.report['last_audio_probe'];previous=start[7]
        immutable=bool(start[39])
        source=original_clip(1,110)
        if immutable:
            assert start[40]==474364
            expected_pcm=bytes(value^128 for value in source['samples'])+b'\0'*3
            assert front.render.ram(f,start[39],start[40])==expected_pcm
            # DS hardware lengths are word-aligned; three silence samples.
            source['samples']+=b'\x80'*3
            source['pcm_sha256']=hashlib.sha256(source['samples']).hexdigest()
        def playing():
            nonlocal previous
            f.tick(200);a=f.report['last_audio_probe']
            assert a[4] and a[5]==0x8000006e and a[26]==1
            assert a[14]==start[14] and a[16]==start[16],'Navigation restarted music'
            assert a[7]>previous,'Audio consumer stopped'
            if immutable:
                assert a[39:41]==start[39:41]
                assert front.render.ram(f,a[39],a[40])==expected_pcm,'Menu PCM was mutated'
            previous=a[7]
            report['states'].append(dict(state=f.probe[3],selection=f.probe[5],consumed=a[7]))
        playing();f.tap(front.A);f.wait_for(2);playing()
        for item,state in ((1,4),(2,5),(4,11),(3,6),(0,3)):
            f.choose(2,item);f.tap(front.A);f.wait_for(state);playing()
            f.tap(front.B);f.wait_for(2)
        if story:
            f.choose(2,3);f.tap(front.A);f.wait_for(6);playing()
        # The same stream must cross two original sample periods without a start/stop.
        for _ in range(6000):
            a=f.report['last_audio_probe']
            if a[7]>2*a[24]+8192:break
            f.tick()
            if story:assert f.probe[3]==6,'Left the story during music verification'
        else:raise AssertionError('Two menu music periods did not complete')
        playing()
        wav=directory/'menu-two-periods.wav'
        front.transport.save_wav(wav,f.audio_pcm,f.av.timing.sample_rate)
        wave=compare_capture(wav,source,33513982/(2*start[12]),
                             repeats=2,output_rate=f.av.timing.sample_rate)
        assert wave['complete_source_covered'] and wave['whole_coefficient']>=.95,wave
        if story:
            f.tap(front.B);f.wait_for(2)
        f.choose(2,0);f.tap(front.A);f.wait_for(3);f.tap(front.A);f.wait_for(7);playing()
        f.text_to(7,8);f.tick(30)
        assert f.report['last_audio_probe'][5]!=0x8000006e,'Menu track leaked into gameplay'
        f.tap(front.START);f.wait_for(9)
        f.choose(9,2);f.tap(front.A);f.wait_for(5)
        assert f.report['last_audio_probe'][5]!=0x8000006e,'Pause controls started menu music'
        f.tap(front.B);f.wait_for(9);f.choose(9,3)
        before_return=f.report['last_audio_probe'][14]
        f.tap(front.A);f.wait_for(2)
        f.tick(200);a=f.report['last_audio_probe']
        assert a[4] and a[5]==0x8000006e and a[14]==before_return+1,(a,before_return)
        report.update(passed=True,story_hold=story,immutable_full_track=immutable,
                      waveform=wave,rom_sha256=front.transport.sha(target/'dok_nds_gameplay.nds'))
    finally:f.close();front.transport.flush_native_stdio()
    (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,states=len(report['states']),whole_coefficient=wave['whole_coefficient'])))


if __name__=='__main__':main()
