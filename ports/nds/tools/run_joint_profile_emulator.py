"""N21P per-presentation CPU stage capture alongside the accumulated gates."""
import argparse
import json
from pathlib import Path
import struct
import sys

import run_map_emulator as maps
import run_renderer_emulator as render
import run_diag_emulator as transport

ROOT = Path(__file__).resolve().parents[3]
FIELDS = ("magic sequence abi case_id frame_id ordinal timer_hz level map_updates discovery_records "
          "upload_bytes interval_ticks prepare_ticks submit_ticks geometry_wait_ticks io_ticks "
          "entry_audio input simulation geometry sprites discovery overlay effects submit_including_wait "
          "wait_vblank stream_commit hud map post_map_audio telemetry cpu_excluding_wait "
          "map_snapshot map_raster map_postvisited map_buttons map_dma map_vram_audit map_publish map_total "
          "reserved56 reserved57 reserved58 reserved59 reserved60 reserved61 reserved62 reserved63").split()


class ProfileMapFrontend(maps.MapFrontend):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.profile_directory = self.target / "joint-profile"
        self.profile_directory.mkdir(exist_ok=True)
        self.profile_report = dict(task="N21P", passed=False, rows=[],
            observer_sha256=transport.sha(Path(__file__)),
            rom_sha256=transport.sha(self.target / "dok_nds_gameplay.nds"),
            elf_sha256=transport.sha(self.target / "dok_nds_gameplay.elf"),
            scope="Emulator CPU timing; discovery is a subset of sprites and detailed map stages are subsets of map")
        self.profile_seen = set()

    def observe_map(self):
        super().observe_map()
        p = struct.unpack("<64I", render.ram(self, self.map_address, 256))
        if p[0] != 0x20504b44 or p[2] & 1 or not p[60]:
            return
        pointer = p[60] + 64
        before = render.ram(self, pointer + 4, 4)
        raw = render.ram(self, pointer, 192)
        after = render.ram(self, pointer + 4, 4)
        values = struct.unpack("<48I", raw)
        if before != after or values[1] & 1 or values[0] != 0x21504b44:
            return
        row = dict(zip(FIELDS, values))
        assert row["abi"] == 1 and row["timer_hz"] == 33513982
        key = row["frame_id"]
        if key in self.profile_seen:
            return
        self.profile_seen.add(key)
        row["frontend_frame"] = self.frames
        self.profile_report["rows"].append(row)
        if len(self.profile_seen) % 100 == 0:
            (self.profile_directory / "progress.json").write_text(json.dumps(dict(
                rows=len(self.profile_seen), frame_id=key, case_id=row["case_id"])), encoding="utf8")

    def write_profile(self):
        (self.profile_directory / "result.json").write_text(json.dumps(self.profile_report, indent=2) + "\n", encoding="utf8")

    def close(self):
        if hasattr(self, "profile_report"):
            self.write_profile()
        super().close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--language", type=int, choices=(1, 2), default=1)
    parser.add_argument("--packet", type=Path, default=ROOT / "build/nds/n21p/scene.dat")
    args = parser.parse_args()
    original, argv = maps.MapFrontend, sys.argv
    try:
        maps.MapFrontend = ProfileMapFrontend
        sys.argv = [__file__, "--target", str(args.target), "--language", str(args.language), "--packet", str(args.packet)]
        code = maps.main()
        f = ProfileMapFrontend.active_instance
        accumulated = json.loads((args.target / "emulator/result.json").read_text("utf8"))
        f.profile_report["expected_presentations"] = accumulated["last_observation"]["total_frames"]
        f.profile_report["passed"] = code == 0 and bool(f.profile_report["rows"])
        f.write_profile()
        print(json.dumps(dict(profile_passed=f.profile_report["passed"], rows=len(f.profile_report["rows"]))))
        return code
    finally:
        maps.MapFrontend, sys.argv = original, argv


if __name__ == "__main__":
    raise SystemExit(main())
