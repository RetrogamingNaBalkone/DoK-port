"""Ordinary inputs, with every-video-frame hold and AI clock observations."""
import array
import struct
from pathlib import Path
import ordinary
from ordinary import ROOT,transport,render


class Observer(ordinary.Observer):
    def __init__(self,core,target,directory):
        super().__init__(core,target,directory)
        source=Path(__file__);snapshot=directory/'sources'/('playtest-probe-'+transport.sha(source)+'.py')
        snapshot.write_bytes(source.read_bytes())
        self.report['playtest_observer']=dict(source=ordinary.pin(source),snapshot=ordinary.pin(snapshot))
        self.hold_address,_=transport.elf_symbol(target/'dok_nds_gameplay.elf','capture_hold')
        try:self.capture_probe_address,_=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_capture_probe')
        except ValueError:self.capture_probe_address=None
        self.sprite_address,_=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_sprite_probe')
        package=(target/'nitrofs/dok/assets.ndp').read_bytes();entries=struct.unpack_from('<I',package,16)[0]
        self.frame_intervals={}
        for i in range(entries):
            wire=package[160+i*128:288+i*128]
            if struct.unpack_from('<H',wire,16)[0]!=3:continue
            at,n=struct.unpack_from('<II',wire,8);meta=package[at:at+n];frames=struct.unpack_from('<H',meta)[0]
            for j in range(frames):
                image,interval=struct.unpack_from('<Ih',meta,8+j*16)
                self.frame_intervals[image]=(interval,j,frames)
        self.hold_reference=None
        self.previous_ai={}
        self.previous_doors={};self.door_openings={};self.last_notice=None
        self.ui_address,_=transport.elf_symbol(target/'dok_nds_gameplay.elf','g_nds_ui_probe')
        self.clock_start=None
        self.clock_epoch=None
        self.audit=dict(hold_video_frames=0,hold_sequences=0,hold_unchanged_checks=0,
                        release_frame_checks=0,release_same_capture=0,
                        captured_lcd_sample_checks=0,source_interval_checks=0,
                        animation_changes=[],clock_samples=[],ai_snapshots=0,door_openings=[],notice_checks=0,c2_samples=[])
        self.report['playtest_audit']=self.audit

    def screen_key(self):
        width,height,pitch,fmt,raw=self.last_video
        assert width==256 and height==384 and fmt==1
        pixels=array.array('I',raw[:pitch*192])
        # Capture stores RGB555; compare at that precision, without requiring
        # the 3D core's sixth component bit to survive hardware capture.
        return bytes().join(struct.pack('<I',p&0x00f8f8f8) for p in pixels)

    def tick(self,count=1):
        for _ in range(count):
            super().tick()
            if not self.probe or self.probe[3]!=8:
                self.hold_reference=None;self.clock_start=None;self.previous_doors={};self.door_openings={};continue
            held=struct.unpack('<I',render.ram(self,self.hold_address,4))[0]
            if held or self.hold_reference is not None:
                key=self.screen_key()
                assert any(key[:256*154*4]),'Texture replacement blanked the whole 3D scene'
                if held and self.hold_reference is not None:
                    assert key==self.hold_reference,('Held LCD changed during texture replacement',self.frames,held)
                    self.audit['hold_unchanged_checks']+=1
                elif not held and self.hold_reference is not None:
                    self.audit['release_frame_checks']+=1
                    self.audit['release_same_capture']+=key==self.hold_reference
                elif held:
                    self.audit['hold_sequences']+=1
                    if self.audit['hold_sequences']<=3:self.capture('held-'+str(self.audit['hold_sequences']))
                self.hold_reference=key if held else None
                if held:self.audit['hold_video_frames']+=1
                if held and self.capture_probe_address:
                    p=struct.unpack('<64I',render.ram(self,self.capture_probe_address,256))
                    assert p[0]==0x38504344 and p[1]==1 and not p[2]&1 and p[6] and p[8]==36
                    _,_,pitch,fmt,raw=self.last_video;assert fmt==1
                    for n in range(36):
                        x=16+n%8*32 if n<32 else 32+(n-32)*64
                        y=8+n//8*40 if n<32 else 178
                        b,g,r=raw[y*pitch+x*4:y*pitch+x*4+3]
                        actual=(r>>3)|((g>>3)<<5)|((b>>3)<<10)
                        assert actual==p[16+n],('Captured framebuffer differs from LCD',self.frames,x,y,actual,p[16+n])
                        self.audit['captured_lcd_sample_checks']+=1
            if not getattr(self,'ai_reader',None) or not self.flow or not self.game:continue
            sample=self.ai_reader.sample(lambda a,n:render.ram(self,a,n))
            if not sample['accepted']:continue
            self.audit['ai_snapshots']+=1
            layout=self.ai_reader.layout;em=layout['enemy']['members'];am=layout['ai']['members']
            ticks=self.game[29]
            if 'door' in layout:
                game_address=sample['ai_address']-layout['game']['members']['ai']
                c2=struct.unpack('<I',render.ram(self,game_address+layout['game']['members']['c2'],4))[0]
                if self.frames%60==0:self.audit['c2_samples'].append(c2)
                door_address=game_address+layout['game']['members']['interactions']+layout['interactions']['members']['doors']
                raw_doors=render.ram(self,door_address,256*layout['door']['bytes'])
                for glyph in range(256):
                    phase,extent,state,ready=struct.unpack_from('<hhBB',raw_doors,glyph*layout['door']['bytes'])
                    old=self.previous_doors.get(glyph)
                    if glyph in self.door_openings and (extent!=self.door_openings[glyph]['extent'] or (old and phase<old[0])):
                        self.door_openings.pop(glyph)
                    if old and old[0]==0 and phase>0 and state==2 and not ready:
                        self.door_openings[glyph]=dict(glyph=glyph,start_frame=self.frames,start_ticks=ticks,first_phase=phase,extent=extent,c2=c2)
                    if glyph in self.door_openings and state==1 and ready:
                        event=self.door_openings.pop(glyph);event.update(end_frame=self.frames,end_ticks=ticks,duration_ticks=ticks-event['start_ticks'])
                        assert phase==extent-1,('Door did not finish on original final phase',glyph,phase,extent)
                        self.audit['door_openings'].append(event)
                    self.previous_doors[glyph]=(phase,extent,state,ready)
                ui=struct.unpack('<64I',render.ram(self,self.ui_address,256))
                if ui[0]==0x19504b44 and not ui[2]&1 and ui[17] and not ui[30]:
                    assert ui[22]-ui[21]==40107*ui[20],('Notice lifetime differs from four seconds',ui[21:23],ui[20])
                    self.audit['notice_checks']+=1
            if self.clock_epoch!=sample['epoch']:
                self.clock_start=None;self.previous_ai={};self.clock_epoch=sample['epoch']
            if self.clock_start is None:self.clock_start=(self.frames,ticks)
            if self.frames%60==0:
                delta=self.frames-self.clock_start[0]
                if delta:self.audit['clock_samples'].append(dict(video_frames=delta,ticks=ticks-self.clock_start[1],
                    ticks_per_second=(ticks-self.clock_start[1])*self.av.timing.fps/delta))
            for enemy in sample['enemies']:
                pointer=sample['ai_address']+am['enemies']+enemy['index']*layout['enemy']['bytes']
                raw=render.ram(self,pointer,layout['enemy']['bytes'])
                cursor=struct.unpack_from('<h',raw,em['displayed_cursor'])[0]
                kind=raw[em['displayed_kind']]
                key=(enemy['state'],kind,cursor)
                sprite=struct.unpack('<34I',render.ram(self,self.sprite_address,136))
                if sprite[0]!=0x12504b44 or sprite[3]&1:continue
                assert sprite[11]==20
                image=struct.unpack('<I',render.ram(self,sprite[9]+enemy['glyph']*sprite[11],4))[0]
                interval,index,frame_count=self.frame_intervals.get(image,(0,0,0))
                old=self.previous_ai.get(enemy['glyph'])
                if old and key!=old['key'] and enemy['state'] and not enemy['corpse']:
                    delta=enemy['last_motion_ticks']-old['motion_ticks']
                    if enemy['state']==old['key'][0]==1 and kind==old['key'][1]==0 and interval>0 and old['interval']>0 and\
                       image>>10==old['image']>>10 and index==(old['index']+1)%frame_count:
                        assert delta>=old['interval'],('Enemy animation advanced before original interval',enemy['glyph'],delta,old['interval'])
                        self.audit['source_interval_checks']+=1
                    self.audit['animation_changes'].append(dict(glyph=enemy['glyph'],state=enemy['state'],kind=kind,
                        cursor=cursor,video_frame=self.frames,video_delta=self.frames-old['video'],
                        source_tick_delta=ticks-old['ticks'],motion_tick_delta=delta,source_interval=old['interval'],
                        previous_state=old['key'][0]))
                if not old or key!=old['key']:
                    self.previous_ai[enemy['glyph']]=dict(key=key,video=self.frames,ticks=ticks,motion_ticks=enemy['last_motion_ticks'],
                        image=image,index=index,interval=interval)


if __name__=='__main__':
    ordinary.Observer=Observer
    raise SystemExit(ordinary.main())
