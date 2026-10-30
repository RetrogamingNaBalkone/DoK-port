"""N18 read-only PCM observer layered over the accumulated gameplay gate.

Only normal libretro joypad callbacks advance the ROM. The observer reads
exported telemetry, saves actual core audio, and never writes emulated RAM.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import run_diag_emulator as transport
import run_gameplay_emulator as gameplay
import run_renderer_emulator as render
from check_audio_capture import original_clip, compare_capture

ROOT = Path(__file__).resolve().parents[3]
BASE_FRONTEND = transport.Frontend


class AudioFrontend(BASE_FRONTEND):
    """Observe the existing runner without altering its input or test logic."""
    active_instance = None
    target = None
    language = 1

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        AudioFrontend.active_instance = self
        self.audio_directory = self.target / "audio"
        self.audio_directory.mkdir(parents=True, exist_ok=True)
        self.probe_address, size = transport.elf_symbol(self.target / "dok_nds_gameplay.elf", "g_nds_audio_probe")
        if size != 256:
            raise ValueError("N18 audio probe ABI size changed")
        self.audio_report = dict(task="N18", passed=False, catalog=[], timeline=[],
                                 pid=os.getpid(), language=self.language,
                                 rom_sha256=transport.sha(self.target/"dok_nds_gameplay.nds"),
                                 elf_sha256=transport.sha(self.target/"dok_nds_gameplay.elf"),
                                 access="read-only telemetry and actual libretro PCM",
                                 scope="Emulated DS sound under the accumulated N17P runtime; physical DS, subjective listening and Sound Blaster equivalence are separate")
        self.audio_segment = None
        self.audio_segment_pcm = bytearray()
        self.live_pcm = bytearray()
        self.live_title_seen = False
        self.live_title_done = False
        self.audio_recent = bytearray()
        self.audio_last_key = None
        self.audio_sample_frames = 0
        self.audio_closed = False
        self._native_core = self.core
        self.core = CoreObserver(self)
        self.write_audio_report()

    def load(self, path):
        super().load(path)
        transport.flush_native_stdio()
        self.audio_fat = self.save/"melonDS DS/dldi_sd_card.bin"
        self.audio_report["fat_after_load_sha256"] = transport.sha(self.audio_fat)

    def audio(self, left, right):
        self.pcm.extend(struct.pack("<hh", left, right))

    def finish_segment(self):
        if self.audio_segment is None:
            return
        segment = self.audio_segment
        path = self.audio_directory / f"catalog-{segment['phase']}-{segment['index']:02}-{segment['resource_index']:03}.wav"
        transport.save_wav(path, self.audio_segment_pcm, self.av.timing.sample_rate)
        segment["wav"] = path.relative_to(ROOT).as_posix()
        # DS sound timer consumes one sample per twice-period bus clock cycles.
        # Rate is taken from the ARM7 report, never from nominal RES metadata.
        clip = original_clip(self.language, segment["resource_index"])
        if not segment["timer_period"]:
            raise AssertionError("Missing actual ARM7 timer period")
        effective_rate = 33513982 / (2 * segment["timer_period"])
        if segment["phase"] == 3:
            # The ROM intentionally stops a streaming loop after 131072
            # consumed samples. Seven complete copies of odd-length clip89
            # fit before that boundary; no word-aligned padding is allowed.
            segment["waveform"] = compare_capture(path, clip, effective_rate, repeats=7,
                                                  output_rate=self.av.timing.sample_rate)
        else:
            segment["waveform"] = compare_capture(path, clip, effective_rate,
                                                  output_rate=self.av.timing.sample_rate)
        self.audio_report["catalog"].append(segment)
        print(f"audio {segment['index']}: {segment['resource_index']} r={segment['waveform']['whole_coefficient']}", flush=True)
        self.audio_segment = None
        self.audio_segment_pcm.clear()

    def observe_audio(self):
        # Discard unneeded whole-run in-memory audio. Current recording and a
        # short pre-roll retain actual bytes around asynchronous IPC starts.
        pcm = bytes(self.pcm)
        self.pcm.clear()
        self.audio_sample_frames += len(pcm)//4
        self.audio_recent.extend(pcm)
        keep = max(4, round(self.av.timing.sample_rate*.05)*4)
        if len(self.audio_recent) > keep:
            del self.audio_recent[:-keep]
        row = struct.unpack("<64I", render.ram(self, self.probe_address, 256))
        if row[0] != 0x18504b44 or row[1] != 1 or row[63] & 1:
            if self.audio_segment is not None:
                self.audio_segment_pcm.extend(pcm)
            return
        if row[8] or row[9] or row[22]:
            self.audio_report["fault_probe"] = list(row)
            raise AssertionError("Actual audio underrun, stale command or runtime fault")
        # A request publishes catalog_index before ARM7 starts. Wait for its
        # matching acknowledged identity, retaining pre-roll for that frame.
        key = (row[2], row[3], row[4], row[5], row[8], row[9], row[14], row[16], row[17], row[22])
        if key != self.audio_last_key:
            self.audio_report["timeline"].append(dict(frontend_frame=self.frames+1,
                output_sample_frames=self.audio_sample_frames, probe=list(row)))
            self.audio_last_key = key
        if row[2] == 2 and not self.live_title_done:
            if row[4] and row[5] == 0x8000006e:
                if not self.live_title_seen:
                    self.live_pcm.extend(self.audio_recent)
                    self.live_title_seen = True
                else:
                    self.live_pcm.extend(pcm)
            elif self.live_title_seen:
                self.live_pcm.extend(pcm)
                self.live_title_done = True
                live_path = self.audio_directory / "live-title.wav"
                transport.save_wav(live_path, self.live_pcm, self.av.timing.sample_rate)
                self.audio_report["live_title"] = dict(wav=live_path.relative_to(ROOT).as_posix(),
                    output_frames=len(self.live_pcm)//4, end_probe=list(row),
                    scope="Actual title PCM while the moving N17P world renders and services its original sound triggers; may be interrupted by source admission/stop policy")
                self.live_pcm.clear()
        if self.audio_segment is not None:
            self.audio_segment_pcm.extend(pcm)
            if row[2] == self.audio_segment["phase"] and row[3] == self.audio_segment["index"]:
                if row[12]:
                    self.audio_segment["timer_period"] = row[12]
                self.audio_segment["last_probe"] = list(row)
            if row[2] != self.audio_segment["phase"] or row[3] != self.audio_segment["index"]:
                self.finish_segment()
        if row[2] in (1, 3) and row[4] and row[5] & 0x80000000 and (
                self.audio_segment is None or row[3] != self.audio_segment["index"]):
            if any(r["phase"] == row[2] and r["index"] == row[3] for r in self.audio_report["catalog"]):
                raise AssertionError("Catalog stage restarted unexpectedly")
            self.audio_segment = dict(phase=row[2], index=row[3], resource_index=row[5] & 0x7fffffff,
                                      timer_period=row[12], start_probe=list(row),
                                      start_frontend_frame=self.frames+1)
            self.audio_segment_pcm.extend(self.audio_recent)
        self.audio_report["last_probe"] = list(row)
        if row[2] == 2 and not self.audio_report.get("catalog_completed"):
            self.audio_report["catalog_completed"] = True
            self.audio_report["catalog_frontend_frame"] = self.frames+1
            if len(self.audio_report["catalog"]) != 24:
                raise AssertionError("Actual catalog did not cover all 23 once clips and the odd loop")
            # Persist before the much longer accumulated rendering run.
            self.write_audio_report()

    def write_audio_report(self):
        self.audio_report["core_options"] = {k: v.decode() for k, v in self.variables.items()}
        self.audio_report["output_sample_frames"] = self.audio_sample_frames
        if hasattr(self, "av"):
            self.audio_report["output_sample_rate"] = self.av.timing.sample_rate
        (self.audio_directory / "result.json").write_text(json.dumps(self.audio_report, ensure_ascii=False, indent=2)+"\n", "utf8")

    def close(self):
        if self.audio_closed:
            return
        self.audio_closed = True
        try:
            self.finish_segment()
            self.write_audio_report()
        finally:
            super().close()
            if hasattr(self, "audio_fat"):
                transport.flush_native_stdio()
                self.audio_report["fat_after_unload_sha256"] = transport.sha(self.audio_fat)
                self.audio_report["fat_unchanged_after_unload"] = self.audio_report["fat_after_unload_sha256"] == self.audio_report["fat_after_load_sha256"]
                self.write_audio_report()


class CoreObserver:
    def __init__(self, frontend):
        self.frontend = frontend

    def __getattr__(self, name):
        return getattr(self.frontend._native_core, name)

    def retro_run(self):
        self.frontend._native_core.retro_run()
        try:
            self.frontend.observe_audio()
        except Exception:
            self.frontend.audio_report["error"] = traceback.format_exc()
            self.frontend.write_audio_report()
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=Path, required=True)
    parser.add_argument("--packet", type=Path, default=ROOT / "build/nds/n18/scene.dat")
    parser.add_argument("--core", type=Path, default=gameplay.CORE)
    parser.add_argument("--language", type=int, choices=(1, 2), default=1)
    parser.add_argument("--max-frontend-frames", type=int, default=150000)
    args = parser.parse_args()
    AudioFrontend.target = args.target.absolute()
    AudioFrontend.language = args.language
    original_argv = sys.argv
    original_frontend = transport.Frontend
    try:
        transport.Frontend = AudioFrontend
        sys.argv = [str(Path(gameplay.__file__)), "--target", str(args.target),
                    "--packet", str(args.packet), "--core", str(args.core),
                    "--max-frontend-frames", str(args.max_frontend_frames)]
        code = gameplay.main()
        frontend = AudioFrontend.active_instance
        report = frontend.audio_report if frontend else {}
        if frontend:
            failures = []
            for row in report.get("catalog", []):
                match = row["waveform"]
                expected_period = (33513982//2+match["nominal_rate"]//2)//match["nominal_rate"]
                exact_eos = row["phase"] == 3 or row.get("last_probe", [0]*8)[7] == match["source_samples"]
                if not match["complete_source_covered"] or match["whole_coefficient"] is None or match["whole_coefficient"] < .95 or row["timer_period"] != expected_period or not exact_eos:
                    failures.append(row["resource_index"])
            report["waveform_failures"] = failures
            report["passed"] = code == 0 and report.get("catalog_completed", False) and not failures and not report.get("error") and report.get("fat_unchanged_after_unload", False)
            frontend.write_audio_report()
        return 0 if report.get("passed") else 1
    finally:
        sys.argv = original_argv
        transport.Frontend = original_frontend


if __name__ == "__main__":
    raise SystemExit(main())
