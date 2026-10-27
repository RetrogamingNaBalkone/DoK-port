"""G20 normal-cadence integrated CPU/audio/AI/projectile measurements.

Original maps are kept. A separate artificial pool-capacity scene freezes game
time to sustain the maximum projectile instances; it is not ordinary gameplay.
"""
import argparse
import json
import math
import os
from pathlib import Path
import statistics
import struct

from verify_ai_combat_gba import AICombatProbe
from verify_audio_gba import AudioProbe, sounds, irq_stack
from verify_renderer_gba import BUDGET, stack_usage
from verify_sprites_gba import load_json
from verify_gba import sha


class GameProbe(AICombatProbe, AudioProbe):
    pass


def freeze(probe, enabled):
    for symbol in ('g_motion_probe', 'g_visual_probe', 'g_combat_probe', 'g_ai_probe'):
        probe.write(probe.symbols[symbol], struct.pack('<I', int(enabled)))


def collect(probe, count, buttons=()):
    probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 0))
    previous = probe.diag()
    result = []
    for _ in range(count):
        d = probe.until(lambda d: d['draws'] > previous['draws'], buttons=buttons)
        assert d['draws'] == previous['draws']+1
        delta = (d['present_vblank']-previous['present_vblank']) & 0xffffffff
        row = dict(cycles=d['last_cycles'], vblanks=delta,
                   ai_steps=probe.words('g_ai_diag', 3)[2],
                   motion_steps=probe.words('g_motion_diag', 3)[2],
                   motion_debt=probe.words('g_motion_diag', 4)[3],
                   ticks=probe.words('g_time_diag', 3)[2],
                   ai_owners=probe.words('g_ai_diag', 9)[8],
                   projectiles=probe.words('g_combat_diag', 3)[2],
                   sound_active=probe.audio()['active'])
        if 'g_render_completed_stages' in probe.symbols:
            t = probe.words('g_render_completed_stages', 7)
            row['stages'] = dict(zip(('logic','resources','geometry','sort','raster','ui','copy'),
                                    [t[0]]+[b-a for a,b in zip(t,t[1:])]))
        if 'g_hw_bg_diag' in probe.symbols:
            row['hardware_bg']=list(probe.words('g_hw_bg_diag',8))
        if 'g_hw_present_diag' in probe.symbols:row['hardware_present']=list(probe.words('g_hw_present_diag',4))
        if 'g_hw_obj_diag' in probe.symbols:row['hardware_obj']=list(probe.words('g_hw_obj_diag',6))
        if 'g_flow_diag' in probe.symbols:row['flow_state']=probe.words('g_flow_diag',2)[1]
        if 'g_ui_diag' in probe.symbols:row['ui_state']=probe.words('g_ui_diag',2)[1]
        if 'g_hw_completed_logic' in probe.symbols:
            keys=('ticks','motion_steps','motion_debt','ai_steps','ai_owners','projectiles','flow_state','ui_state')
            row.update(zip(keys,probe.words('g_hw_completed_logic',8)))
        assert probe.audio()['underruns'] == 0, probe.audio()
        assert probe.words('g_ai_diag', 2)[1] == 0
        assert probe.words('g_combat_diag', 2)[1] == 0
        assert probe.words('g_motion_diag', 2)[1] == 0
        result.append(row)
        previous = d
    # Phase-independent sampling cannot alias the short presentation window.
    # The capture-only hold stays disabled throughout every timed interval.
    cycles = [r['cycles'] for r in result]
    return dict(minimum=min(cycles), median=statistics.median(cycles), maximum=max(cycles),
                presented_fps=16777216/(280896*statistics.mean(r['vblanks'] for r in result)),
                max_projectiles=max(r['projectiles'] for r in result),
                max_ai_owners=max(r['ai_owners'] for r in result),
                motion_steps=result[-1]['motion_steps']-result[0]['motion_steps'],
                max_motion_debt=max(r['motion_debt'] for r in result),
                time_conservation_error_ticks=(result[-1]['motion_steps']-result[0]['motion_steps'])*416+
                    result[-1]['motion_debt']-result[0]['motion_debt']-
                    ((result[-1]['ticks']-result[0]['ticks'])&0xffffffff),
                ai_steps=result[-1]['ai_steps']-result[0]['ai_steps'],
                game_ticks=(result[-1]['ticks']-result[0]['ticks'])&0xffffffff,
                sound_active_frames=sum(r['sound_active'] for r in result),
                audio=probe.audio(), samples=result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, default=Path('build/g20'))
    p.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA'])/'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    p.add_argument('--report', type=Path, default=Path('analysis/g20-game-profile.json'))
    p.add_argument('--samples', type=int, default=30)
    p.add_argument('--stress-samples', type=int, default=6)
    a = p.parse_args()
    assert a.samples >= 6 and a.stress_samples >= 6
    probe = GameProbe(a.core, a.build_dir)
    rows = []
    try:
        probe.until(lambda d:d['draws']>=1)
        probe.write(probe.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        probe.until(lambda d: d['draws'] >= 4 and d['phase'] == 2)
        freeze(probe, True)
        rate = max(sounds()[0], key=lambda r: r['rate'])
        for language in (0,1):
            for level in range(3):
                probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                freeze(probe, True)
                probe.language(language)
                model = load_json(f'build/runtime/ru/LEVEL{level+1}-runtime.json')
                hero = model['objects'][model['player_index']-1]['actor']
                # Real start camera, fresh original maps and shared release balance.
                probe.camera((level+1)%3, 15120, -300, 22800, 90)
                probe.camera(level, hero['x'], hero['y'], hero['z'], hero['heading'])
                probe.command(6)
                probe.command(2, rate['id'])
                ordinary = collect(probe, 6)
                ordinary.update(level=level+1, language=language, scene='startup-render-pcm', simulation_frozen=True)
                rows.append(ordinary)
                freeze(probe, False)
                natural=collect(probe,a.samples,buttons=(8,7))
                assert natural['motion_steps']>0 and probe.words('g_flow_diag',2)[1]==2
                tolerance=416+math.ceil(natural['maximum']*10000/16777216)
                natural.update(level=level+1,language=language,scene='natural-live-pcm',simulation_frozen=False,
                               keeps_up_with_real_time=natural['max_motion_debt']<416,
                               final_motion_debt=natural['samples'][-1]['motion_debt'])
                rows.append(natural)
                probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                freeze(probe,True)
                probe.present()
                probe.camera((level+1)%3,15120,-300,22800,90)
                probe.camera(level,hero['x'],hero['y'],hero['z'],hero['heading'])
                probe.command(6)
                probe.command(2,rate['id'])
                # Sustain the native free-slot maximum without changing limits.
                full = probe.control(8)
                assert full['result'] > 0 and full['active'] == full['result'], full
                capacity = collect(probe, 6)
                assert capacity['max_projectiles'] == full['result']
                capacity.update(level=level+1, language=language, scene='pool-capacity-render-pcm', simulation_frozen=True,
                                artificial_stress=True, projectile_capacity=full['result'])
                rows.append(capacity)
                probe.write(probe.symbols['g_camera_capture_hold'], struct.pack('<I', 1))
                probe.present()
                probe.control(6)
                for index, obj in enumerate(model['objects']):
                    if obj['class'] == 9 and obj['placements']:
                        probe.ai(2, index)
                # HP is test-only to keep this benchmark out of Game Over.
                player = probe.player()
                player[0] = player[2] = 32767
                probe.set_player(player)
                probe.control(1, 1, 1000)
                probe.control(8)
                freeze(probe, False)
                live = collect(probe, a.stress_samples, buttons=(8,7))
                assert live['motion_steps'] > 0 and live['ai_steps'] > 0, live
                assert live['max_ai_owners'] > 0, live
                # Account for sub-step remainder and at most one observation
                # frame of phase skew; separate time regression checks exact debt.
                tolerance = 416+math.ceil(live['maximum']*10000/16777216)
                live['keeps_up_with_real_time']=live['max_motion_debt']<416
                live['final_motion_debt']=live['samples'][-1]['motion_debt']
                if 'g_ai_work' in probe.symbols:live['ai_work']=probe.words('g_ai_work',4)
                live.update(level=level+1, language=language, scene='all-awake-live-volley-pcm', simulation_frozen=False,
                            artificial_all_enemies_awake=True, test_health=32767,
                            movement_time_tolerance_ticks=tolerance)
                rows.append(live)
                print(f'G20 game L{level+1} language={language}: live {live["minimum"]}..{live["maximum"]} cycles, {live["presented_fps"]:.2f} FPS', flush=True)
                probe.pcm.clear()
        stack = stack_usage(probe.frontend)
        irq = irq_stack(probe)
        assert stack['untouched_margin_bytes'] >= 256 and irq['untouched_margin_bytes'] >= 16
        result = dict(task='G20', status='passed', script_sha256=sha(__file__), rom_sha256=sha(probe.rom),
                      core_sha256=sha(a.core), bios='mGBA replacement BIOS', physical_gba_tested=False,
                      budget_cycles=BUDGET, target_met=all(r['maximum']<=BUDGET for r in rows),
                      rows=rows, stack=stack, irq_stack=irq, audio_underruns=probe.audio()['underruns'],
                      capture_hold_during_samples=False,
                      limitations=['Artificial all-awake and capacity fixtures are distinguished from normal gameplay.',
                                   'CPU timer excludes presentation wait and diagnostic publication; presentation FPS includes them.',
                                   'Controlled stationary camera; live D-pad turning, fire, callbacks and real clock run normally.'])
        a.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
