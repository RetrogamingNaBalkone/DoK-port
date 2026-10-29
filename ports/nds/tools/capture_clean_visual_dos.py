"""N06 natural dismissal of the original attempt popup, then active PLAY capture.

Uses the frozen DOS transport/format helpers; no game RAM/code/pose writes.
Existing popup captures remain separate immutable observations.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
import time
import re
import urllib.request

import capture_visual_dos as base


def counter(memory, language, data_base):
    return struct.unpack_from("<h", memory, data_base + (0x1fae if language == "ru" else 0x205e))[0]


def clean_capture(api, directory, language, data_base, pose):
    started = time.monotonic()
    whole_before = api.memory()
    dac_before = api.dac()
    palette = bytes((v * 255 + 31) // 63 for v in dac_before)
    for attempt in range(100):
        memory_a = api.memory()
        rgb_a = api.raw()
        rgb_b = api.raw()
        memory_b = api.memory()
        pixels = memory_a[0xa0000:0xafa00]
        expected_rgb = b"".join(palette[v * 3:v * 3 + 3] for v in pixels)
        state_a = base.native_state(memory_a, language, data_base, pose)
        state_b = base.native_state(memory_b, language, data_base, pose)
        if pixels == memory_b[0xa0000:0xafa00] and rgb_a == rgb_b == expected_rgb and state_a["camera_xyz"] == state_b["camera_xyz"] == pose:
            break
    else:
        raise RuntimeError("No complete stable native frame found during active PLAY")
    dac_after = api.dac()
    whole_after = api.memory()
    if dac_before != dac_after:
        raise RuntimeError("DAC changed across the active PLAY capture")
    if counter(memory_a, language, data_base) != 2 or counter(memory_b, language, data_base) != 2:
        raise RuntimeError("Attempt popup did not return before clean frame")
    if state_a.get("hero") != state_b.get("hero") or state_a["hero"]["xyz"] != pose:
        raise RuntimeError("Hero changed during active PLAY frame bracket")
    start_state = base.native_state(whole_before, language, data_base, pose)
    end_state = base.native_state(whole_after, language, data_base, pose)
    if start_state["scheduler_elapsed"] == end_state["scheduler_elapsed"]:
        raise RuntimeError("Scheduler did not advance during clean PLAY observation")
    name = "play-clean"
    data = memory_a[data_base:data_base + 65536]
    files = {"idx": pixels, "vga6": dac_before, "rgb8": palette, "api.rgb": rgb_a, "data.bin": data}
    for extension, contents in files.items():
        (directory / (name + "." + extension)).write_bytes(contents)
    base.png(directory / (name + ".png"), 320, 200, pixels, palette)
    return {"name": name, "phase": "active PLAY after native attempt-popup return", "attempt_counter": 2,
        "attempt_counter_ds_offset": "0x1FAE" if language == "ru" else "0x205E",
        "state_before": state_a, "state_after": state_b,
        "scheduler_before_dac": start_state, "scheduler_after_dac": end_state,
        "stable_frame_attempt": attempt + 1, "capture_wall_seconds": time.monotonic() - started,
        "dac_before_after_identical": True, "two_vga_frames_and_two_rgb_frames_identical": True,
        "dac_and_vga_indices_match_raw_rgb": True, "effect_phase": "unknown",
        "consistency": "Frame equality is a short atomic-endpoint bracket inside a longer identical-DAC bracket; guest is running, scheduler advances, no globally paused snapshot claimed.",
        "files": {extension: {"bytes": len(contents), "sha256": base.digest(contents)} for extension, contents in files.items()},
        "png_sha256": base.digest((directory / (name + ".png")).read_bytes())}


def trial(language, level, output, dosbox, port):
    output = output.resolve()
    if not output.is_relative_to((base.ROOT / "research/evidence/nds/n06/dos").resolve()):
        raise ValueError("Output outside N06 DOS evidence")
    try:
        urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
    except OSError:
        pass
    else:
        raise RuntimeError("Refusing occupied HTTP port")
    output.mkdir(parents=True, exist_ok=False)
    previous_path = base.ROOT / f"research/evidence/nds/n06/dos/{language}/level{level}/report.json"
    previous = json.loads(previous_path.read_text(encoding="utf8"))
    source = base.ROOT / "originals/pkr" / ("RUS" if language == "ru" else "ENG")
    before = {p.name: base.digest(p.read_bytes()) for p in source.iterdir() if p.is_file()}
    if before != previous["source_file_hashes_before"]:
        raise ValueError("Original sources differ from verified popup session")
    game = output / "game"
    shutil.copytree(source, game)
    old_game = previous_path.parent / "game"
    config = previous["configuration"].replace(str(old_game), str(game))
    config = re.sub(r"(?m)^webserver_port = \d+$", f"webserver_port = {port}", config)
    assert str(game) in config and str(old_game) not in config
    config_path = output / "dosbox.conf"
    config_path.write_text(config, encoding="ascii")
    token = secrets.token_hex(32)
    api = base.Api(port, token)
    pose = previous["frames"][-1]["state_before"]["hero"]["xyz"]
    report = {"status": "failed", "language": language, "level": level,
        "collector_sha256": base.digest(Path(__file__).read_bytes()),
        "transport_sha256": base.digest(Path(base.__file__).read_bytes()),
        "source_report_sha256": base.digest(previous_path.read_bytes()),
        "source_file_hashes_before": before, "configuration": config,
        "dosbox_sha256": base.digest(dosbox.read_bytes()), "events": [],
        "input_method": "native i8042 make/break Enter/Down only; no guest-memory/code/pose writes",
        "command": [str(dosbox), "--noprimaryconf", "--nolocalconf", "-conf", str(config_path), "-noconsole"]}
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DOSBOX_API_TOKEN=token)
    with (output / "stdout.log").open("wb") as out, (output / "stderr.log").open("wb") as err:
        proc = subprocess.Popen(report["command"], cwd=output, env=env, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            report["emulator"] = base.wait_for(lambda: api.get("dosbox/info"))
            data_base = base.wait_for(lambda: base.base_of(api.memory(), language))
            def key(name):
                result = api.key(name)
                report["events"].append({"key": name, "pressed_pic_ms": 100, "result": result})
                time.sleep(1.5)
            time.sleep(1.5)
            key("enter")
            key("enter")
            for _ in range(level - 1):
                key("down")
            key("enter")
            key("enter")
            key("enter")
            popup = base.capture(api, output, "attempt-popup", language, data_base, pose, require_play=True)
            popup_data = (output / "attempt-popup.data.bin").read_bytes()
            assert struct.unpack_from("<h", popup_data, 0x1fae if language == "ru" else 0x205e)[0] == 1
            popup["phase"] = "first 3D frame while native attempt popup is waiting for fresh key"
            popup["attempt_counter"] = 1
            report["popup"] = popup
            key("enter")
            base.wait_for(lambda: counter(api.memory(), language, data_base) == 2)
            report["clean"] = clean_capture(api, output, language, data_base, pose)
            report["status"] = "passed"
        except Exception as error:
            report["error"] = repr(error)
            raise
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            after = {p.name: base.digest(p.read_bytes()) for p in source.iterdir() if p.is_file()}
            copied = {p.name: base.digest(p.read_bytes()) for p in game.iterdir() if p.is_file()}
            exe = "PKR.EXE" if language == "ru" else "PKE.EXE"
            report["originals_unchanged"] = before == after
            report["source_file_hashes_after"] = after
            report["copied_runtime_unchanged"] = all(copied.get(k) == before[k] for k in (exe, exe.replace(".EXE", ".RES"), "RTM.EXE", "DPMI16BI.OVL"))
            if not report["originals_unchanged"] or not report["copied_runtime_unchanged"]:
                report["status"] = "failed"
            (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(json.dumps({"status": report["status"], "language": language, "level": level, "clean": report.get("clean", {}).get("files", {}).get("idx")}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("language", choices=("ru", "en"))
    parser.add_argument("level", type=int, choices=(1, 2, 3))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dosbox", type=Path, default=base.DEFAULT_DOSBOX)
    parser.add_argument("--port", type=int, default=8147)
    args = parser.parse_args()
    trial(args.language, args.level, args.output, args.dosbox, args.port)


if __name__ == "__main__":
    main()
