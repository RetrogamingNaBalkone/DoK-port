"""N21P independent HUD/native-x86 map restore with release audit disabled.

Derived from accepted N21 restore observer SHA256 3af2f3719a597aff851cb71b16b4fa56941d5fb5fd3c5d0d530c58336dc44d2a.
Only the new HUD adapter, task/directory and provenance fields differ.
Original INDEX8 and actual LCD RGB comparisons remain mandatory.
"""
import argparse
import json
from pathlib import Path
import struct
import traceback

import run_frontend_emulator as front
import run_hud_emulator as hud
from joint_hud_oracle import observe_joint_hud
import run_map_emulator as maps
from frontend_fat_oracle import volume_files, audit_changes
from hud_pixel_oracle import OriginalHudArt
from map.original_map import original_map, original_map_result


class RestoreObserver(front.FrontendObserver):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.hud_art = OriginalHudArt()
        self.messages = hud.original_messages(self.hud_art)
        self.ui_address, _ = front.transport.elf_symbol(self.target / "dok_nds_gameplay.elf", "g_nds_ui_probe")
        self.map_address, _ = front.transport.elf_symbol(self.target / "dok_nds_gameplay.elf", "g_nds_map_probe")
        self.hud_report = dict(rows=[], pixel_checks=0, hardware_pixel_checks=0)
        self.hud_seen, self.hud_capture_seen = set(), set()
        self.hud_stable, self.hud_stability = None, 0
        self.map_report = dict(rows=[], index8_checks=0, rgb_checks=0)
        self.map_seen, self.map_snapshot_seen = set(), set()
        self.map_stable, self.map_stability = None, 0
        self.hud_directory = self.directory / "hud"
        self.map_directory = self.directory / "map"
        self.hud_directory.mkdir(exist_ok=True)
        self.map_directory.mkdir(exist_ok=True)
        self.plan_done, self.plan_index = False, 0

    native_draw = staticmethod(original_map)
    native_result = staticmethod(original_map_result)

    def tick(self, count=1):
        for _ in range(count):
            super().tick()
            p = struct.unpack("<64I", front.render.ram(self, self.address, 256))
            if p[0] != 0x21504b44 or p[2] & 1 or p[3] != 8:
                continue
            u = struct.unpack("<64I", front.render.ram(self, self.ui_address, 256))
            m = struct.unpack("<64I", front.render.ram(self, self.map_address, 256))
            if u[0] != 0x19504b44 or m[0] != 0x20504b44 or u[2] & 1 or m[2] & 1:
                continue
            if (u[3], u[4], m[3]) != (p[4], p[6], p[6]):
                continue
            art = self.art
            try:
                self.art = self.hud_art
                observe_joint_hud(self)
                maps.MapFrontend.observe_map_pixels(self, m)
            finally:
                self.art = art
        return self.probe

    def write_hud_report(self):
        pass

    def require_restore(self, level, language):
        self.hud_seen.clear()
        self.map_seen.clear()
        start_h, start_m = len(self.hud_report["rows"]), len(self.map_report["rows"])
        for _ in range(360):
            self.tick()
            if len(self.hud_report["rows"]) > start_h and len(self.map_report["rows"]) > start_m:
                self.report.setdefault("restore_points", []).append(dict(level=level, language=language,
                    frame=self.frames, images=self.capture(f"restored-{level}-{language}")))
                return
        raise AssertionError(("No original HUD/map scanout verified after return", level, language))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--language", type=int, choices=(1, 2), default=1)
    args = parser.parse_args()
    target = args.target.absolute()
    assert target.is_relative_to(front.ROOT / "build/nds")
    directory = target / "joint-restore"
    directory.mkdir(parents=True, exist_ok=True)
    f = None
    report = dict(task="N21P", passed=False)
    try:
        f = RestoreObserver(front.CORE, target, directory)
        report = f.report
        report["restore_observer_sha256"] = front.transport.sha(Path(__file__))
        report["hud_adapter_sha256"] = front.transport.sha(Path(__file__).with_name("joint_hud_oracle.py"))
        report["initial_language"] = args.language
        image = f.save / "melonDS DS/dldi_sd_card.bin"
        front.write_fixture(image, front.settings_bytes(2) if args.language == 2 else None)
        f.load(target / "dok_nds_gameplay.nds")
        front.transport.flush_native_stdio()
        before = volume_files(image)
        f.wait_for(1)
        f.tap(front.A)
        f.wait_for(2)
        for level in (1, 2, 3):
            f.choose(2, 0)
            f.tap(front.A)
            f.wait_for(3)
            f.choose(3, level - 1)
            f.tap(front.A)
            f.text_to(7, 8)
            language = f.probe[4]
            f.require_restore(level, language)
            f.tap(front.START)
            f.wait_for(9)
            f.language_from_pause(3 - language)
            f.choose(9, 0)
            f.tap(front.A)
            f.wait_for(8)
            f.require_restore(level, 3 - language)
            f.tap(front.START)
            f.wait_for(9)
            f.choose(9, 3)
            f.tap(front.A)
            f.wait_for(2)
        assert {(r["level"], r["language"]) for r in report["restore_points"]} == {
            (level, language) for level in (1, 2, 3) for language in (1, 2)}
        report["hud_restore"] = f.hud_report
        report["map_restore"] = f.map_report
        report["passed"] = True
    except Exception:
        report["error"] = traceback.format_exc()
        if f and f.last_video:
            report["failure_images"] = f.capture("failure")
    finally:
        if f:
            f.close()
            front.transport.flush_native_stdio()
            if "before" in locals():
                try:
                    report["disk"] = audit_changes(before, volume_files(image), ["/DOK/SETTINGS.DAT"])
                except Exception:
                    report["passed"] = False
                    report["disk_error"] = traceback.format_exc()
        (directory / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps({k: v for k, v in report.items() if k in ("passed", "error", "disk_error")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
