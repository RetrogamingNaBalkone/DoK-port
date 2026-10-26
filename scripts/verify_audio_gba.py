"""G16 capture real mGBA FIFO audio while the production renderer runs.

Source samples come independently from original RES. Libretro's resampled and
filtered stereo output is not expected to equal Sound Blaster PCM bytewise.
"""
import argparse
from array import array
import ctypes as ct
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import wave

from verify_gba import AUDIO, BATCH, VIDEO, sha
from verify_combat_gba import CombatProbe, setup_open
from probe_render_layout import frames
from verify_sprites_gba import Probe, load_json, original_animations
from verify_renderer_gba import stack_usage


class Geometry(ct.Structure):
    _fields_ = [('width', ct.c_uint), ('height', ct.c_uint),
                ('max_width', ct.c_uint), ('max_height', ct.c_uint), ('aspect', ct.c_float)]


class Timing(ct.Structure):
    _fields_ = [('fps', ct.c_double), ('sample_rate', ct.c_double)]


class AV(ct.Structure):
    _fields_ = [('geometry', Geometry), ('timing', Timing)]


class AudioProbe(CombatProbe):
    def __init__(self, core, build):
        super().__init__(core, build)
        self.pcm = []
        self.batch_callback = BATCH(self.batch)
        self.single_callback = AUDIO(lambda left, right: self.pcm.append(struct.pack('<hh', left, right)))
        self.video_callback = VIDEO(self.video)
        self.frontend.core.retro_set_audio_sample_batch(self.batch_callback)
        self.frontend.core.retro_set_audio_sample(self.single_callback)
        self.frontend.core.retro_set_video_refresh(self.video_callback)
        self.frontend.core.retro_get_system_av_info.argtypes = [ct.POINTER(AV)]
        av = AV()
        self.frontend.core.retro_get_system_av_info(ct.byref(av))
        self.sample_rate, self.fps = av.timing.sample_rate, av.timing.fps
        assert 8000 <= self.sample_rate <= 192000 and 50 < self.fps < 65

    def batch(self, data, count):
        self.pcm.append(ct.string_at(data, count * 4))
        return count

    def video(self, data, width, height, pitch):
        self.frontend.width, self.frontend.height = width, height
        self.frontend.video_frames += 1

    def audio(self):
        names = ('magic active resource rate period position loops starts rejected stops '
                 'completed irqs underruns max_gap refill_cycles ram_bytes level_resets requests identity').split()
        result = dict(zip(names, self.words('g_audio_diag', len(names))))
        assert result['magic'] == 0x41554431, result
        return result

    def command(self, op, resource=0):
        address = self.symbols['g_audio_probe']
        request = self.words('g_audio_probe', 2)[0] + 1
        self.write(address + 8, struct.pack('<2I', op, resource))
        self.write(address, struct.pack('<I', request))
        self.until(lambda d: self.words('g_audio_probe', 2)[1] == request)
        return self.words('g_audio_probe', 5)[4], self.audio()

    def combat_fast(self, op, a=0, b=0, c=0, tick=0, count=1, slot=0):
        address = self.symbols['g_combat_probe']
        request = self.words('g_combat_probe', 3)[1] + 1
        self.write(address + 12, struct.pack('<7I', op, a, b, c, tick, count, slot))
        self.write(address + 4, struct.pack('<I', request))
        self.until(lambda d: self.words('g_combat_probe', 3)[2] == request)
        return self.combat_diag()

    def advance(self, frames):
        for _ in range(frames):
            self.frontend.run(1)
        basic = self.frontend.diag(self.symbols['g_diag'])
        assert basic['fault'] == 0, basic

    def recording(self, name):
        raw = b''.join(self.pcm)
        self.pcm.clear()
        values = array('h', raw)
        path = self.build / 'audio-check' / (name + '.wav')
        path.parent.mkdir(exist_ok=True)
        with wave.open(str(path), 'wb') as output:
            output.setnchannels(2)
            output.setsampwidth(2)
            output.setframerate(round(self.sample_rate))
            output.writeframes(raw)
        return dict(path=path.as_posix(), sha256=sha(path), frames=len(raw) // 4,
                    seconds=len(raw) / 4 / self.sample_rate,
                    peak=max(map(abs, values), default=0),
                    rms=math.sqrt(sum(n * n for n in values) / max(1, len(values))),
                    channel_difference_samples=sum(l != r for l, r in zip(values[::2], values[1::2])))


def sounds():
    resources, source = original_animations(0)
    ids = {row['name_hex']: row['id'] for row in load_json('build/gba/assets/manifest.json')['resources']}
    result = []
    for name, raw in resources.items():
        if raw[2:5] == b'SVM':
            rate, count = struct.unpack_from('<II', raw, 16)
            result.append(dict(id=ids[name], name=name, rate=rate, count=count,
                               seconds=count / rate, samples=raw[36:]))
    assert len(result) == 23
    return sorted(result, key=lambda row: row['seconds']), source


def pcm_correlation(row, recording, period):
    """Independent gain/DC invariant match; allow core resampling/filter delay."""
    with wave.open(recording['path'], 'rb') as stream:
        fs = stream.getframerate()
        actual = array('h', stream.readframes(stream.getnframes()))[::2]
    source = row['samples']
    step = max(1, row['rate'] // 400)
    indices = list(range(0, min(len(source), row['rate'] // 3), step))
    x = [source[i] - 128 for i in indices]
    mean = sum(x) / len(x)
    x = [value - mean for value in x]
    variance = sum(value * value for value in x)
    assert variance > 1, row['name']
    positions = [round(i * period * fs / 16777216) for i in indices]
    limit = min(round(fs * 0.6), len(actual) - positions[-1] - 1)
    assert limit >= 0, (row['id'], recording)

    def coefficient(lag):
        y = [actual[lag + pos] for pos in positions]
        sy, sy2 = sum(y), sum(value * value for value in y)
        vy = sy2 - sy * sy / len(y)
        if vy <= 0:
            return -1
        return sum(a * b for a, b in zip(x, y)) / math.sqrt(variance * vy)

    candidates = [(coefficient(lag), lag) for lag in range(0, limit + 1, 2)]
    best, lag = max(candidates)
    best, lag = max((coefficient(offset), offset)
                    for offset in range(max(0, lag - 4), min(limit, lag + 4) + 1))
    all_indices = range(0, len(source), max(1, len(source) // 8000))
    pairs = [(source[i] - 128, actual[lag + round(i * period * fs / 16777216)])
             for i in all_indices if lag + round(i * period * fs / 16777216) < len(actual)]
    n = len(pairs)
    sx, sy = sum(x for x, y in pairs), sum(y for x, y in pairs)
    covariance = sum(x*y for x, y in pairs) - sx*sy/n
    vx = sum(x*x for x, y in pairs) - sx*sx/n
    vy = sum(y*y for x, y in pairs) - sy*sy/n
    whole = covariance / math.sqrt(vx*vy)
    return dict(coefficient=best, delay_output_frames=lag, delay_seconds=lag / fs,
                compared_source_samples=len(x), clock_period=period,
                whole_recording_coefficient=whole, whole_source_samples=n)


def irq_stack(probe):
    words = struct.unpack('<40I', probe.frontend.read(0x03007f00, 160))
    margin = next((i for i, value in enumerate(words) if value != 0x0d15ea5e), len(words)) * 4
    return dict(low=0x03007f00, top=0x03007fa0, untouched_margin_bytes=margin,
                high_water_bytes=160-margin, includes_bios_irq_saves=True)


def identity_cases(probe):
    probe.command(6)
    model = load_json('build/runtime/ru/LEVEL1-runtime.json')
    first, second = (model['glyph_index'][glyph] - 1 for glyph in (0x80, 0x81))
    assert model['objects'][first]['sound'] == model['objects'][second]['sound']
    first = (first << 16) | 0xfff2
    second = (second << 16) | 0xfff2
    ok, active = probe.command(8, first)
    assert ok and active['identity'] == first, active
    query, _ = probe.command(10, second)
    assert not query
    stopped, current = probe.command(9, second)
    assert not stopped and current['active'] and current['identity'] == first, current
    rejected, current = probe.command(7, second)
    assert not rejected and current['identity'] == first, current
    query, _ = probe.command(10, first)
    assert query
    stopped, current = probe.command(9, first)
    assert stopped and not current['active'], current
    return dict(same_pcm_distinct_sound_objects=True, other_owner_query_false=True,
                other_owner_stop_preserved_current=True, other_owner_start_rejected=True,
                correct_owner_stop=True)


def projectile_identity(probe):
    probe.command(6)
    model = load_json('build/runtime/ru/LEVEL1-runtime.json')
    setup_open(probe, model)
    wall = next(obj['glyph'] for obj in model['objects']
                if obj['class'] == 2 and not obj['flags'] & 0x18)
    probe.write(probe.symbols['g_render_world'] + 17472 + 4096 + 31 * 64 + 36, bytes([wall]))
    for _ in range(2):
        spawned = probe.combat_fast(5, model['player_index'] - 1, ord('1'))
        assert spawned['result'] == 14, spawned
    # Prepare the older clone's last effect frame. In a single real poll the
    # newer clone impacts, starts its sound, then the older clone completes.
    # This keeps the stop test within one callback sweep despite a slow draw.
    resources, _ = original_animations(0)
    resource = next(row['id'] for row in load_json('build/gba/assets/manifest.json')['resources']
                    if row['name_hex'] == b'MIMO'.hex())
    last = len(frames(resources[b'MIMO'.hex()])) - 1
    slot = probe.symbols['g_combat'] + 12
    probe.write(slot + 28, struct.pack('<HH', resource, last))
    probe.write(slot + 36, b'\x03')
    before = probe.audio()
    probe.combat_fast(4, 416, tick=10416)
    active = probe.audio()
    assert active['active'] and active['identity'] & 0xc0000000 == 0x40000000, active
    owner = (active['identity'] & 0x3fffffff) - 1
    slots = probe.active_slots()
    assert len(slots) == 1 and slots[0][0] == owner and slots[0][1]['state'] == 2, slots
    assert not probe.slots()[0]['state'] and owner == 1
    assert active['stops'] == before['stops'], (before, active)
    probe.command(6)
    probe.control(6)
    return dict(actual_projectile_impact=True, prepared_other_effect_completion=True,
                same_pcm_distinct_clone_identities=True,
                removal_of_other_clone_preserved_voice=True, playing_slot=owner,
                removed_slot=0)


def full_sounds(probe, rows):
    results = []
    for row in rows:
        probe.command(6)
        probe.pcm.clear()
        before = probe.audio()
        draws = probe.diag()['draws']
        accepted, active = probe.command(1, row['id'])
        assert accepted and active['active'], active
        first_frame = probe.frontend.video_frames
        probe.until(lambda d: not probe.audio()['active'])
        elapsed = (probe.frontend.video_frames - first_frame) / probe.fps
        expected = row['count'] * active['period'] / 16777216
        assert abs(elapsed - expected) < 0.05, (row['id'], elapsed, expected)
        final = probe.audio()
        assert final['completed'] == before['completed'] + 1, final
        assert final['underruns'] == before['underruns'], final
        record = probe.recording(f'sound-{row["id"]:03d}')
        match = pcm_correlation(row, record, active['period'])
        assert match['coefficient'] > 0.95 and match['whole_recording_coefficient'] > 0.95, (row['id'], match)
        assert not record['channel_difference_samples'] and record['peak'] > 0, record
        results.append(dict(id=row['id'], name_hex=row['name'], nominal_rate=row['rate'],
                            source_samples=row['count'], expected_hardware_seconds=expected,
                            observed_seconds=elapsed, renderer_frames=probe.diag()['draws']-draws,
                            pcm=record, correlation=match))
        print(f'ARM G16 sound{row["id"]} {row["count"]}samples passed', flush=True)
    return results


def integrated_cost(probe, short, long):
    def timings():
        values = []
        for _ in range(12):
            values.append(probe.present()['last_cycles'])
        raw = probe.frontend.read(probe.symbols['g_render_frame'] + 4, 38400)
        return dict(minimum=min(values), maximum=max(values), mean=sum(values)/len(values), values=values,
                    framebuffer_sha256=hashlib.sha256(raw).hexdigest())
    probe.command(6)
    quiet = timings()
    probe.command(2, short['id'])
    full_rate = timings()
    probe.command(6)
    probe.command(2, long['id'])
    half_rate = timings()
    probe.command(6)
    assert quiet['framebuffer_sha256'] == full_rate['framebuffer_sha256'] == half_rate['framebuffer_sha256']
    return dict(silent=quiet, active22050=full_rate, active11025=half_rate,
                overhead22050=full_rate['mean']/quiet['mean']-1,
                overhead11025=half_rate['mean']/quiet['mean']-1,
                timer_quantum_cycles=64, scope='Whole renderer elapsed cycles including audio IRQs and DMA')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/g16'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g16-audio-gba.json'))
    parser.add_argument('--quick', action='store_true')
    args = parser.parse_args()
    probe = AudioProbe(args.core, args.build_dir)
    try:
        probe.until(lambda d: d['draws'] >= 3 and d['phase'] == 2)
        for symbol in ('g_motion_probe', 'g_visual_probe', 'g_combat_probe', 'g_ai_probe'):
            probe.write(probe.symbols[symbol], struct.pack('<I', 1))
        rows, source = sounds()
        probe.command(5)
        probe.pcm.clear()
        short, long = rows[0], rows[-1]
        start_draw = probe.diag()['draws']
        accepted, started = probe.command(1, short['id'])
        assert accepted and started['rate'] == short['rate']
        probe.until(lambda d: not probe.audio()['active'])
        once = probe.recording('short-once')
        assert once['peak'] > 0 and once['rms'] > 0, once
        probe.command(2, short['id'])
        probe.advance(math.ceil((short['seconds'] * 3 + 0.5) * probe.fps))
        looping = probe.audio()
        assert looping['active'] and looping['loops'] >= 3, looping
        loop_audio = probe.recording('short-loop')
        rejected, same = probe.command(1, short['id'])
        assert not rejected and same['active'], same
        probe.advance(math.ceil((short['seconds'] * 2 + 0.5) * probe.fps))
        assert probe.audio()['active'], 'Rejected once request changed latched loop'
        probe.command(6)
        probe.command(1, long['id'])
        rejected, busy = probe.command(1, short['id'])
        assert not rejected and busy['active'] and busy['resource'] == long['id'], busy
        stopped, unchanged = probe.command(3, short['id'])
        assert not stopped and unchanged['active'], unchanged
        stopped, finished = probe.command(3, long['id'])
        assert stopped and not finished['active'], finished
        assert probe.diag()['draws'] > start_draw
        identity = identity_cases(probe)
        projectile = projectile_identity(probe)
        all_sounds = full_sounds(probe, rows) if not args.quick else []
        costs = integrated_cost(probe, short, long) if not args.quick else None
        medium = next(row for row in rows if row['seconds'] >= 1.5)
        probe.command(6)
        probe.command(1, medium['id'])
        accepted, _ = probe.command(2, medium['id'])
        assert not accepted
        probe.advance(math.ceil((medium['seconds'] + 0.1) * probe.fps))
        assert not probe.audio()['active'], 'Rejected loop request changed latched once mode'
        probe.command(2, long['id'])
        before_language = probe.audio()
        probe.language(1)
        after_language = probe.audio()
        assert after_language['active'] and after_language['identity'] == before_language['identity']
        assert after_language['starts'] == before_language['starts'] and after_language['stops'] == before_language['stops']
        probe.camera(1, 15120, -300, 22800, 90)
        after_level = probe.audio()
        assert not after_level['active'] and after_level['level_resets'] > after_language['level_resets'], after_level
        irq = irq_stack(probe)
        assert irq['untouched_margin_bytes'] >= 16, irq
        assert probe.audio()['underruns'] == 0, probe.audio()
        listening = load_json('analysis/g16-listening.json')
        assert sha(listening['wav_path']) == listening['wav_sha256']
        assert listening['status'] == 'confirmed_by_user'
        assert sha(probe.rom) == probe.metadata['rom_sha256'], 'ROM changed during ARM capture'
        report = dict(task='G16', status='quick' if args.quick else 'passed',
                      rom_sha256=sha(probe.rom), script_sha256=sha(__file__),
                      core_sample_rate=probe.sample_rate, core_fps=probe.fps,
                      once=once, loop=loop_audio, final=probe.audio(), source=source,
                      all_sounds=all_sounds, identity=identity, projectile_identity=projectile, integrated_cost=costs,
                      rejected_loop_preserved_once=True, language_preserved_active_voice=True,
                      level_change_stopped_voice=True, irq_stack=irq,
                      stack=stack_usage(probe.frontend), human_listening_verified=True,
                      listening_evidence_sha256=sha('analysis/g16-listening.json'),
                      physical_gba_tested=False)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print('G16 actual mGBA short/loop/latched-mode/busy/identity-stop capture passed')
    finally:
        probe.close()


if __name__ == '__main__':
    main()
