"""N21P real-input frontend latency and per-presentation CPU observer.

No emulated memory writes. Static targets come from the original-resource N21
oracle; map targets come from the native x86 oracle, not product audit flags.
Latency uses emulated video boundaries, never elapsed host execution time.
"""
from __future__ import annotations

import argparse
from bisect import bisect_left, bisect_right
import hashlib
import json
from pathlib import Path
import struct
import traceback

import run_frontend_emulator as front
import run_map_emulator as maps
from frontend_fat_oracle import audit_changes, volume_files
from map.original_map import original_map_result
from joint_profile_abi import decode_profile

ROOT = front.ROOT
render = front.render


def stable_words(f, address, count=64, sequence_word=2):
    before = render.ram(f, address + sequence_word * 4, 4)
    raw = render.ram(f, address, count * 4)
    after = render.ram(f, address + sequence_word * 4, 4)
    values = struct.unpack('<' + str(count) + 'I', raw)
    return values if before == after == raw[sequence_word*4:sequence_word*4+4] and not values[sequence_word] & 1 else None


def rgb555(rgb):
    result = bytearray(len(rgb) // 3 * 2)
    for pixel in range(len(rgb) // 3):
        at = pixel * 3
        color = (rgb[at] >> 3) | ((rgb[at+1] >> 3) << 5) | ((rgb[at+2] >> 3) << 10)
        struct.pack_into('<H', result, pixel * 2, color)
    return bytes(result)


def indexed555(pixels, palette):
    table = [struct.pack('<H', color & 32767) for color in palette]
    return b''.join(table[index] for index in pixels)


class JointFrontendObserver(front.FrontendObserver):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.map_address, _ = front.transport.elf_symbol(self.target / 'dok_nds_gameplay.elf', 'g_nds_map_probe')
        self.history_fields = render.scalar_fields(ROOT / 'ports/nds/platform/render_probe/render_probe.h', 'NdsRenderHistoryRecord')
        self.history_total = 0
        self.profile_seen = set()
        self.profile_address = None
        self.profile_ring_address = None
        self.profile_ring_total = 0
        self.phase = 'boot'
        self.static_targets = {}
        self.visible_keys = set()
        self.map_target = None
        self.map_key = None
        self.map_visible = None
        self.map_state = None
        self.pending = None
        self.last_target_key = None
        self.report.update(task='N21P', joint_observer_sha256=front.transport.sha(Path(__file__)),
            profile_decoder_sha256=front.transport.sha(ROOT/'ports/nds/tools/joint_profile_abi.py'),
            profile_rows=[], history_records=[], visibility=[], input_latency=[], touch_stages=[],
            map_index8_checks=0, map_rgb_checks=0, boot={},
            measurement=dict(access='Real libretro joypad/pointer; read-only emulator RAM and actual LCD RGB',
                clock='Emulated video boundaries at libretro reported refresh; hardware timer/history for presentations',
                latency='Upper bound: input asserted immediately before retro_run to first end-of-video-frame complete expected image. Sampling period is one video frame. Stable3 N21 checks remain independent.',
                gameplay_endpoint='Gameplay entry/restore endpoints verify the complete lower map LCD, not the 3D scene',
                limitation='Emulator observation only; physical DS IRQ/latency/FPS remain N24',
                profile_subsets='discovery is contained in sprites; detailed map stages are contained in map'))

    def begin_input(self, kind, **details):
        if self.pending is not None:
            self.pending['superseded_at_frame'] = self.frames
        event = dict(kind=kind, input_boundary_frame=self.frames, onset_video_frame=self.frames+1,
            phase=self.phase, source=list(self.probe) if self.probe else None, **details)
        self.report['input_latency'].append(event)
        self.pending = event
        self.last_target_key = self.target_key(self.probe) if self.probe else None
        return event

    @staticmethod
    def target_key(p):
        return (p[3], p[4], p[5], p[6], p[7], p[11], p[24])

    def tap(self, button, hold=1, release=6):
        self.begin_input('joypad', button=button, hold=hold)
        super().tap(button, hold, release)

    def chord(self, buttons, hold=12):
        self.begin_input('joypad_chord', buttons=list(buttons), hold=hold)
        super().chord(buttons, hold)

    def complete_input(self, endpoint, **details):
        event = self.pending
        if event is None or 'visible_frame' in event:
            return
        upper = self.frames - event['input_boundary_frame']
        source = event['source']
        destination = self.probe
        if event['kind'] == 'pointer':
            category = 'touch'
        elif event['kind'] == 'boot_one_video_frame':
            category = 'boot_skip'
        elif source and destination:
            if source[4] != destination[4]: category = 'language_change'
            elif destination[3] == 9 and source[3] == 8: category = 'pause'
            elif source[3] == 9 and destination[3] == 8: category = 'resume'
            elif source[3] == 7 and destination[3] == 8: category = 'level_loading'
            elif source[3] == destination[3] and destination[3] in (6,7,10,11): category = 'pagination'
            elif source[3] == 3 and destination[3] == 7: category = 'intro_loading'
            elif source[3] == destination[3]: category = 'menu_focus'
            else: category = 'screen_transition'
        else:
            category = 'screen_transition'
        event.update(endpoint=endpoint, visible_frame=self.frames, latency_upper_vblanks=upper,
            latency_upper_ms=upper * 1000 / self.av.timing.fps, sampling_period_vblanks=1,
            category=category, destination_state=destination[3] if destination else None, **details)
        self.pending = None

    def tick(self, count=1):
        for _ in range(count):
            super().tick()
            raw = struct.unpack('<64I', render.ram(self, self.address, 256))
            if raw[0] == 0x21504b44:
                self.report['boot'].setdefault('first_magic_frame', self.frames)
                if raw[13]:
                    self.report['boot'].setdefault('first_ready_frame', self.frames)
            self.observe_history_and_profile()
            p = self.probe
            if not p:
                continue
            if p[3] != 8:
                self.observe_static(p)
            else:
                self.observe_map(p)
        return self.probe

    def observe_static(self, p):
        key = self.target_key(p)
        if key not in self.static_targets:
            expected = front.render_frontend(self.art, self.labels, self.controls,
                p[3], p[4], p[5], p[6], p[7], p[31])
            target = indexed555(expected['top'], expected['top_palette']) + indexed555(expected['bottom'], expected['bottom_palette'])
            row = dict(key=list(key), phase=self.phase, publish_frame=self.frames,
                expected_lcd_sha256=hashlib.sha256(target).hexdigest())
            self.static_targets[key] = expected, target, row
            self.report['visibility'].append(row)
            if self.pending and key != self.last_target_key:
                self.pending.setdefault('publish_frame', self.frames)
                self.pending.setdefault('target', list(key))
        expected, target, row = self.static_targets[key]
        if key in self.visible_keys:
            return
        width, height, rgb = self.rgb()
        assert (width, height) == (256, 384)
        if rgb555(rgb) != target:
            return
        for screen, ptr, palette_ptr in (('top', p[20], p[22]), ('bottom', p[21], p[23])):
            assert render.ram(self, ptr, 49152) == expected[screen], 'Source INDEX8 differs at first actual visibility'
            assert struct.unpack('<256H', render.ram(self, palette_ptr, 512)) == tuple(expected[screen+'_palette'])
        row.update(first_whole_lcd_frame=self.frames, publish_to_visible_vblanks=self.frames-row['publish_frame'])
        self.visible_keys.add(key)
        if p[3] == 1:
            self.report['boot'].setdefault('title_whole_lcd_frame', self.frames)
            self.report['boot'].setdefault('title_from_load_upper_ms', self.frames*1000/self.av.timing.fps)
        if self.pending and key != self.last_target_key and (
            self.pending['kind'] != 'boot_one_video_frame' or p[3] == 2
        ):
            self.complete_input('whole_static_lcd_rgb', target=list(key))

    def observe_history_and_profile(self):
        renderer = self.report.get('last_renderer')
        if renderer:
            total = renderer['history_total']
            assert total >= self.history_total and total-self.history_total <= 128, 'Presentation history observations lost'
            for ordinal in range(self.history_total+1, total+1):
                row = render.history_record(self, renderer, ordinal, self.history_fields)
                if row is None:
                    break
                row.update(observer_video_frame=self.frames, phase=self.phase)
                self.report['history_records'].append(row)
                self.history_total = ordinal
        # The fixed scene-tail profile has its own seqlock. Reading it must not
        # depend on whether the independently published map descriptor is odd.
        if self.profile_address is None:
            p = stable_words(self, self.map_address)
            if not p or p[0] != 0x20504b44 or not p[60]:
                return
            self.profile_address = p[60]+64
            self.report['profile_address'] = self.profile_address
        values = stable_words(self, self.profile_address, 48, 1)
        if not values or values[0] != 0x21504b44:
            return
        # Optional bounded ABI1 history extension: product words56..59 name
        # pointer/capacity/stride/total. Slots contain the same48-word snapshot,
        # with sequence at word1; slot=(total-1)%capacity. No RAM writes needed.
        ring, capacity, stride, total = values[40:44]
        if ring:
            assert 1 <= capacity <= 128 and stride == 192, 'Unknown optional profile ring ABI'
            if ring != self.profile_ring_address or total < self.profile_ring_total:
                self.profile_ring_address = ring
                self.profile_ring_total = 0
            assert total-self.profile_ring_total <= capacity, 'CPU profile ring observations lost'
            for ordinal in range(self.profile_ring_total+1,total+1):
                snapshot = stable_words(self,ring+((ordinal-1)%capacity)*stride,48,1)
                if not snapshot or snapshot[0] != 0x21504b44:
                    break
                assert snapshot[4] == ordinal, 'CPU profile ring slot overwritten or index changed'
                self.capture_profile(snapshot,profile_ring_ordinal=ordinal)
                self.profile_ring_total = ordinal
            self.report['profile_ring'] = dict(address=ring,capacity=capacity,stride=stride,total=total,
                observed_total=self.profile_ring_total)
        self.capture_profile(values)

    def capture_profile(self, values, **details):
        row = decode_profile(values)
        assert row['abi'] in (1,2,3) and row['timer_hz'] == 33513982
        if row['frame_id'] in self.profile_seen:
            return
        self.profile_seen.add(row['frame_id'])
        row.update(observer_video_frame=self.frames, phase=self.phase,
            frontend_state=self.probe[3] if self.probe else None, **details)
        self.report['profile_rows'].append(row)
        if len(self.profile_seen) % 100 == 0:
            (self.directory/'profile-progress.json').write_text(json.dumps(dict(
                observer_video_frame=self.frames, profiles=len(self.profile_seen),
                latest_frame=row['frame_id'], phase=self.phase)),encoding='utf8')

    def observe_map(self, frontend):
        p = stable_words(self, self.map_address)
        if not p or p[0] != 0x20504b44 or p[1] not in (1, 2) or p[3] != frontend[6] or not p[28]:
            return
        assert p[31] == 0
        if p[1] == 2:
            live = struct.unpack('<48I', render.ram(self, p[61], 192))
            assert live[44] == 0
            self.map_state = dict(frame=live[0], scale=live[9], follow=live[10], capture=live[13], blocked=live[30],
                center=list(live[11:13]), ai_callbacks=live[18], hero_callbacks=live[20], ticks=live[1], updates=live[41])
        else:
            self.map_state = dict(frame=p[5], scale=p[10], follow=p[11], capture=p[14], blocked=p[54],
                center=list(p[12:14]), ai_callbacks=p[38], hero_callbacks=p[40], ticks=p[19], updates=p[33])
        self.report['last_map_state'] = self.map_state
        key = (p[3], p[4], p[41], p[33], frontend[4])
        if key != self.map_key:
            size = p[8]*p[9]
            snapshot = dict(width=p[8], length=p[9], hero_x=maps.quotient(maps.signed(p[20]),480),
                hero_z=maps.quotient(maps.signed(p[21]),480), heading=p[22], map_available=p[30],
                visited=render.ram(self,p[23],size), maps=[render.ram(self,p[i],size) for i in (25,24,26)],
                glyphs=render.ram(self,p[27],1024))
            pixels, postvisited = original_map_result('ru', snapshot, p[10], maps.signed(p[50]), maps.signed(p[51]))
            assert render.ram(self,p[55],size) == postvisited[:size]
            expected = bytearray(pixels) + bytearray(8192)
            font = self.art.resources[1]['FONT8']
            for button, (left, width, label) in enumerate(((0,85,b'-'), (85,85,b'+'),
                (170,86,'Центр'.encode('cp866') if frontend[4] == 1 else b'Center'))):
                active = p[14] == button+2
                for y in range(160,192):
                    expected[y*256+left:y*256+left+width] = bytes([2 if active else 86])*width
                    expected[y*256+left] = 0
                expected[160*256+left:160*256+left+width] = bytes(width)
                x = left+(width-len(label)*8)//2
                for n, code in enumerate(label):
                    for y in range(8):
                        for col in range(8):
                            if font[code*8+y] & (128>>col):
                                expected[(172+y)*256+x+n*8+col] = 86 if active else 2
            palette = self.art.palette(1, 'GAMEP'+str(p[3]))
            assert struct.unpack('<256H', render.ram(self,p[29],512)) == tuple(palette)
            assert render.ram(self,p[28],49152) == expected, 'Native-x86 map INDEX8 mismatch'
            self.report['map_index8_checks'] += 49152
            self.map_target = indexed555(expected,palette)
            self.map_key = key
        width, height, rgb = self.rgb()
        assert (width, height) == (256, 384)
        if rgb555(rgb[49152*3:]) != self.map_target:
            return
        if key != self.map_visible:
            self.report['map_rgb_checks'] += 49152
            self.map_visible = key
        if self.pending:
            event = self.pending
            if event['kind'] == 'pointer':
                if p[33] <= event['map_updates_before']:
                    return
                touch = event['touch']
                if touch is None:
                    if p[15]: return
                elif not p[15] or tuple(p[16:18]) != tuple(touch):
                    return
                self.complete_input('whole_map_lcd_rgb', map_key=list(key), scale=p[10], capture=p[14],
                    map_publish_frame=p[62] if p[1] == 2 else p[5])
            elif self.last_target_key is None or self.last_target_key[0] != 8:
                self.complete_input('gameplay_whole_map_lcd_rgb', map_key=list(key))

    def advance_presentations(self, count):
        start = len(self.report['profile_rows'])
        for _ in range(max(12000, count*16)):
            self.tick()
            if len(self.report['profile_rows'])-start >= count:
                return
        raise AssertionError(('No sufficient live profile presentations', count, self.probe))

    def pointer_step(self, name, touch, count=12, *, require_visible=True):
        before = dict(self.map_state)
        self.begin_input('pointer', name=name, touch=touch, map_updates_before=before['updates'],
            visible_change_required=require_visible)
        self.touch = touch
        event = self.pending
        self.advance_presentations(count)
        after = dict(self.map_state)
        assert after['ticks'] > before['ticks'] and after['hero_callbacks'] > before['hero_callbacks'], 'World stopped during touch'
        if require_visible:
            assert 'visible_frame' in event, ('Touch produced no original-verified actual LCD update', name)
        self.report['touch_stages'].append(dict(name=name, touch=touch, before=before, after=after, event=event))


def live_flow(f, language, levels, steady):
    f.wait_for(1, language=language)
    f.tap(front.A)
    f.wait_for(2)
    for level in levels:
        f.phase = 'level-loading'
        f.choose(2,0)
        f.tap(front.A)
        f.wait_for(3)
        f.choose(3,level-1)
        f.tap(front.A)
        f.text_to(7,8)
        p = f.wait_for(8)
        f.phase = f'level-{level}-language-{p[4]}-steady'
        f.advance_presentations(steady)
        start = dict(f.map_state)
        f.phase = f'level-{level}-touch'
        f.pointer_step('minimum',(40,175))
        assert f.map_state['scale'] == 2
        f.pointer_step('release minimum',None)
        for scale in (3,4,6,8):
            f.pointer_step('zoom '+str(scale),(125,175))
            assert f.map_state['scale'] == scale
            f.pointer_step('release zoom '+str(scale),None)
        f.pointer_step('drag begin',(100,70),require_visible=False)
        f.pointer_step('drag move',(220,120))
        assert f.map_state['follow'] == 0 and f.map_state['capture'] == 1
        f.phase = 'pause'
        f.tap(front.START,hold=3,release=12)
        p = f.wait_for(9,unblocked=False)
        frozen = p[40:52]
        f.tick(30)
        assert f.probe[40:52] == frozen
        f.touch = None
        f.wait_for(9)
        f.phase = 'paused-language-change'
        f.language_from_pause(3-p[4])
        f.choose(9,0)
        f.phase = 'resume-held-pointer'
        f.touch = (220,120)
        f.tap(front.A)
        f.wait_for(8,unblocked=False)
        f.advance_presentations(12)
        assert f.map_state['capture'] == 0 and f.probe[9] == 1, 'Held stylus was not absorbed at frontend return'
        f.pointer_step('held pointer after resume',(20,20),require_visible=False)
        assert f.map_state['capture'] == 0 and f.probe[9] == 1
        f.pointer_step('release after resume',None,require_visible=False)
        assert f.map_state['blocked'] == 0
        f.pointer_step('fresh touch',(80,60),require_visible=False)
        f.pointer_step('fresh drag',(180,110))
        f.pointer_step('fresh release',None,require_visible=False)
        f.pointer_step('center',(210,175))
        assert f.map_state['follow'] == 1
        f.pointer_step('center release',None)
        f.phase = f'level-{level}-language-{f.probe[4]}-steady'
        f.advance_presentations(steady)
        assert f.map_state['ticks'] > start['ticks'] and f.map_state['hero_callbacks'] > start['hero_callbacks']
        f.report.setdefault('live_levels',[]).append(dict(level=level, start=start, end=dict(f.map_state)))
        f.phase = 'pause-return-menu'
        f.tap(front.START)
        f.wait_for(9)
        f.choose(9,3)
        f.tap(front.A)
        f.wait_for(2)


def summarize(f):
    report = f.report
    profiles = {row['frame_id']:row for row in report['profile_rows']}
    active = []
    groups = []
    group = []
    previous = None
    for row in report['history_records']:
        if not row['flags'] & 8:
            continue
        assert not row['disp3d_flags'] and not row['gxstat_flags'], 'GPU overflow'
        assert row['hardware_polygons'] <= 2048 and row['hardware_vertices'] <= 6144
        profile = profiles.get(row['frame_id'])
        row['profile_observed'] = profile is not None
        if profile:
            row['profile_interval_ticks'] = profile['interval_ticks']
        # Product history alone classifies presented gameplay. An observer
        # phase can lag entry/resume/pause and must never hide a slow normal
        # presentation. Timestamp gaps below separate actual UI reading time.
        steady = not row['flags'] & (1|4|32|64)
        if not steady:
            if group: groups.append(group)
            group = []
            previous = None
            continue
        if previous and previous['vblank_end'] != row['vblank_start']:
            groups.append(group)
            group = []
        if not group: group = [row['vblank_start']]
        group.append(row['vblank_end'])
        active.append(row)
        previous = row
    if group: groups.append(group)
    if active:
        import render_metrics
        probe = report['last_renderer']
        refresh = probe['timer_hz']/probe['vblank_ticks']
        intervals = [(r['vblank_end']-r['vblank_start']) & 0xffffffff for r in active]
        rates, counts = [], []
        for g in groups:
            for index,start in enumerate(g):
                if start+refresh > g[-1]: break
                end = bisect_left(g,start+refresh)
                rates.append((end-index)*refresh/(g[end]-start))
                counts.append(bisect_right(g,start+refresh)-bisect_right(g,start))
        report['steady_metrics'] = render_metrics.distribution(intervals,refresh)
        report['steady_metrics'].update(refresh_hz=refresh, checked_one_second_windows=len(rates),
            scope='Every product PRESENTED interval without LOADING/WARMUP/DIAGNOSTIC/SYNC flags, independent of observer phase; timestamp gaps separate UI reading time',
            minimum_window_cadence_fps=min(rates) if rates else None,
            minimum_new_frames_in_one_second=min(counts) if counts else None,
            budget_passed=bool(rates) and min(rates)+1e-12 >= refresh/2 and sum(x>2 for x in intervals)/len(intervals) <= .01,
            missing_profiles=[r['frame_id'] for r in active if not r['profile_observed']])
    visible = [r for r in report['input_latency'] if 'visible_frame' in r
        and (r['kind'] != 'pointer' or r.get('visible_change_required', True))]
    report['latency_summary_scope'] = 'Actual visible changes only; pointer steps explicitly requiring no visible change remain in raw input events but are excluded from visible latency summaries'
    report['latency_summary'] = {kind:dict(samples=len(rows), max_upper_vblanks=max(r['latency_upper_vblanks'] for r in rows),
        max_upper_ms=max(r['latency_upper_ms'] for r in rows)) for kind in sorted({r['endpoint'] for r in visible})
        if (rows := [r for r in visible if r['endpoint'] == kind])}
    report['transition_latency_summary'] = {kind:dict(samples=len(rows), max_upper_vblanks=max(r['latency_upper_vblanks'] for r in rows),
        max_upper_ms=max(r['latency_upper_ms'] for r in rows)) for kind in sorted({r['category'] for r in visible})
        if (rows := [r for r in visible if r['category'] == kind])}
    report['video_refresh_hz'] = f.av.timing.fps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--core',type=Path,default=front.CORE)
    parser.add_argument('--language',type=int,choices=(1,2),default=1)
    parser.add_argument('--mode',choices=('live','full','early'),default='live')
    parser.add_argument('--levels',type=int,nargs='+',choices=(1,2,3),default=[1,2,3])
    parser.add_argument('--steady-presentations',type=int,default=120)
    parser.add_argument('--phase',type=int,choices=range(8),default=0)
    parser.add_argument('--button',choices=('A','Start'),default='A')
    parser.add_argument('--max-ui-latency-vblanks',type=int)
    parser.add_argument('--max-touch-latency-vblanks',type=int)
    args = parser.parse_args()
    target = args.target.absolute()
    assert target.is_relative_to(ROOT/'build/nds')
    assert not any(p.is_symlink() or p.is_junction() for p in (target,*target.parents) if p.exists())
    assert args.steady_presentations >= 1
    directory = target/'joint-frontend'/ (args.mode if args.mode != 'early' else f'early-{args.button}-{args.phase}')
    directory.mkdir(parents=True,exist_ok=True)
    f = None
    report = dict(task='N21P',passed=False)
    try:
        assert front.transport.sha(args.core) == render.CORE_SHA
        f = JointFrontendObserver(args.core,target,directory)
        report = f.report
        report['initial_language'] = args.language
        report['command_arguments'] = {k:str(v) if isinstance(v,Path) else v for k,v in vars(args).items()}
        image = f.save/'melonDS DS/dldi_sd_card.bin'
        front.write_fixture(image,front.settings_bytes(2) if args.language == 2 else None)
        f.load(target/'dok_nds_gameplay.nds')
        front.transport.flush_native_stdio()
        before = volume_files(image)
        if args.mode == 'full':
            f.phase = 'full-functional-front-end'
            front.full_flow(f,args.language)
        elif args.mode == 'early':
            for _ in range(12000):
                f.tick()
                raw = struct.unpack('<64I',render.ram(f,f.address,256))
                if raw[0] == 0x21504b44 and not raw[13]: break
            else: raise AssertionError('No early IRQ sampling window')
            f.tick(args.phase)
            f.begin_input('boot_one_video_frame',button=args.button,phase_offset=args.phase)
            f.joypad = 1 << (front.A if args.button == 'A' else front.START)
            f.tick()
            f.joypad = 0
            p = f.wait_for(2)
            assert p[14] & (1 if args.button == 'A' else 8)
            f.tick(30)
            assert f.probe[3] == 2 and f.probe[5] == 0
        else:
            live_flow(f,args.language,args.levels,args.steady_presentations)
        summarize(f)
        for event in report['input_latency']:
            bound = args.max_touch_latency_vblanks if event['kind'] == 'pointer' else args.max_ui_latency_vblanks
            if bound is not None and 'latency_upper_vblanks' in event:
                assert event['latency_upper_vblanks'] <= bound, ('Explicit latency bound exceeded',bound,event)
        if args.mode == 'live':
            assert report['profile_rows'] and report['history_records'] and report['map_rgb_checks']
            assert not report['steady_metrics']['missing_profiles'], 'Live profile presentations lost'
        report['passed'] = True
    except Exception:
        report['error'] = traceback.format_exc()
        if f and f.last_video: report['failure_images'] = f.capture('failure')
    finally:
        if f:
            f.close()
            front.transport.flush_native_stdio()
            if 'before' in locals():
                try: report['disk'] = audit_changes(before,volume_files(image),['/DOK/SETTINGS.DAT'])
                except Exception:
                    report['passed'] = False
                    report['disk_error'] = traceback.format_exc()
        (directory/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in report.items() if k in ('passed','error','disk_error','latency_summary','steady_metrics')}))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
