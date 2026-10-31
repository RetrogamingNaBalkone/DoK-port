"""N21 independent real-input observer; no writes to emulated RAM or VRAM."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import traceback
import shutil

import make_assets_fixture as fat
import run_diag_emulator as transport
import run_renderer_emulator as render
from frontend_fat_oracle import volume_files, audit_changes
from frontend_pixel_oracle import OriginalFrontendArt, render_frontend
from check_audio_capture import original_clip, compare_capture

ROOT = Path(__file__).resolve().parents[3]
CORE = Path.home() / "AppData/Local/Programs/melonDS-DS/1.4.0/melondsds_libretro-win32-x86_64-Release/cores/melondsds_libretro.dll"
A, B, START, UP, DOWN, SELECT, X, Y = 8, 0, 3, 4, 5, 2, 9, 1


class FrontendObserver(transport.Frontend):
    def __init__(self, core, target, directory, readonly=False):
        super().__init__(core, directory, "happy")
        self.options["melonds_homebrew_readonly"] = "enabled" if readonly else "disabled"
        self.target = target
        self.address, size = transport.elf_symbol(target / "dok_nds_gameplay.elf", "g_nds_frontend_probe")
        assert size == 256
        self.render_address, _ = transport.elf_symbol(target / "dok_nds_gameplay.elf", "g_nds_render_probe")
        self.render_fields = render.scalar_fields(ROOT / "ports/nds/platform/render_probe/render_probe.h", "NdsRenderProbe")
        self.art = OriginalFrontendArt()
        copy = json.loads((ROOT / "ports/nds/platform/frontend/labels.json").read_text("utf8"))
        self.labels, self.controls = copy["labels"], copy["controls"]
        self.report = dict(task="N21", passed=False, rows=[], input_steps=[],
                           index8_checks=0, rgb_checks=0, source_text_checks=0,
                           observer_sha256=transport.sha(Path(__file__)),
                           pixel_oracle_sha256=transport.sha(ROOT / "ports/nds/tools/frontend_pixel_oracle.py"),
                           fat_oracle_sha256=transport.sha(ROOT / "ports/nds/tools/frontend_fat_oracle.py"),
                           rom_sha256=transport.sha(target / "dok_nds_gameplay.nds"),
                           elf_sha256=transport.sha(target / "dok_nds_gameplay.elf"))
        self.probe = None
        self.stable = None
        self.stability = 0
        self.checked = set()
        self.audio_address, _ = transport.elf_symbol(target / "dok_nds_gameplay.elf", "g_nds_audio_probe")
        self.audio_segment = None
        self.audio_pcm = bytearray()
        self.audio_recent = bytearray()
        self.report["audio"] = []
        self.report["visual_alias"] = []
        self.visual_seen = set()
        self.popal_pixels = {language: self.art.popal_gpu(language) for language in (1, 2)}

    def tick(self, count=1):
        for _ in range(count):
            if self.frame_time and self.frame_time.callback:
                self.frame_time.callback(round(1000000 / self.av.timing.fps))
            self.core.retro_run()
            self.frames += 1
            assert not self.callback_error and not self.shutdown, self.callback_error
            self.observe_audio()
            renderer = render.observe(self, self.render_address, self.render_fields, 0x37524b44)
            if renderer:
                self.report["last_renderer"] = renderer
                assert renderer["status"] != 3 and not renderer["error_bits"], ("Actual renderer failure", renderer)
            before = render.ram(self, self.address + 8, 4)
            p = struct.unpack("<64I", render.ram(self, self.address, 256))
            after = render.ram(self, self.address + 8, 4)
            if before != after or p[2] & 1 or p[0] != 0x21504b44 or p[1] != 1 or not p[3]:
                continue
            assert p[30] == 0, ("Frontend fault", p)
            self.probe = p
            self.report["last_probe"] = list(p)
            if p[3] == 8 and p[63] and p[60]:
                expected_id = (p[4] << 30) | (145 << 18) | (5 << 10) | 5
                assert p[60] == expected_id and p[62] == 32768
                assert p[33] and p[34] == 40 and p[37] > 0
                descriptor = struct.unpack_from("<Q4I4HI", render.ram(self, p[33], p[34]))
                key, rid, pointer, size, palette, sw, sh, width, height, transparent = descriptor
                assert (rid, pointer, size, sw, sh, width, height) == (expected_id, p[61], 32768, 256, 128, 158, 70)
                assert key & 0xffffffff == expected_id and transparent == 1
                assert render.ram(self, p[61], p[62]) == self.popal_pixels[p[4]], "Actual localized POPAL cache differs from raw original"
                visual_key = (p[4], p[6], p[44])
                if visual_key not in self.visual_seen:
                    self.visual_seen.add(visual_key)
                    self.report["visual_alias"].append(dict(language=p[4], level=p[6], serial=p[44],
                        asset_id=p[60], cache_address=p[61], bytes=p[62], frontend_frame=self.frames,
                        descriptor=list(descriptor), rendered_frame=p[35], slot=p[36], quad_references=p[37],
                        images=self.capture(f"popal-{p[4]}-{p[6]}-{p[44]}")))
            key = (p[3], p[4], p[5], p[6], p[7], p[11], p[24])
            self.stability = self.stability + 1 if key == self.stable else 1
            self.stable = key
            if self.stability == 1:
                (self.directory / "progress.json").write_text(json.dumps(dict(
                    frontend_frame=self.frames, probe=list(p), waiting=self.report.get("waiting"))), encoding="utf8")
            if p[3] != 8 and self.stability >= 3 and key not in self.checked:
                self.check_pixels(p)
                self.checked.add(key)
        return self.probe

    def observe_audio(self):
        pcm = bytes(self.pcm)
        self.pcm.clear()
        self.audio_recent.extend(pcm)
        keep = round(self.av.timing.sample_rate * .05) * 4
        del self.audio_recent[:-keep]
        a = struct.unpack("<64I", render.ram(self, self.audio_address, 256))
        if a[0] != 0x18504b44 or a[63] & 1:
            if self.audio_segment:
                self.audio_pcm.extend(pcm)
            return
        assert not a[8] and not a[9] and not a[22], ("Audio fault", a)
        self.report["last_audio_probe"] = list(a)
        title = a[4] and a[5] == 0x8000006e
        if self.audio_segment:
            self.audio_pcm.extend(pcm)
            if not title or a[14] != self.audio_segment["start_probe"][14]:
                self.finish_audio(a)
        if title and self.audio_segment is None:
            self.audio_segment = dict(start_probe=list(a), start_frame=self.frames,
                                      state=self.probe[3] if self.probe else 1,
                                      language=self.report.get("initial_language", 1))
            self.audio_pcm.extend(self.audio_recent)

    def finish_audio(self, end):
        segment = self.audio_segment
        assert segment is not None
        segment["end_probe"] = list(end)
        segment["complete"] = end[7] == 474361
        path = self.directory / f"title-{len(self.report['audio'])}.wav"
        transport.save_wav(path, self.audio_pcm, self.av.timing.sample_rate)
        segment["wav"] = path.relative_to(ROOT).as_posix()
        if segment["complete"]:
            wave = compare_capture(path, original_clip(segment["language"], 110),
                                   33513982 / (2 * segment["start_probe"][12]),
                                   output_rate=self.av.timing.sample_rate)
            assert wave["complete_source_covered"] and wave["whole_coefficient"] >= .95
            segment["waveform"] = wave
        self.report["audio"].append(segment)
        self.audio_segment = None
        self.audio_pcm.clear()

    def wait_audio_complete(self, limit=4000):
        count = len(self.report["audio"])
        for _ in range(limit):
            self.tick()
            if len(self.report["audio"]) > count:
                assert self.report["audio"][-1]["complete"], "Title ended before source EOS"
                return
        raise AssertionError("Title did not reach complete source EOS")

    def check_pixels(self, p):
        expected = render_frontend(self.art, self.labels, self.controls,
                                   p[3], p[4], p[5], p[6], p[7], p[31])
        index = expected["text_index"]
        assert p[16] == (0xffffffff if index is None else index)
        assert p[19] == expected["background"]
        assert p[8] == expected["pages"]
        assert p[17] == len(expected["message"])
        if p[17]:
            assert render.ram(self, p[18], p[17]) == expected["message"]
            self.report["source_text_checks"] += p[17]
        width, height, rgb = self.rgb()
        assert (width, height) == (256, 384)
        for screen, pixel_ptr, palette_ptr, base in (("top", p[20], p[22], 0),
                                                    ("bottom", p[21], p[23], 49152)):
            pixels = expected[screen]
            actual = render.ram(self, pixel_ptr, 49152)
            assert actual == pixels, ("INDEX8 mismatch", screen, next(i for i, pair in enumerate(zip(actual, pixels)) if pair[0] != pair[1]))
            palette = tuple(expected[screen + "_palette"])
            assert struct.unpack("<256H", render.ram(self, palette_ptr, 512)) == palette
            for i, color in enumerate(pixels):
                at = (base + i) * 3
                value = (rgb[at] >> 3) | ((rgb[at + 1] >> 3) << 5) | ((rgb[at + 2] >> 3) << 10)
                if value != palette[color]:
                    self.capture("pixel-mismatch")
                    raise AssertionError(("Actual LCD RGB mismatch", screen, i % 256, i // 256, value, palette[color]))
            self.report["index8_checks"] += 49152
            self.report["rgb_checks"] += 49152
        row = dict(state=p[3], language=p[4], selection=p[5], level=p[6], page=p[7],
                   pages=p[8], revision=p[11], draws=p[24], frontend_frame=self.frames)
        image_key = (p[3], p[4], p[6] if p[3] in (7, 10) else 0, p[7])
        if not any(r.get("image_key") == list(image_key) for r in self.report["rows"]):
            row["image_key"] = list(image_key)
            row["images"] = self.capture(f"frontend-{p[3]}-{p[4]}-{p[6]}-{p[7]}")
        self.report["rows"].append(row)

    def wait_for(self, state, *, selection=None, language=None, page=None, unblocked=True, limit=12000):
        self.report["waiting"] = dict(state=state, selection=selection, language=language, page=page)
        for _ in range(limit):
            self.tick()
            p = self.probe
            if p and p[3] == state and self.stability >= 4 and (not unblocked or not p[9]) and (
                selection is None or p[5] == selection
            ) and (language is None or p[4] == language) and (page is None or p[7] == page):
                return p
        raise AssertionError(("Frontend input timed out", state, selection, language, page, self.probe))

    def tap(self, button, hold=1, release=6):
        self.joypad = 1 << button
        self.tick(hold)
        self.joypad = 0
        self.tick(release)
        self.report["input_steps"].append(dict(button=button, hold=hold, frame=self.frames,
                                                state=self.probe[3] if self.probe else None))

    def choose(self, state, selection):
        self.wait_for(state)
        count = {2: 5, 3: 3, 4: 2, 9: 4}[state]
        for _ in range(count + 1):
            if self.probe[5] == selection:
                return
            expected = (self.probe[5] + 1) % count
            self.tap(DOWN)
            self.wait_for(state, selection=expected)
        raise AssertionError("Menu focus did not move")

    def text_to(self, state, destination, backward=True):
        p = self.wait_for(state)
        pages = p[8]
        if backward and pages > 1:
            self.tap(A)
            self.wait_for(state, page=1)
            self.tap(B)
            self.wait_for(state, page=0)
        for page in range(pages):
            self.wait_for(state, page=page)
            self.tap(A)
            if page + 1 < pages:
                self.wait_for(state, page=page + 1)
            else:
                self.wait_for(destination)

    def chord(self, buttons, hold=12):
        self.joypad = sum(1 << button for button in buttons)
        self.tick(hold)
        self.joypad = 0
        self.tick(8)
        self.report["input_steps"].append(dict(chord=list(buttons), hold=hold, frame=self.frames))

    def language_from_pause(self, language):
        p = self.wait_for(9)
        frozen = p[40:52]
        self.choose(9, 1)
        self.tap(A)
        self.wait_for(4)
        self.choose(4, language - 1)
        self.tap(A)
        p = self.wait_for(9, language=language)
        assert p[40:52] == frozen, "Language changed the paused world or clocks"
        for before, after in ((p[52], p[53]), (p[54], p[55])):
            assert before and after and render.ram(self, before, 32) == render.ram(self, after, 32)
        self.report.setdefault("language_preservation", []).append(dict(language=language, game=list(frozen)))


def full_flow(frontend, language):
    f = frontend
    f.report["phase"] = "title"
    f.wait_for(1, language=language)
    f.wait_audio_complete()
    f.tap(A)
    f.wait_for(2)
    f.choose(2, 3)
    f.report["phase"] = "story"
    f.tap(A)
    f.wait_for(6)
    f.wait_audio_complete()
    f.text_to(6, 2)
    f.choose(2, 2)
    f.tap(A)
    f.wait_for(5)
    f.tap(B)
    f.wait_for(2)
    f.choose(2, 0)
    f.tap(A)
    f.wait_for(3)
    for level in (1, 2, 3):
        f.choose(3, level - 1)
        f.tap(A)
        f.text_to(7, 8)
        p = f.wait_for(8)
        assert p[6] == level
        if not p[63]:
            f.chord((SELECT, Y))
        for _ in range(240):
            f.tick()
            if f.probe[63] and f.probe[60]:
                break
        else:
            raise AssertionError("Display-only POPAL fixture did not activate")
        initial = p[41]
        f.tick(30)
        assert f.probe[41] > initial
        f.tap(START)
        p = f.wait_for(9)
        frozen = p[40:52]
        f.tick(24)
        assert f.probe[40:52] == frozen
        f.language_from_pause(3 - language)
        f.choose(9, 0)
        f.tap(A)
        f.wait_for(8, language=3 - language)
        f.tick(24)
        f.tap(START)
        f.wait_for(9, language=3 - language)
        f.language_from_pause(language)
        f.choose(9, 2)
        f.tap(A)
        f.wait_for(5)
        f.tap(B)
        f.wait_for(9)
        f.tap(START)
        f.wait_for(8)
        f.tick(24)
        assert f.probe[41] > frozen[1]
        f.report.setdefault("level_memory", []).append(render.verify_memory(
            f, f.report["last_renderer"], f.target / "dok_nds_gameplay.elf"))
        assert any(row["language"] == 3 - language and row["level"] == level
                   for row in f.report["visual_alias"]), "Localized POPAL was never rendered after language switch"
        assert f.probe[31] == 1, "Full flow requires explicitly marked OUTRO fixtures"
        f.chord((SELECT, START))
        f.text_to(10, 3)
    f.tap(B)
    f.wait_for(2)
    f.choose(2, 4)
    f.tap(A)
    f.text_to(11, 12)
    expected = {(6, 0, 0, p) for p in range(3)}
    for row in f.report["rows"]:
        if row["state"] in (7, 10, 11):
            expected |= {(row["state"], row["level"], row["language"], p)
                         for p in range(row["pages"])}
    observed = {(r["state"], 0 if r["state"] == 6 else r["level"],
                 0 if r["state"] == 6 else r["language"], r["page"])
                for r in f.report["rows"]}
    assert expected <= observed, ("Missing complete text pages", expected - observed)
    assert len([a for a in f.report["audio"] if a["complete"]]) == 2
    f.report["all_original_pages_covered"] = True


def settings_bytes(language):
    raw = bytearray(b"DOKS\x01\x00\x10\x00" + bytes([language, 0, 0, 0]))
    value = 2166136261
    for code in raw:
        value = ((value ^ code) * 16777619) & 0xffffffff
    return bytes(raw) + struct.pack("<I", value)


def embedded_resources(target):
    return json.loads((target / 'build-manifest.json').read_text('utf8')).get('embedded_resources', False)


def write_fixture(image, settings=None, embedded=False):
    files = [] if embedded else [("assets.ndp", b"ASSETS  NDP", (ROOT / "build/nds/assets/dok-assets.ndspak").read_bytes()),
             ("ui.dat", b"UI      DAT", (ROOT / "build/nds/n19/ui.dat").read_bytes()),
             ("frontend.dat", b"FRONTENDDAT", (ROOT / "build/nds/n21/frontend.dat").read_bytes())]
    if settings is not None:
        files.append(("settings.dat", b"SETTINGSDAT", settings))
    image.parent.mkdir(parents=True, exist_ok=True)
    image.with_name(image.name + ".idx").unlink(missing_ok=True)
    fat.write_image(image, files, include_dok=not embedded or settings is not None)


def smoke_flow(frontend, mode, language):
    report = frontend.report
    target = frontend.target
    p = frontend.wait_for(1, language=language)
    report["boot_probe"] = list(p)
    draws = p[24]
    frontend.tick(24)
    assert frontend.probe[24] == draws, "Static title was redundantly redrawn"
    frontend.tap(A)
    frontend.wait_for(2)
    frontend.choose(2, 2)
    frontend.tap(A)
    frontend.wait_for(5)
    frontend.tap(B)
    frontend.wait_for(2)
    if mode == "world-smoke":
        frontend.choose(2, 0)
        frontend.tap(A)
        frontend.wait_for(3)
        frontend.tap(A)
        p = frontend.wait_for(7)
        for page in range(p[8]):
            frontend.wait_for(7, page=page)
            frontend.tap(A)
        p = frontend.wait_for(8)
        report["play_probe"] = list(p)
        frontend.tick(30)
        assert frontend.probe[41] > p[41], "Hero callbacks did not run"
        frontend.tap(START)
        p = frontend.wait_for(9)
        frozen = p[40:52]
        frontend.tick(24)
        assert frontend.probe[40:52] == frozen, "Pause changed gameplay clocks/state"
        frontend.tap(A, hold=20)
        frontend.wait_for(8)
        report["resume_probe"] = list(frontend.probe)
        report["memory"] = render.verify_memory(frontend, report["last_renderer"], target / "dok_nds_gameplay.elf")


def settings_flow(f, mode):
    p = f.wait_for(1, language=2 if mode == "restart" else 1)
    f.report["boot_probe"] = list(p)
    assert not any(p[40:52]), "Restart restored gameplay progress"
    assert p[27] == (1 if mode == "restart" else 2 if mode == "corrupt" else 0)
    if mode == "restart":
        f.tick(24)
        assert f.probe[3] == 1 and not any(f.probe[40:52])
        return
    f.tap(A)
    f.wait_for(2)
    f.choose(2, 1)
    f.tap(A)
    f.wait_for(4)
    f.choose(4, 1)
    f.tap(A)
    p = f.wait_for(2, language=2)
    assert p[29] >= 1 if mode == "readonly" else p[28] >= 1
    f.report["settings_probe"] = list(p)


def early_flow(f, button, phase):
    for _ in range(12000):
        f.tick()
        raw = struct.unpack("<64I", render.ram(f, f.address, 256))
        if raw[0] == 0x21504b44 and not raw[13]:
            break
    else:
        raise AssertionError("No boot sampling window observed")
    f.report["early_window_frame"] = f.frames
    f.tick(phase)
    f.joypad = 1 << (A if button == "A" else START)
    f.tick()
    f.joypad = 0
    p = f.wait_for(2)
    assert p[14] & (1 if button == "A" else 8), "VBlank did not latch the short boot input"
    f.tick(30)
    assert f.probe[3] == 2 and f.probe[5] == 0, "Boot input leaked into next screen"
    f.report["early_input"] = dict(button=button, phase=phase, video_frames=1, probe=list(p))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--core", type=Path, default=CORE)
    parser.add_argument("--language", type=int, choices=(1, 2), default=1)
    parser.add_argument("--mode", choices=("smoke", "world-smoke", "full", "settings", "restart", "corrupt", "readonly", "early"), default="smoke")
    parser.add_argument("--reuse-image", type=Path)
    parser.add_argument("--no-storage", action="store_true", help="Disable DLDI media; embedded cartridge resources must still boot")
    parser.add_argument("--phase", type=int, choices=range(8), default=0)
    parser.add_argument("--button", choices=("A", "Start"), default="A")
    args = parser.parse_args()
    target = args.target.absolute()
    assert target.is_relative_to(ROOT / "build/nds")
    assert not any(p.is_symlink() or p.is_junction() for p in (target, *target.parents) if p.exists())
    directory = target / ("frontend-" + args.mode)
    if args.no_storage:
        directory = target / ("frontend-" + args.mode + "-no-storage")
    if args.mode == "early":
        directory = target / f"frontend-early-{args.button}-{args.phase}"
    directory.mkdir(parents=True, exist_ok=True)
    frontend = None
    report = dict(task="N21", passed=False)
    try:
        assert transport.sha(args.core) == render.CORE_SHA
        frontend = FrontendObserver(args.core, target, directory, readonly=args.mode == "readonly")
        embedded = embedded_resources(target)
        assert not args.no_storage or embedded, 'Missing-media test requires embedded resources'
        if args.no_storage:
            frontend.options['melonds_homebrew_sdcard'] = 'disabled'
        # The separate restart process consumes this exact post-settings image.
        # Other modes clean their private image only after all post-close audits.
        frontend._keep_disk_for_restart = args.mode == "settings"
        report = frontend.report
        report["initial_language"] = args.language
        report['embedded_resources'] = embedded
        report['no_storage'] = args.no_storage
        image = frontend.save / "melonDS DS/dldi_sd_card.bin"
        if args.mode == "restart":
            assert args.reuse_image and args.reuse_image.resolve().is_relative_to(ROOT / "build/nds")
            image.parent.mkdir(parents=True, exist_ok=True)
            image.with_name(image.name + ".idx").unlink(missing_ok=True)
            shutil.copyfile(args.reuse_image, image)
        else:
            write_fixture(image, b"invalid settings" if args.mode == "corrupt" else
                          settings_bytes(2) if args.language == 2 else None, embedded=embedded)
        frontend.load(target / "dok_nds_gameplay.nds")
        transport.flush_native_stdio()
        before = volume_files(image)
        if embedded:
            assert not any(name in before for name in ('/DOK/ASSETS.NDP', '/DOK/UI.DAT', '/DOK/FRONTEND.DAT'))
            report['external_resource_files'] = []
        if args.mode == "full":
            full_flow(frontend, args.language)
        elif args.mode in ("settings", "restart", "corrupt", "readonly"):
            settings_flow(frontend, args.mode)
        elif args.mode == "early":
            early_flow(frontend, args.button, args.phase)
        else:
            smoke_flow(frontend, args.mode, args.language)
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
        if frontend and frontend.last_video:
            report["failure_images"] = frontend.capture("failure")
    finally:
        if frontend:
            frontend.close()
            transport.flush_native_stdio()
            if "before" in locals():
                try:
                    after = volume_files(image)
                    report["disk"] = audit_changes(before, after, ["/DOK/SETTINGS.DAT"])
                    if args.mode in ("settings", "corrupt", "restart"):
                        assert after["/DOK/SETTINGS.DAT"] == settings_bytes(2)
                    if args.mode in ("readonly", "restart", "early", "smoke", "world-smoke"):
                        assert not report["disk"]["changed_files"]
                except Exception:
                    report["passed"] = False
                    report["disk_error"] = traceback.format_exc()
        (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps({k: v for k, v in report.items() if k in ("passed", "error", "disk_error", "index8_checks", "rgb_checks")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
