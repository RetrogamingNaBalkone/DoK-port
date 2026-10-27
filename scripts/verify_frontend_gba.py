"""UI01: real boot/keypad, original art, named levels and uninterrupted music.

Every screenshot and PCM sample comes from mGBA. Original RES and the saved
Ghidra data segments are independent oracles, not the generated menu copy.
"""
import argparse
from array import array
import ctypes as ct
import json
import os
from pathlib import Path
import struct
import wave

from verify_flow_gba import FlowProbe
from verify_ui_gba import glyph_mask
from verify_audio_gba import AV, sounds
from verify_gba import AUDIO, BATCH, sha
from verify_sprites_gba import original_animations
from verify_renderer_gba import stack_usage


class FrontendProbe(FlowProbe):
    def __init__(self, core, build):
        super().__init__(core, build)
        self.pcm=bytearray()
        self.batch_callback=BATCH(self.batch)
        self.single_callback=AUDIO(lambda l,r:self.pcm.extend(struct.pack('<hh',l,r)))
        self.frontend.core.retro_set_audio_sample_batch(self.batch_callback)
        self.frontend.core.retro_set_audio_sample(self.single_callback)
        self.frontend.core.retro_get_system_av_info.argtypes=[ct.POINTER(AV)]
        av=AV();self.frontend.core.retro_get_system_av_info(ct.byref(av))
        self.sample_rate=round(av.timing.sample_rate)

    def batch(self,data,count):
        self.pcm.extend(ct.string_at(data,count*4));return count

    def audio(self):
        names='magic active resource rate period position loops starts rejected stops completed irqs underruns max_gap refill_cycles ram_bytes level_resets requests identity'.split()
        return dict(zip(names,self.words('g_audio_diag',len(names))))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    args=parser.parse_args()
    core=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll'
    p=FrontendProbe(core,args.build_dir)
    try:
        p.until(lambda d:d['draws']>=2)
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        p.present();p.freeze()
        assert p.ui()['state']==5 and p.flow()['state']==0
        initial=p.audio();assert initial['active'] and initial['identity']==0x20000001 and initial['rate']==11025
        screens={};oracles=[]
        for language in (0,1):
            if language:
                p.command(2,language);p.command(1,5)
            resources,origin=original_animations(language)
            raw,palette=p.frame()
            splash=resources['ЗАСТАВКА_СПР'.encode('cp866').hex()]
            expected=bytes(splash[18+(y+40)*320+x] if y>=149 and x<81 else splash[18+(y*200//160)*320+x*320//240] for y in range(160) for x in range(240))
            expected=bytes(0 if y>=149 and 58<=x<81 and expected[y*240+x]!=196 else expected[y*240+x]
                           for y in range(160) for x in range(240))
            assert raw==expected
            rgb6=resources['ЗАСТАВКА_ПАЛ'.encode('cp866').hex()]
            expected_palette=b''.join(struct.pack('<H',(rgb6[i]>>1)|((rgb6[i+1]>>1)<<5)|((rgb6[i+2]>>1)<<10)) for i in range(0,768,3))
            assert palette==expected_palette
            screens[f'splash_{language}']=p.screen(f'ui01-splash-{language}')
            # Holding A opens only the menu, never its first action as well.
            p.until(lambda d:p.ui()['state']==1,buttons=(8,))
            for _ in range(5):p.frontend.run(1,(8,))
            assert p.ui()['state']==1 and p.flow()['session']==0
            p.press()  # release edge
            screens[f'menu_{language}']=p.screen(f'ui01-menu-{language}')
            p.press(8);assert p.ui()['state']==6
            segments=Path('build/ui')/('ru/1190.bin' if language==0 else 'en/10d0.bin')
            data=segments.read_bytes()
            names=(['ХОРОМЫ НЕЧИСТИ'.encode('cp866'),
                    'ПОДЗЕМНАЯ РЕКА'.encode('cp866'),'ПОДВАЛЫ КРЕМЛЯ'.encode('cp866')] if language==0 else
                   [b'PALACE OF EVIL',b'SACRED RIVER',b'UNDERGROUND KREMLIN'])
            assert all(name+b'\0' in data for name in names)
            raw,palette=p.frame()
            for i,name in enumerate(names):
                glyph_mask(raw,resources[b'FONT8'.hex()],22,52+i*22,name,86 if i==p.ui()['focus'] else 2)
            screens[f'levels_{language}']=p.screen(f'ui01-levels-{language}')
            before=p.world_sha();p.press(5);p.press(5);p.press(0)
            assert p.ui()['state']==1 and p.ui()['focus']==0 and p.world_sha()==before
            p.press(5);p.press(5);p.press(8)
            assert p.ui()['state']==3
            p.press(0);assert p.ui()['state']==1 and p.ui()['focus']==2
            for _ in range(2):p.press(4)
            oracles.append(dict(language=language,source=origin,original_splash_indices_exact=True,original_palette_exact=True,level_names_from_ghidra=True))
        # Retain one continuous capture across splash/menu/selection changes
        # and two actual source-end wraps, with no test playback commands.
        for _ in range(8000):
            if p.audio()['loops']>=2:break
            p.frontend.run(1)
        current=p.audio()
        assert current['loops']>=2 and current['starts']==initial['starts']==1
        assert current['rejected']==0 and current['underruns']==0,current
        wav=args.build_dir/'audio-check/ui01-menu-two-loops.wav';wav.parent.mkdir(exist_ok=True)
        with wave.open(str(wav),'wb') as f:
            f.setnchannels(2);f.setsampwidth(2);f.setframerate(p.sample_rate);f.writeframes(p.pcm)
        recording_seconds=len(p.pcm)/4/p.sample_rate
        assert recording_seconds>=2*474361*current['period']/16777216
        values=array('h',p.pcm)
        assert all(l==r for l,r in zip(values[::2],values[1::2]))
        music=next(r for r in sounds()[0] if r['count']==474361)
        # Recording began after boot. Compare an independent fresh once capture
        # in the audio regression; here verify both wrap neighborhoods agree.
        period=music['count']*current['period']/16777216
        samples=round(period*p.sample_rate)
        left=values[::2];stride=max(1,p.sample_rate//400)
        first=next(i for i,v in enumerate(left) if abs(v)>512)
        indices=range(first+p.sample_rate,min(first+samples-1000,len(left)-samples),stride)
        x=[left[i] for i in indices]
        y=[left[i+samples] for i in indices]
        mx=sum(x)/len(x);my=sum(y)/len(y)
        corr=sum((a-mx)*(b-my) for a,b in zip(x,y))/(sum((a-mx)**2 for a in x)*sum((b-my)**2 for b in y))**.5
        assert corr>.98,corr
        p.new_game(0,1)
        assert p.audio()['active']==0
        p.source_screens('BINTRO1',1,[]);p.wait_flow(2)
        p.press(2,3);assert p.ui()['state']==2 and p.audio()['identity']!=0x20000001
        p.press(0);p.wait_flow(2)
        p.to_title();assert p.audio()['identity']==0x20000001 and p.audio()['active']
        result=dict(task='G20B/UI01',status='passed',rom_sha256=sha(p.rom),script_sha256=sha(__file__),screens=screens,
                    art_oracles=oracles,music=dict(wav=wav.as_posix(),sha256=sha(wav),seconds=recording_seconds,
                    loops=current['loops'],starts=current['starts'],underruns=current['underruns'],max_irq_gap=current['max_gap'],
                    repeat_correlation=corr,stereo_channels_equal=True,stop_before_intro=True,no_menu_music_in_pause=True,restart_on_return=True),
                    held_splash_does_not_activate_menu=True,selection_cancel_preserves_world=True,stack=stack_usage(p.frontend),
                    physical_gba_tested=False,whole_dos_session_tested=False)
        args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print('UI01 original art, RU/EN named levels, held input, two music loops:',corr)
    finally:p.close()


if __name__=='__main__':main()
