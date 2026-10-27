"""G18 independent ARM game lifecycle and resource-screen verification.

Controlled player poses/stats exercise production game rules; actual keypad
events advance screens. This is not a manual whole-DOS or hardware playthrough.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

from verify_ui_gba import UIProbe, glyph_mask, message_mask, ui_colors
from verify_sprites_gba import Probe, original_animations, load_json
from verify_gba import sha
from verify_renderer_gba import stack_usage
from verify_items_gba import MAP_OFFSET

FLOW_NAMES = 'magic state level session lives score transitions resets pending text_id text_size page pages errors'.split()


def screen_lines(data):
    """Independent source-text layout, retaining original blank lines."""
    data = data.split(b'$', 1)[0].split(b'\0', 1)[0].replace(b'\r', b'')
    rows = [bytearray()]
    for value in data:
        if value == 10:
            rows.append(bytearray())
        else:
            if len(rows[-1]) == 28:
                rows.append(bytearray())
            rows[-1].append(value if value >= 32 else 32)
    return [bytes(row) for row in rows]


class FlowProbe(UIProbe):
    preserve_title = True

    def flow(self):
        result = dict(zip(FLOW_NAMES, self.words('g_flow_diag', len(FLOW_NAMES))))
        assert result['magic'] == 0x464c3138 and result['errors'] == 0, result
        return result

    def wait_flow(self, state):
        try:
            self.until(lambda d: self.flow()['state'] == state and d['phase'] == 2)
        except AssertionError:
            raise AssertionError(('Flow state timeout',state,self.flow(),self.ui(),self.player()))
        return self.flow()

    def press(self, *buttons):
        """Screen input uses the raw production keypad, including non-menu pages."""
        masks = {0: 2, 2: 4, 3: 8, 4: 64, 5: 128, 6: 32, 7: 16,
                 8: 1, 10: 512, 11: 256}
        mask = sum(masks[b] for b in buttons)
        previous = self.diag()['draws']
        self.until(lambda d: d['last_keys'] == mask and d['draws'] > previous
                   and d['phase'] == 2, buttons=buttons)
        previous = self.diag()['draws']
        self.until(lambda d: d['last_keys'] == 0 and d['draws'] > previous and d['phase'] == 2)

    def runtime(self, level, language=0):
        return load_json(f'build/runtime/{"ru" if language == 0 else "en"}/LEVEL{level+1}-runtime.json')

    def player_pose(self, model, cell):
        obj = model['objects'][model['player_index']-1]
        address = self.symbols['g_render_world'] + 9220 + obj['actor_index']*64
        self.write(address+4, struct.pack('<iii', cell%64*480+240, -300, cell//64*480+240))

    def menu_focus(self, focus):
        if self.ui()['state']==6:self.press(0)
        assert self.flow()['state'] == 0 and self.ui()['state'] == 1, (self.flow(), self.ui())
        for _ in range(6):
            if self.ui()['focus'] == focus:
                return
            self.press(5)
        raise AssertionError(('Title focus unavailable', focus, self.ui()))

    def new_game(self, level, language):
        self.menu_focus(0)
        if self.ui()['language'] != language:
            self.press(2)
        self.press(8)
        assert self.ui()['state']==6,self.ui()
        for _ in range((level-self.ui()['focus']) % 3):
            self.press(5)
        self.selected_level = level
        self.press(8)
        f = self.wait_flow(1)
        assert f['level'] == level and f['session'] == 1 and f['page'] == 0, f
        assert self.player()[16] == 3 and self.player()[17] == 0 and not any(self.player()[18:27]), self.player()
        assert self.player()[10] == 0 and self.player()[27] == 0, 'Fresh world retained keys/map reveal'
        assert not any(self.frontend.read(self.symbols['g_render_world']+MAP_OFFSET+12288,4096)), 'Fresh world retained visited cells'
        assert self.owner_counts() == dict(ai=0,visual=0,interactions=0), self.owner_counts()
        assert not self.active_slots() and not any(self.frontend.read(self.symbols['g_combat']+12+10240,512))
        assert self.words('g_audio_diag',2)[1] == 0 and self.ui()['length'] == 0
        return f

    def owner_counts(self):
        return {name:struct.unpack('<H',self.frontend.read(self.symbols[symbol]+offset,2))[0]
                for name,symbol,offset in (('ai','g_ai',20),('visual','visual',8),('interactions','interactions',12))}

    def owners_snapshot(self):
        return {name:self.frontend.read(self.symbols[symbol]+offset,32+count*56)
                for name,symbol,offset,count in (('ai','g_ai',12,128),('visual','visual',0,256),('interactions','interactions',4,256))}

    def audio_command(self, op, resource=0):
        address=self.symbols['g_audio_probe']
        request=self.words('g_audio_probe',2)[1]+1
        self.write(address+8,struct.pack('<2I',op,resource))
        self.write(address,struct.pack('<I',request))
        self.until(lambda d:self.words('g_audio_probe',2)[1]==request)
        return self.words('g_audio_probe',5)[4]

    def seed_transients(self,model,initial):
        world=self.symbols['g_render_world']
        actor=world+9220+model['objects'][model['player_index']-1]['actor_index']*64
        planes=[self.frontend.read(world+MAP_OFFSET+i*4096,4096) for i in range(3)]
        definitions={o['glyph']:o for o in model['objects']}
        found=False
        for cell,glyph in enumerate(planes[1]):
            if glyph!=ord('d'):continue
            for delta,heading in ((-64,0),(64,180),(-1,270),(1,90)):
                neighbor=cell+delta
                if 0<=neighbor<4096 and planes[0][neighbor]==32 and definitions[planes[1][neighbor]]['flags']&16:
                    self.player_pose(model,neighbor)
                    self.write(actor+36,struct.pack('<h',heading))
                    self.motion_step(0x800)
                    found=True
                    break
            if found:break
        assert found and self.owner_counts()['interactions']>0
        actor_offset=9220+model['objects'][model['player_index']-1]['actor_index']*64
        self.write(actor+4,initial[actor_offset+4:actor_offset+16])
        self.write(actor+36,initial[actor_offset+36:actor_offset+42])
        monster=next(i for i,o in enumerate(model['objects']) if o['class']==9)
        self.ai(2,monster)
        Probe.poll(self,10000,monster,0xfff1,True)
        self.control(5,model['player_index']-1,ord('1'))
        assert self.active_slots()
        self.audio_command(6)
        assert self.audio_command(2,62)==1
        assert all(self.owner_counts().values()),self.owner_counts()

    def source_screens(self, name, language, output):
        data, origin = original_animations(language)
        source = data[name.encode().hex()]
        rows = screen_lines(source)
        expected_pages = (len(rows)+13)//14
        f = self.flow()
        assert f['text_size'] == len(source) and f['pages'] == expected_pages, (name, f, expected_pages)
        assert f['page'] == 0
        pages = []
        for page in range(expected_pages):
            raw, palette = self.frame()
            black, white = ui_colors(palette)
            font = data[b'FONT8'.hex()]
            for row in range(14):
                index = page*14+row
                line = rows[index] if index < len(rows) else b''
                glyph_mask(raw, font, 8, 24+row*8, line.ljust(28, b' '), white)
            hint = 'A/START: OK B: BACK' if language else 'A/START: OK B: НАЗАД'
            footer = f'{hint} {page+1}/{expected_pages}'.encode('cp866')
            message_mask(raw, font, footer, white)
            assert self.flow()['page'] == page
            pages.append(self.screen(f'{name.lower()}-{language}-page{page}'))
            if name=='EINTRO3' and language==1 and page==expected_pages-1:
                before=self.world_sha()
                self.press(2)
                current=self.flow()
                assert current['page']<current['pages'],('Language left an invalid screen page',current)
                assert self.world_sha()==before
                self.press(2)
                while self.flow()['page']<page:
                    self.press(8)
            if page+1 < expected_pages:
                self.press(8)
        output.append(dict(resource=name, language=language, bytes=len(source), pages=pages,
                           source=origin, source_sha256=hashlib.sha256(source).hexdigest(),
                           all_original_text_pixels=True))
        self.press(8)

    def to_title(self):
        self.press(2, 3)
        assert self.ui()['state'] == 2
        for _ in range(3):
            self.press(5)
        self.press(8)
        self.wait_flow(0)

    def trigger_exit(self, model, score):
        exits = [obj for obj in model['objects'] if obj['class'] == 12 and
                 any(f['offset'] == 0x66 and f.get('number') == 1000 for f in obj['fields'])]
        assert len(exits) == 1
        fcmap = self.frontend.read(self.symbols['g_render_world']+MAP_OFFSET+8192, 4096)
        target = fcmap.index(bytes([exits[0]['glyph']]))
        self.player_pose(model, target)
        self.write(self.symbols['g_render_world']+17412+34, struct.pack('<h', score))
        self.motion_step(0)
        return target


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', '--build-dir', type=Path, default=Path('build/g18'))
    parser.add_argument('--core', type=Path, default=Path(os.environ['LOCALAPPDATA']) /
                        'Programs/mGBA/libretro-20261024/mgba_libretro.dll')
    parser.add_argument('--report', type=Path, default=Path('analysis/g18-flow-gba.json'))
    args = parser.parse_args()
    p = FlowProbe(args.core, args.build)
    try:
        p.until(lambda d: d['draws'] >= 2 and d['phase'] == 2)
        assert p.ui()['state']==5,p.ui()
        p.press(8)
        assert p.flow()['state'] == 0 and p.ui()['state'] == 1
        p.selected_level = 0
        p.freeze()
        # Keep each committed frame exposed for a VBlank; this existing test
        # aid changes capture cadence only, outside measured renderer cycles.
        p.write(p.symbols['g_camera_capture_hold'],struct.pack('<I',1))
        screens, levels, retries = [], [], []
        ui_screens={'title_ru':p.screen('flow-title-ru')}
        for language in (0, 1):
            for level in range(3):
                p.new_game(level, language)
                # All pages are raster-checked against independently read RES.
                p.source_screens(f'BINTRO{level+1}', language, screens)
                p.wait_flow(2)
                model = p.runtime(level, language)
                assert model['pickup_99_total'] == 0 and p.player()[17] == 0
                fresh = p.frontend.read(p.symbols['g_render_world'], 33864)
                initial_actor = model['objects'][model['player_index']-1]['actor_index']
                if level == 0:
                    # Equip a real weapon, alter persistent state, then kill the
                    # hero through the production stats path, not a flow seam.
                    weapon = next((i,o) for i,o in enumerate(model['objects']) if o['class'] == 10)
                    values = p.player()
                    values[0:4] = [100,77,150,180]
                    values[10] = 15
                    values[18] = weapon[0]+1
                    values[27] = 1
                    p.set_player(values)
                    world = p.symbols['g_render_world']
                    p.write(world+MAP_OFFSET+12288+17, b'\x01')
                    actor = world+9220+initial_actor*64
                    # Native30-byte snapshot must not overwrite distant fields.
                    p.seed_transients(model,fresh)
                    p.write(actor+24, struct.pack('<ii', 12345, -23456))
                    p.write(actor+56, struct.pack('<h', 321))
                    persistent = p.player()
                    map_before = p.frontend.read(world+MAP_OFFSET, 16384)
                    events_before=p.owners_snapshot()
                    projectiles_before=p.frontend.read(p.symbols['g_combat']+12,10240+512)
                    sound_identity=p.words('g_audio_diag',19)[18]
                    hero = model['objects'][model['player_index']-1]['glyph']
                    old_cell = map_before[:4096].index(bytes([hero]))
                    new_cell = next(i for i, g in enumerate(map_before[:4096])
                                    if g == 32 and i != old_cell)
                    p.write(world+MAP_OFFSET+old_cell,b' ')
                    p.write(world+MAP_OFFSET+new_cell,bytes([hero]))
                    p.player_pose(model,new_cell)
                    p.write(actor+16,struct.pack('<ii',123,456))
                    p.write(actor+36,struct.pack('<hhh',137,11,3))
                    from verify_items_gba import ItemsProbe
                    assert ItemsProbe.command(p, 1, 0, -100) == 1
                    p.wait_flow(3)
                    assert p.player()[16] == 2 and p.player()[0] == 0
                    ui_screens[f'retry_{language}']=p.screen(f'flow-retry-{language}')
                    p.press(8)
                    p.wait_flow(2)
                    after = p.player()
                    expected = persistent[:]
                    expected[0] = 150
                    expected[16] = 2
                    assert after == expected, (after, expected)
                    map_after=p.frontend.read(world+MAP_OFFSET,16384)
                    assert map_after[:12288] == map_before[:12288]
                    assert all(a==b or (a==1 and b==0) for a,b in zip(map_after[12288:],map_before[12288:])), 'Retry erased explored cells'
                    assert p.owners_snapshot() == events_before, 'Retry reset original live scheduler events'
                    assert p.frontend.read(p.symbols['g_combat']+12,10240+512)==projectiles_before
                    assert p.words('g_audio_diag',19)[18]==sound_identity and p.words('g_audio_diag',2)[1]==1
                    assert struct.unpack('<ii', p.frontend.read(actor+24,8)) == (12345,-23456)
                    assert struct.unpack('<h',p.frontend.read(actor+56,2))[0] == 321
                    pose = struct.unpack('<iii',p.frontend.read(actor+4,12))
                    initial_pose = struct.unpack_from('<iii',fresh,9220+initial_actor*64+4)
                    assert pose == initial_pose
                    assert p.frontend.read(actor+16,8) == fresh[9220+initial_actor*64+16:9220+initial_actor*64+24]
                    assert p.frontend.read(actor+36,6) == fresh[9220+initial_actor*64+36:9220+initial_actor*64+42]
                    retries.append(dict(language=language, lives=2, inventory_world_visited_preserved=True,
                                        original_pose_subset_restored=True, other_actor_fields_preserved=True,
                                        ai_visual_door_events_preserved=True, projectiles_preserved=True,
                                        existing_sound_preserved_by_busy_death_voice=True))
                # Counterfactual score guard is tested separately from actual
                # PKR gameplay: its real score and total are both zero.
                if level == 0:
                    p.trigger_exit(model, 1)
                    assert p.flow()['state'] == 2, p.flow()
                cell = p.trigger_exit(model, 0)
                p.wait_flow(4)
                assert p.owner_counts()==dict(ai=0,visual=0,interactions=0),p.owner_counts()
                assert not p.active_slots() and p.words('g_audio_diag',2)[1]==0
                p.source_screens(f'EINTRO{level+1}', language, screens)
                p.wait_flow(0)
                assert p.flow()['session'] == 0, p.flow()
                levels.append(dict(language=language, level=level+1, actual_fcmap_exit_cell=cell,
                                   score_collection_required=False, score=0, total=0,
                                   completed_to_title=True, fresh_inventory_on_start=True,
                                   retired_world_event_projectile_audio_cleanup=True))
            # Original ENDTXT belongs to leaving the title menu, not a newly
            # invented campaign after LEVEL3.
            p.menu_focus(3)
            p.press(8)
            p.wait_flow(5)
            p.source_screens('ENDTXT', language, screens)
            p.wait_flow(0)
        # Exhaust the original three lives via real damage; every new game is
        # a fresh world and restores three lives after Game Over.
        p.new_game(0, 1)
        p.source_screens('BINTRO1', 1, screens)
        p.wait_flow(2)
        from verify_items_gba import ItemsProbe
        for remaining in (2,1,0):
            hp = p.player()[0]
            assert ItemsProbe.command(p,1,0,-hp) == 1
            p.wait_flow(3 if remaining else 6)
            assert p.player()[16] == remaining
            if not remaining:
                ui_screens['gameover_en']=p.screen('flow-gameover-en')
            p.press(8)
            p.wait_flow(2 if remaining else 0)
        p.new_game(2,0)
        p.source_screens('BINTRO3',0,screens)
        p.wait_flow(2)
        assert p.player()[16] == 3
        # Continue belongs only to Pause and preserves the existing live world.
        model=p.runtime(2,0)
        fresh=p.frontend.read(p.symbols['g_render_world'],33864)
        values=p.player();values[10]=15;p.set_player(values)
        p.seed_transients(model,fresh)
        world_before=p.world_sha()
        owners_before=p.owners_snapshot()
        slots_before=p.frontend.read(p.symbols['g_combat']+12,10240+512)
        p.press(2,3)
        ui_screens['pause_ru']=p.screen('flow-pause-ru')
        assert p.ui()['state']==2 and p.ui()['focus']==0
        p.press(8);p.wait_flow(2)
        assert p.world_sha()==world_before and p.owners_snapshot()==owners_before
        assert p.frontend.read(p.symbols['g_combat']+12,10240+512)==slots_before
        assert p.words('g_audio_diag',2)[1]==1
        # END means close the run even when entered from a suspended session.
        p.to_title();p.menu_focus(3);p.press(8);p.wait_flow(5)
        assert p.flow()['session']==0
        assert p.owner_counts()==dict(ai=0,visual=0,interactions=0),p.owner_counts()
        assert not p.active_slots() and p.words('g_audio_diag',2)[1]==0
        p.source_screens('ENDTXT',0,screens);p.wait_flow(0)
        # Starting a new world over a live suspended world also discards its
        # actors, dynamic occupancy, owners and DMA stream.
        p.new_game(0,0);p.source_screens('BINTRO1',0,screens);p.wait_flow(2)
        model=p.runtime(0,0)
        fresh=p.frontend.read(p.symbols['g_render_world'],33864)
        values=p.player();values[10]=15;p.set_player(values)
        p.seed_transients(model,fresh)
        p.to_title();p.new_game(1,0)
        p.source_screens('BINTRO2',0,screens);p.wait_flow(2)
        # Native negative HP is a finish sentinel; stats clamps never create
        # this artificial fixture. Normal six-level exits were proven above.
        p.write(p.symbols['g_render_world']+17412,struct.pack('<h',-1))
        p.wait_flow(4)
        p.source_screens('EINTRO2',0,screens);p.wait_flow(0)
        stack = stack_usage(p.frontend)
        assert stack['untouched_margin_bytes'] >= 512
        assert sha(p.rom) == p.metadata['rom_sha256'], 'ROM changed during verification'
        report = dict(task='G18',status='passed',rom_sha256=sha(p.rom),script_sha256=sha(__file__),
                      levels=levels,retries=retries,screens=screens,ui_screens=ui_screens,gameover_three_lives=True,
                      new_game_after_gameover=True,ending_from_title=True,
                      pause_continue_preserved_world_events_projectiles=True,
                      ending_discarded_suspended_world_streams=True,
                      new_game_replaced_live_world_without_old_owners=True,
                      negative_hp_native_finish_sentinel=True,
                      source_flow_independent_levels=True,no_automatic_campaign_or_inventory_carry=True,
                      stack=stack,final=p.flow(),physical_gba_tested=False,whole_dos_session_tested=False)
        args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print('G18 ARM: all6 worlds,14 original text resources, retry/lives/gameover/source title flow passed')
    finally:
        p.close()


if __name__ == '__main__':
    main()
