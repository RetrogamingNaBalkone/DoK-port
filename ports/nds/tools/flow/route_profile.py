"""Read-only presentation/stage profiling for ordinary N22 route observers.

The normal ROM already publishes these histories. No emulated memory, input,
clock, simulation, presentation, or engine instrumentation is changed here.
"""
from __future__ import annotations
import bisect
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
import run_joint_frontend_emulator as joint
import run_renderer_emulator as render
import run_diag_emulator as transport


def pin(path):
    path = Path(path)
    raw = path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def signed32(value):
    return value-0x100000000 if value & 0x80000000 else value


def stats(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(samples=len(values), mean=sum(values)/len(values),
                median=ordered[(len(ordered)-1)//2],
                p95=ordered[max(0, math.ceil(len(ordered)*.95)-1)],
                p99=ordered[max(0, math.ceil(len(ordered)*.99)-1)], maximum=ordered[-1])


def worst_windows(active, profiles, refresh, stages, limit=10):
    """Locate disjoint slow sections using the acceptance cadence definition.

    Endpoints enclose at least one second; actual UI gaps split groups. Windows
    overlap while enumerating, then the slowest disjoint sections are retained.
    """
    groups = []
    for row in active:
        if not groups or groups[-1][-1]['vblank_end'] != row['vblank_start']:
            groups.append([])
        groups[-1].append(row)
    windows = []
    for group_id, rows in enumerate(groups):
        stamps = [rows[0]['vblank_start']] + [row['vblank_end'] for row in rows]
        for start, stamp in enumerate(stamps):
            if stamp+refresh > stamps[-1]:
                break
            end = bisect.bisect_left(stamps, stamp+refresh)
            windows.append(((end-start)*refresh/(stamps[end]-stamp), group_id, start, end))
    chosen = []
    result = []
    for rate, group_id, start, end in sorted(windows):
        if any(g == group_id and start < e and s < end for g, s, e in chosen):
            continue
        chosen.append((group_id, start, end))
        rows = groups[group_id][start:end]
        cpu = [profiles[row['frame_id']] for row in rows if row['frame_id'] in profiles]
        histogram = {}
        for row in rows:
            interval = (row['vblank_end']-row['vblank_start']) & 0xffffffff
            histogram[str(interval)] = histogram.get(str(interval), 0)+1
        result.append(dict(cadence_fps=rate, presentations=len(rows),
            first_frame_id=rows[0]['frame_id'], last_frame_id=rows[-1]['frame_id'],
            vblank_start=rows[0]['vblank_start'], vblank_end=rows[-1]['vblank_end'],
            duration_ms=(rows[-1]['vblank_end']-rows[0]['vblank_start'])*1000/refresh,
            first_camera=[signed32(rows[0][name]) for name in ('camera_x', 'camera_y', 'camera_z')],
            last_camera=[signed32(rows[-1][name]) for name in ('camera_x', 'camera_y', 'camera_z')],
            interval_histogram=histogram, total_upload_bytes=sum(row['upload_bytes'] for row in rows),
            max_upload_bytes=max(row['upload_bytes'] for row in rows), profile_coverage=len(cpu),
            stages_ms={name: stats([p[name]*1000/p['timer_hz'] for p in cpu]) for name in stages}))
        if len(result) >= limit:
            break
    return result


def summarize(observer):
    # Reuse the accepted presentation metrics, including real timestamp gaps
    # for menus and cold-load flags. Observer phases never exclude a slow frame.
    joint.summarize(observer)
    report = observer.report
    profiles = {row['frame_id']: row for row in report['profile_rows']}
    active = [row for row in report['history_records'] if row['flags'] & 8 and not row['flags'] & (1|4|32|64)]
    stages = ('cpu_excluding_wait', 'entry_audio', 'input', 'simulation', 'geometry', 'sprites',
              'discovery', 'overlay', 'effects', 'submit_including_wait', 'wait_vblank',
              'stream_commit', 'hud', 'map', 'post_map_audio', 'telemetry',
              'map_snapshot', 'map_raster', 'map_postvisited', 'map_buttons', 'map_dma', 'map_publish')
    selected = [profiles[row['frame_id']] for row in active if row['frame_id'] in profiles]
    stage_ms = {name: stats([row[name]*1000/row['timer_hz'] for row in selected]) for name in stages}
    worst = []
    for row in sorted(active, key=lambda r: r['total_ticks'], reverse=True)[:20]:
        profile = profiles.get(row['frame_id'])
        worst.append(dict(frame_id=row['frame_id'], video_frame=row['observer_video_frame'], level=row['level'],
                          camera=[signed32(row[name]) for name in ('camera_x','camera_y','camera_z')],
                          sin_q15=signed32(row['sin_q15']), cos_q15=signed32(row['cos_q15']),
                          interval_vblanks=(row['vblank_end']-row['vblank_start']) & 0xffffffff,
                          interval_ms=row['total_ticks']*1000/report['last_renderer']['timer_hz'],
                          uploaded_bytes=row['upload_bytes'], texture_bytes=row['texture_bytes'],
                          quads=row['submitted_quads'], vertices=row['submitted_vertices'],
                          stages_ms={name:profile[name]*1000/profile['timer_hz'] for name in stages} if profile else None))
    refresh = report['last_renderer']['timer_hz']/report['last_renderer']['vblank_ticks']
    resource_frames = {row['frame_id'] for row in report.get('resource_rows', [])}
    return dict(metrics=report.get('steady_metrics'), stages_ms=stage_ms, worst_presentations=worst,
                worst_disjoint_one_second_windows=worst_windows(active, profiles, refresh, stages),
                resource_records=len(resource_frames),
                resource_active_coverage=sum(row['frame_id'] in resource_frames for row in active),
                resource_scope='Optional read-only published probes, added after initial prefixes. Sprite CPU cache fields precede overlay admission; frame-store cumulative counters include overlay. CPU read bytes are not disk I/O in the resident frame-store path. GPU hit counters omitted because they lag one frame.',
                history_records=len(report['history_records']), profile_records=len(report['profile_rows']),
                active_presentations=len(active), profile_coverage=len(selected),
                omitted_loading=sum(bool(r['flags'] & 1) for r in report['history_records']),
                scope='Every normal product PRESENTED interval; loading/warmup/diagnostic flags and actual UI timestamp gaps separate. Emulator timing only.',
                stage_caveats='Discovery is a subset of sprites; detailed map stages are subsets of map. cpu_excluding_wait covers render_one, while actual presentation intervals also include frontend/flow/probe work. GPU *_hit counters are lagged; history upload bytes belong to the current frame.')


class RouteProfile:
    def __init__(self, observer):
        self.observer = observer
        observer.history_fields = render.scalar_fields(ROOT / 'ports/nds/platform/render_probe/render_probe.h', 'NdsRenderHistoryRecord')
        observer.history_total = 0
        observer.profile_seen = set()
        observer.profile_address = observer.profile_ring_address = None
        observer.profile_ring_total = 0
        observer.phase = 'boot'
        observer.capture_profile = joint.JointFrontendObserver.capture_profile.__get__(observer)
        observer.report.update(profile_rows=[], history_records=[], input_latency=[])
        observer.report['resource_rows'] = []
        self.resource_seen = set()
        self.sprite_address, _ = transport.elf_symbol(observer.target/'dok_nds_gameplay.elf', 'g_nds_sprite_probe')
        self.sprite_fields = render.scalar_fields(ROOT/'ports/nds/platform/sprites_probe/sprites_probe.h', 'NdsSpriteProbe')
        self.last_progress = 0
        self.inputs = []
        for source in (Path(__file__), ROOT/'ports/nds/tools/run_joint_frontend_emulator.py',
                       ROOT/'ports/nds/tools/joint_profile_abi.py', ROOT/'ports/nds/tools/run_joint_profile_emulator.py',
                       ROOT/'ports/nds/tools/render_metrics.py',
                       ROOT/'ports/nds/tools/run_renderer_emulator.py',
                       ROOT/'ports/nds/tools/run_diag_emulator.py',
                       ROOT/'ports/nds/platform/render_probe/render_probe.h',
                       ROOT/'ports/nds/platform/sprites_probe/sprites_probe.h'):
            original = pin(source)
            snapshot = observer.directory/'sources'/(original['sha256']+'-'+source.name)
            snapshot.parent.mkdir(parents=True, exist_ok=True)
            if snapshot.exists():
                assert snapshot.read_bytes() == source.read_bytes()
            else:
                shutil.copyfile(source, snapshot)
            self.inputs.append(dict(source=original, snapshot=pin(snapshot)))
        observer.report['profile_observer_inputs'] = self.inputs

    def resources(self):
        observer = self.observer
        if not observer.probe or observer.probe[3] != 8 or not observer.game or not observer.report['profile_rows']:
            return
        frame = observer.renderer['frame_id']
        if frame in self.resource_seen or observer.game[3] != frame or observer.report['profile_rows'][-1]['frame_id'] != frame:
            return
        # publish() holds the render seqlock across sprite publication. The
        # later stage-profile publication proves gameplay's full probe flush
        # has finished too. Emulation is stopped during these read-only reads.
        sprite = render.observe(observer, self.sprite_address, self.sprite_fields, 0x12504b44)
        renderer = render.observe(observer, observer.render_address, observer.render_fields, 0x37524b44)
        if not sprite or not renderer or renderer['sequence'] != observer.renderer['sequence']:
            return
        g, r = observer.game, observer.renderer
        observer.report['resource_rows'].append(dict(frame_id=frame, video_frame=observer.frames,
            sprite_cpu_hits=sprite['cpu_hits'], sprite_cpu_misses=sprite['cpu_misses'],
            sprite_cpu_evictions=sprite['cpu_evictions'], sprite_cpu_read_bytes=sprite['cpu_reads'],
            raw_cache_used=sprite['cache_bytes'], raw_cache_capacity=g[39],
            frame_store_bytes=g[38], frame_decodes_cumulative=g[40], decoded_bytes_cumulative=g[41],
            sprite_count=sprite['sprite_count'], sprite_considered=sprite['considered'],
            ai_active=g[5], ai_callbacks_cumulative=g[7], combat_callbacks_cumulative=g[8],
            hero_callbacks_cumulative=g[9], scheduled_events=g[30],
            world_serial=observer.probe[44], world_epoch=observer.probe[45],
            weapon_view=dict(visible=g[42], image=g[43], left=render.signed32(g[44]),
                top=render.signed32(g[45]), shift=render.signed32(g[46]),
                state=render.signed32(g[47]), cursor=render.signed32(g[48])),
            effect_pool_capacity=g[62], effect_pool_used=g[63],
            assets_high_water=r['assets_high_water'], scene_high_water=r['scene_high_water'],
            world_high_water=r['world_high_water'], scratch_high_water=r['scratch_high_water']))
        self.resource_seen.add(frame)

    def observe(self):
        observer = self.observer
        if not observer.renderer:
            return
        observer.report['last_renderer'] = observer.renderer
        observer.phase = 'play' if observer.probe and observer.probe[3] == 8 else 'frontend'
        joint.JointFrontendObserver.observe_history_and_profile(observer)
        self.resources()
        if len(observer.profile_seen) >= self.last_progress + 120:
            self.last_progress = len(observer.profile_seen)
            progress = summarize(observer)
            progress.update(video_frame=observer.frames, level=observer.probe[6] if observer.probe else None,
                            rom=observer.report['rom'], observer_inputs=self.inputs)
            path = observer.directory/'performance-progress.json'
            temporary = path.with_suffix('.tmp')
            temporary.write_text(json.dumps(progress, indent=2)+'\n','utf8')
            temporary.replace(path)

    def finish(self):
        observer = self.observer
        if not observer.report['history_records']:
            return
        self.observe()
        summary = summarize(observer)
        data = dict(task=self.observer.report.get('task','N22'), passed=bool(summary['active_presentations']) and summary['active_presentations']==summary['profile_coverage'],
                    completed_playthrough=bool(observer.report.get('completed_playthrough')),
                    rom=observer.report['rom'], elf=observer.report['elf'], observer_inputs=self.inputs,
                    summary=summary, history_records=observer.report['history_records'], profile_rows=observer.report['profile_rows'],
                    resource_rows=observer.report['resource_rows'])
        path = observer.directory/'runtime-profile.json'
        path.write_text(json.dumps(data, indent=2)+'\n','utf8')
        observer.report['performance'] = summary
        observer.report['performance_report'] = pin(path)
        # Keep one authoritative raw profile instead of duplicating all stage
        # records in the already-large navigation/input report.
        observer.report.pop('history_records')
        observer.report.pop('profile_rows')
        observer.report.pop('resource_rows')


def install_route_profiling(cls):
    if getattr(cls, '_route_profiling_installed', False):
        return
    original_init, original_tick, original_close = cls.__init__, cls.tick, cls.close

    def initialize(self, *args, **kwargs):
        original_init(self, *args, **kwargs)
        self._route_profiler = RouteProfile(self)

    def tick(self, count=1):
        result = None
        for _ in range(count):
            result = original_tick(self, 1)
            self._route_profiler.observe()
        return result

    def close(self):
        try:
            self._route_profiler.finish()
        finally:
            original_close(self)

    cls.__init__, cls.tick, cls.close = initialize, tick, close
    cls._route_profiling_installed = True
