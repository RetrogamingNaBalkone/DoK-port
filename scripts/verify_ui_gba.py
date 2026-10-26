"""G17 independent keypad, pause, localization and framebuffer checks in mGBA.

Console code and rendering still execute when Python RGB conversion is disabled.
This harness does not assert whole-session DOS or physical GBA equivalence.
"""
import argparse
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import struct

from verify_gba import VIDEO, sha, png
from verify_sprites_gba import Probe, original_animations
from verify_renderer_gba import stack_usage
from analyze_levels import reference, T
from verify_ai_combat_gba import AICombatProbe, death_case, source_data
from verify_items_gba import ItemsProbe, baseline, MAP_OFFSET
from verify_sprites_gba import load_json

UI_NAMES = 'magic state focus language messages object slot length page pages remaining draws font8 font16 last_keys errors'.split()


def font8(language):
    resources, origin = original_animations(language)
    result = resources[b'FONT8'.hex()]
    assert len(result) == 2048
    return result, origin


def glyph_mask(frame, font, x, y, text, foreground, height=8):
    """Compare every bitmap pixel against independently read original font."""
    for index, glyph in enumerate(text):
        for row in range(height):
            bits = font[glyph * height + row]
            for col in range(8):
                at = (y + row) * 240 + x + index * 8 + col
                assert (frame[at] == foreground) == bool(bits & (128 >> col)), (
                    'FONT8 pixel mismatch', index, row, col)


def palette_extremes(palette):
    colors = struct.unpack('<256H', palette)
    luminosity = [sum((v & 31, (v >> 5) & 31, (v >> 10) & 31)) for v in colors]
    return min(range(256), key=luminosity.__getitem__), max(range(256), key=luminosity.__getitem__)


class UIProbe(AICombatProbe):
    preserve_title = True

    def __init__(self, core, build):
        super().__init__(core, build)
        self.video_callback = VIDEO(self.video)
        self.frontend.core.retro_set_video_refresh(self.video_callback)

    def video(self, data, width, height, pitch):
        self.frontend.width, self.frontend.height = width, height
        self.frontend.video_frames += 1

    def ui(self):
        return dict(zip(UI_NAMES, self.words('g_ui_diag', len(UI_NAMES))))

    def wait_ui(self, predicate, buttons=(), limit=6000):
        return self.until(lambda d: self.ui()['magic'] == 0x55493137 and predicate(self.ui()),
                          buttons=buttons, limit=limit)

    def command(self, op, arg=0, object_id=0, slot=0):
        address = self.symbols['g_ui_probe']
        request = self.words('g_ui_probe', 2)[1] + 1
        self.write(address + 8, struct.pack('<4I', op, arg, object_id, slot))
        self.write(address, struct.pack('<I', request))
        previous = self.ui()['draws']
        self.until(lambda d: self.words('g_ui_probe', 2)[1] == request
                   and self.ui()['draws'] > previous)
        return self.ui()

    def press(self, *buttons):
        masks = {0: 2, 2: 4, 3: 8, 4: 64, 5: 128, 6: 32, 7: 16,
                 8: 1, 10: 512, 11: 256}
        mask = sum(masks[b] for b in buttons)
        self.wait_ui(lambda u: u['last_keys'] == mask, buttons=buttons)
        result = self.ui()
        self.wait_ui(lambda u: u['last_keys'] == 0)
        return result

    def freeze(self):
        for name in ('g_motion_probe', 'g_visual_probe', 'g_combat_probe', 'g_ai_probe'):
            self.write(self.symbols[name], struct.pack('<I', 1))

    def language(self, language):
        self.command(2, language)

    def world_bytes(self):
        return self.frontend.read(self.symbols['g_render_world'] + 4, 33860)

    def world_sha(self):
        return hashlib.sha256(self.world_bytes()).hexdigest()

    def frame(self):
        d = self.present()
        raw = self.frontend.read(0x06000000 + d['visible_page'] * 0xa000, 38400)
        palette = self.frontend.read(0x05000000, 512)
        return raw, palette

    def screen(self, name):
        raw, palette = self.frame()
        rgb = bytearray()
        colors = struct.unpack('<256H', palette)
        for index in raw:
            value = colors[index]
            rgb.extend(((value & 31)*255//31, ((value >> 5) & 31)*255//31,
                        ((value >> 10) & 31)*255//31))
        path = self.build / 'ui-check' / (name + '.png')
        path.parent.mkdir(exist_ok=True)
        png(path, 240, 160, rgb)
        return dict(path=path.as_posix(), sha256=sha(path),
                    framebuffer_sha256=hashlib.sha256(raw).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', '--build-dir', type=Path, default=Path('build/g17'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g17-ui-gba.json'))
    args = parser.parse_args()
    p = UIProbe(args.core, args.build)
    try:
        p.wait_ui(lambda u: u['draws'] >= 2)
        assert p.ui()['state'] == 1 and p.ui()['language'] == 0, p.ui()
        screens = {'title_ru': p.screen('title-ru')}
        raw, palette = p.frame()
        _, white = palette_extremes(palette)
        resources, _ = original_animations(0)
        glyph_mask(raw, resources[b'FONT16'.hex()], 16, 20,
                   'ПОДЗЕМЕЛЬЯ КРЕМЛЯ'.encode('cp866'), white, height=16)
        # Every menu action below is driven by real libretro keypad input.
        p.press(5)
        assert p.ui()['focus'] == 1
        before = p.world_sha()
        p.press(8)
        assert p.ui()['language'] == 1 and p.world_sha() == before
        screens['title_en'] = p.screen('title-en')
        raw, palette = p.frame()
        _, white = palette_extremes(palette)
        resources, _ = original_animations(1)
        glyph_mask(raw, resources[b'FONT16'.hex()], 16, 20, b'KREMLIN DUNGEONS', white, height=16)
        p.press(5)
        p.press(8)
        assert p.ui()['state'] == 3
        screens['help_en'] = p.screen('help-en')
        p.press(0)
        assert p.ui()['state'] == 1 and p.ui()['focus'] == 2
        p.press(5)
        p.press(8)
        assert p.ui()['state'] == 4
        screens['statistics_en'] = p.screen('statistics-en')
        p.press(0)
        for _ in range(3):
            p.press(4)
        p.press(8)
        assert p.ui()['state'] == 0
        # Chord is pause, without a simultaneous Use or language switch.
        language = p.ui()['language']
        shots = p.words('g_combat_diag', 4)[3]
        events = p.words('g_interaction_diag', 3)[2]
        p.press(2, 3)
        assert p.ui()['state'] == 2 and p.ui()['language'] == language
        paused = p.world_sha()
        tick = p.words('g_time_diag', 3)[2]
        counter = p.words('g_time_diag', 5)[3]
        remainder = p.words('g_time_diag', 5)[4]
        motion = p.words('g_motion_diag', 4)
        for _ in range(6):
            p.present()
        assert p.world_sha() == paused and p.words('g_time_diag', 3)[2] == tick
        assert p.words('g_time_diag', 5)[4] == remainder
        assert (p.words('g_time_diag', 5)[3]-counter) & 0xffffffff > 0
        assert p.words('g_motion_diag', 4) == motion
        assert p.words('g_combat_diag', 4)[3] == shots
        assert p.words('g_interaction_diag', 3)[2] == events
        screens['pause_en'] = p.screen('pause-en')
        # Resume while A is held must not emit a shot until release/new press.
        p.wait_ui(lambda u: u['state'] == 0, buttons=(8,))
        assert (p.words('g_time_diag', 3)[2]-tick) & 0xffffffff < 416, 'Paused time leaked into resume'
        p.wait_ui(lambda u: u['last_keys'] == 1, buttons=(8,))
        for _ in range(2):
            previous = p.diag()['draws']
            p.until(lambda d: d['draws'] > previous and d['phase'] == 2, buttons=(8,))
            assert not p.words('g_motion_diag', 5)[4] & 0x200
        assert p.words('g_combat_diag', 4)[3] == shots
        p.wait_ui(lambda u: u['last_keys'] == 0)
        # Stop autonomous simulation only after the production pause path passed.
        p.freeze()
        p.command(1, 2)
        p.command(2, 0)
        snapshot = p.world_sha()
        p.command(2, 1)
        assert p.world_sha() == snapshot
        p.command(2, 0)
        assert p.world_sha() == snapshot
        p.command(1, 0)
        p.press(2)
        assert p.ui()['language'] == 1 and p.world_sha() == snapshot
        p.press(2)
        assert p.ui()['language'] == 0 and p.world_sha() == snapshot
        p.command(1, 2)
        p.press(2)
        assert p.ui()['state'] == 0 and p.ui()['language'] == 0
        p.command(1, 2)
        for _ in range(4):
            p.press(5)
        p.press(8)
        assert p.ui()['state'] == 1 and p.world_sha() == snapshot
        p.press(3)
        assert p.ui()['state'] == 0 and p.world_sha() == snapshot
        # Extreme signed16 values prove six glyph digits do not clip or overlap.
        player = p.symbols['g_render_world'] + 17412
        p.write(player, struct.pack('<hh', 32767, -32768))
        p.write(player + 20, struct.pack('<H', 0xffff))
        p.write(player + 32, struct.pack('<hh', -32768, 32767))
        p.command(1, 0)
        raw, palette = p.frame()
        _, white = palette_extremes(palette)
        font, origin = font8(0)
        glyph_mask(raw, font, 2, 2, b'HP', white)
        glyph_mask(raw, font, 26, 2, b'32767', white)
        glyph_mask(raw, font, 178, 2, b'-32768', white)
        glyph_mask(raw, font, 202, 134, b'FFFF', white)
        screens['hud_ru'] = p.screen('hud-ru')
        p.command(1, 2)
        # Original message identity is re-resolved after localization changes.
        resources, _ = original_animations(0)
        model = reference(resources[b'LEVEL1'.hex()])
        messages = [(len(bytes.fromhex(v)), i, off, v)
                    for i, obj in enumerate(model['objects']) for kind, off, v in obj['fields']
                    if kind == T and off in (0x58, 0xbe)]
        _, index, slot, expected = max(messages)
        p.command(3, object_id=index, slot=slot)
        p.wait_ui(lambda u: u['object'] == index and u['slot'] == slot and u['length'] > 0)
        message_checks = []
        for language in (0, 1):
            p.command(2, language)
            localized, _ = original_animations(language)
            local_model = reference(localized[b'LEVEL1'.hex()])
            glyph = model['objects'][index]['glyph']
            obj = next(o for o in local_model['objects'] if o['glyph'] == glyph)
            value = next(v for kind, off, v in obj['fields'] if kind == T and off == slot)
            text = bytes.fromhex(value)
            raw, palette = p.frame()
            _, white = palette_extremes(palette)
            bitmap, _ = font8(language)
            glyph_mask(raw, bitmap, 0, 144, text[:30], white)
            if len(text) > 30:
                glyph_mask(raw, bitmap, 0, 152, text[30:60], white)
            message_checks.append(dict(language=language, bytes=len(text), object=index, slot=slot))
            screens[f'message_{language}'] = p.screen(f'message-{language}')
        p.command(4, 0xffffffff)
        assert p.ui()['length'] == 240 and p.ui()['pages'] == 4 and p.ui()['errors'] == 0, p.ui()
        raw, palette = p.frame()
        _, white = palette_extremes(palette)
        bitmap, _ = font8(1)
        pattern = bytes(65+i % 26 for i in range(60))
        glyph_mask(raw, bitmap, 0, 144, pattern[:30], white)
        glyph_mask(raw, bitmap, 0, 152, pattern[30:], white)
        screens['stress'] = p.screen('stress')
        page_checks = []
        for page in (1, 2, 3):
            deadline = (p.words('g_time_diag', 3)[2] + p.ui()['remaining']) & 0xffffffff
            p.command(5, deadline)
            assert p.ui()['page'] == page and p.ui()['length'] == 240, p.ui()
            raw, palette = p.frame()
            pattern = bytes(65+i % 26 for i in range(page*60, (page+1)*60))
            glyph_mask(raw, bitmap, 0, 144, pattern[:30], white)
            glyph_mask(raw, bitmap, 0, 152, pattern[30:], white)
            page_checks.append(dict(page=page, exact_font_pixels=True))
        # Real production notification sinks, rather than only UI mailboxes.
        notifications = []
        resource_ids = {r['name_hex']: r['id'] for r in load_json('build/gba/assets/manifest.json')['resources']}
        for language in (0, 1):
            p.command(4, 0)
            p.command(2, language)
            p.camera(1, 15120, -300, 22800, 90)
            p.camera(0, 15120, -300, 22800, 90)
            runtime = load_json(f'build/runtime/{"ru" if language == 0 else "en"}/LEVEL1-runtime.json')
            p.set_player(baseline())
            target = 47*64+30
            p.pose(runtime, target)
            p.write(p.symbols['g_render_world'] + MAP_OFFSET + 4096 + target, b'8')
            item = next(i for i, o in enumerate(runtime['objects']) if o['glyph'] == ord('8'))
            assert ItemsProbe.command(p, 2) == 1
            assert p.ui()['object'] == item and p.ui()['slot'] == 0x58 and p.ui()['length'] > 0, p.ui()
            screens[f'pickup_{language}'] = p.screen(f'pickup-{language}')
            p.command(4, 0)
            initial = p.frontend.read(p.symbols['g_render_world'], 33864)
            index = runtime['glyph_index'][0x80]-1
            fields, animations, _ = source_data(language, 0, 0x80)
            death_case(p, runtime, initial, index, 0, animations['death'], resource_ids[fields[0xec]])
            assert p.ui()['object'] == index and p.ui()['slot'] == 0xbe and p.ui()['length'] > 0, p.ui()
            raw, palette = p.frame()
            _, white = palette_extremes(palette)
            bitmap, _ = font8(language)
            text = bytes.fromhex(fields[0xbe])
            glyph_mask(raw, bitmap, 0, 144, text[:30], white)
            if len(text)>30:
                glyph_mask(raw, bitmap, 0, 152, text[30:60], white)
            screens[f'death_{language}'] = p.screen(f'death-{language}')
            notifications.append(dict(language=language, actual_pickup_item=item,
                                      actual_completed_monster_death=index, exact_death_text_pixels=True))
        assert p.diag()['scene_height'] == 144 and p.diag()['message_height'] == 16
        stack = stack_usage(p.frontend)
        assert stack['untouched_margin_bytes'] >= 512, stack
        assert sha(p.rom) == p.metadata['rom_sha256'], 'ROM changed during verification'
        report = dict(task='G17', status='passed', rom_sha256=sha(p.rom),
                      script_sha256=sha(__file__), source=origin, screens=screens,
                      checks=dict(real_keypad_menu=True, title_help_statistics=True,
                                  chord_suppressed_use_and_language=True, paused_world_and_clock=True,
                                  pause_preserved_fraction_and_raw_timer_continued=True,
                                  resume_excluded_paused_time=True,
                                  no_resume_fire_leak=True, language_preserved_world=True,
                                  exact_original_font8_pixels=True, exact_original_font16_pixels=True,
                                  signed16_hud_extremes=True,
                                  bounded_240byte_stress=True, viewport_240x144_messages_240x16=True),
                      messages=message_checks, pages=page_checks, notifications=notifications,
                      final=p.ui(), stack=stack,
                      physical_gba_tested=False, full_dos_session_tested=False)
        args.report.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
        print('G17 ARM keypad/pause/world/localization/FONT8/bounds checks passed')
    finally:
        p.close()


if __name__ == '__main__':
    main()
