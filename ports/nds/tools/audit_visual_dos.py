"""Independent read-only audit of all six N06 original DOS capture sessions."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT / "research/evidence/nds/n06/dos"
EMULATOR_SHA = "bbcef5754411fcab12fca5fbffb7e3d1b335063c3ae7510597492d7b6892f6c0"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def png_contents(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    offset = 8
    palette = None
    packed = bytearray()
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        tag = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        assert crc == zlib.crc32(tag + payload)
        if tag == b"IHDR":
            assert struct.unpack(">IIBBBBB", payload) == (320, 200, 8, 3, 0, 0, 0)
        elif tag == b"PLTE":
            palette = payload
        elif tag == b"IDAT":
            packed.extend(payload)
        elif tag == b"IEND":
            assert size == 0 and offset + 12 == len(data)
        offset += size + 12
    rows = zlib.decompress(packed)
    assert len(rows) == 321 * 200
    assert all(rows[y * 321] == 0 for y in range(200))
    indices = b"".join(rows[y * 321 + 1:(y + 1) * 321] for y in range(200))
    return indices, palette


def main():
    result = {"task": "N06", "status": "failed", "scope": "six popup sessions plus six natural-dismissal active PLAY sessions, native pixels/DAC/camera and immutable source checks", "sessions": [], "clean_sessions": []}
    (BASE / "manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    collector_sha = sha((ROOT / "ports/nds/tools/capture_visual_dos.py").read_bytes())
    for language in ("ru", "en"):
        source = ROOT / "originals/pkr" / ("RUS" if language == "ru" else "ENG")
        inventory = json.loads((ROOT / f"analysis/originals/inventory-{language}.json").read_text(encoding="utf8"))
        resources = json.loads((ROOT / f"analysis/originals/resources-{language}.json").read_text(encoding="utf8"))
        exe = "PKR.EXE" if language == "ru" else "PKE.EXE"
        current_hashes = {p.name: sha(p.read_bytes()) for p in source.iterdir() if p.is_file()}
        assert current_hashes[exe] == inventory["sha256"]
        archive = (source / exe.replace(".EXE", ".RES")).read_bytes()
        assert sha(archive) == inventory["resource"]["sha256"]
        palette_entries = [entry for entry in resources["entries"] if entry["kind"] == "vga_palette"]
        expected_poses = json.loads((ROOT / f"analysis/originals/worlds-{language}.json").read_text(encoding="utf8"))
        for level in (1, 2, 3):
            directory = BASE / language / f"level{level}"
            raw_report = (directory / "report.json").read_bytes()
            report = json.loads(raw_report)
            assert report["status"] == "passed" and report["originals_unchanged"] and report["copied_runtime_unchanged"]
            assert report["language"] == language and report["level"] == level
            assert report["source_file_hashes_before"] == report["source_file_hashes_after"] == current_hashes
            assert report["collector_sha256"] == collector_sha
            assert report["dosbox_sha256"] == sha(Path(report["dosbox"]).read_bytes()) == EMULATOR_SHA
            assert report["emulator"]["version"] == "0.85.1 (7567ec4)"
            assert all(report["emulator"]["features"][k] for k in ("memory", "input", "port_io"))
            assert "--noprimaryconf" in report["command"] and "--nolocalconf" in report["command"]
            assert (directory / "dosbox.conf").read_text(encoding="ascii") == report["configuration"]
            assert [event["key"] for event in report["events"]] == ["enter", "enter"] + ["down"] * (level - 1) + ["enter", "enter", "enter"]
            assert all(event["pressed_pic_ms"] == 100 and event["api_result"]["events_scheduled"] == 2 for event in report["events"])
            for runtime in (exe, exe.replace(".EXE", ".RES"), "RTM.EXE", "DPMI16BI.OVL"):
                assert sha((directory / "game" / runtime).read_bytes()) == current_hashes[runtime]
            session = {"language": language, "level": level, "observed_3d_phase": "native attempt popup still waiting for a fresh key, not yet active PLAY", "report": (directory / "report.json").relative_to(ROOT).as_posix(), "report_sha256": sha(raw_report), "frames": []}
            assert [frame["name"] for frame in report["frames"]] == ["boot", "story", "menu-default", "menu-selected", "bintro", "play-start"]
            for frame in report["frames"]:
                name = frame["name"]
                indices = (directory / (name + ".idx")).read_bytes()
                dac = (directory / (name + ".vga6")).read_bytes()
                palette = (directory / (name + ".rgb8")).read_bytes()
                api_rgb = (directory / (name + ".api.rgb")).read_bytes()
                data = (directory / (name + ".data.bin")).read_bytes()
                png = (directory / (name + ".png")).read_bytes()
                assert len(indices) == 64000 and len(dac) == len(palette) == 768 and len(api_rgb) == 192000 and len(data) == 65536
                assert max(dac) <= 63
                assert sha(indices) == frame["indices_sha256"] and sha(dac) == frame["vga6_sha256"]
                assert sha(palette) == frame["rgb8_sha256"] and sha(api_rgb) == frame["api_rgb_sha256"]
                assert sha(data) == frame["data_sha256"] and sha(png) == frame["png_sha256"]
                assert palette == bytes((component * 255 + 31) // 63 for component in dac)
                assert api_rgb == b"".join(palette[index * 3:index * 3 + 3] for index in indices)
                assert png_contents(png) == (indices, palette)
                assert all(frame[k] for k in ("framebuffer_two_reads_identical", "rendered_rgb_two_reads_identical", "dac_and_vga_indices_match_raw_rgb", "camera_and_hero_bracket_identical"))
                assert frame["state_before"]["effect_phase"] == frame["state_after"]["effect_phase"] == "unknown"
                source_matches = []
                for entry in palette_entries:
                    payload = archive[entry["offset"]:entry["offset"] + entry["size"]]
                    assert sha(payload) == entry["sha256"]
                    for subtract in (0, 1):
                        if dac == bytes(max(0, component - subtract) for component in payload):
                            source_matches.append({"resource": entry["name"], "source_index": entry["index"], "subtract_clamped": subtract, "raw_resource_sha256": entry["sha256"]})
                if name in ("bintro", "play-start"):
                    assert any(match["resource"] == f"GAMEP{level}" and match["subtract_clamped"] == 1 for match in source_matches)
                if name == "play-start":
                    state = frame["state_before"]
                    after = frame["state_after"]
                    pose = [expected_poses[level - 1]["player"][k] for k in ("x", "y", "z")]
                    assert state["camera_xyz"] == state["hero"]["xyz"] == after["camera_xyz"] == after["hero"]["xyz"] == pose
                    assert state["hero"]["heading"] == after["hero"]["heading"] == 90
                    assert state["hero"]["health_energy"] == [100, 0] and state["hero"]["lives_score"] == [3, 0]
                    camera = 0xaa16 + (0 if language == "ru" else 0xb0)
                    assert struct.unpack_from("<h", data, 0x1fae if language == "ru" else 0x205e)[0] == 1
                    assert state["scheduler_elapsed"] == after["scheduler_elapsed"]
                    session["attempt_counter"] = 1
                    assert struct.unpack_from("<i", data, camera)[0] == state["D"] == 200
                    assert list(struct.unpack_from("<hhhh", data, camera + 4)) == state["width_height_cx_cy"] == [320, 161, 159, 79]
                    assert list(struct.unpack_from("<iiii", data, camera + 12)) == state["clip"] == [0, 0, 319, 159]
                    assert list(struct.unpack_from("<iii", data, camera + 48)) == pose
                    assert state["sin_cos"] == [32768, 0] and state["viewport_scale"] == 8 and state["focal_q12"] == 4096
                    session["play_camera"] = state
                session["frames"].append({"name": name, "indices_sha256": sha(indices), "vga6_sha256": sha(dac), "rgb8_sha256": sha(palette), "api_rgb_sha256": sha(api_rgb), "png_sha256": sha(png), "palette_source_matches": source_matches, "capture_wall_seconds": frame["capture_wall_seconds"]})
            if level > 1:
                assert (directory / "menu-default.idx").read_bytes() != (directory / "menu-selected.idx").read_bytes()
            result["sessions"].append(session)
    clean_collector_sha = sha((ROOT / "ports/nds/tools/capture_clean_visual_dos.py").read_bytes())
    for language in ("ru", "en"):
        source = ROOT / "originals/pkr" / ("RUS" if language == "ru" else "ENG")
        exe = "PKR.EXE" if language == "ru" else "PKE.EXE"
        source_hashes = {p.name: sha(p.read_bytes()) for p in source.iterdir() if p.is_file()}
        for level in (1, 2, 3):
            directory = BASE / language / f"level{level}-clean"
            raw_report = (directory / "report.json").read_bytes()
            report = json.loads(raw_report)
            assert report["status"] == "passed" and report["originals_unchanged"] and report["copied_runtime_unchanged"]
            assert report["collector_sha256"] == clean_collector_sha and report["transport_sha256"] == collector_sha
            assert report["source_file_hashes_before"] == report["source_file_hashes_after"] == source_hashes
            assert report["source_report_sha256"] == sha((BASE / language / f"level{level}/report.json").read_bytes())
            assert report["dosbox_sha256"] == EMULATOR_SHA and report["emulator"]["version"] == "0.85.1 (7567ec4)"
            assert [event["key"] for event in report["events"]] == ["enter", "enter"] + ["down"] * (level - 1) + ["enter"] * 4
            assert all(event["pressed_pic_ms"] == 100 and event["result"]["events_scheduled"] == 2 for event in report["events"])
            assert (directory / "dosbox.conf").read_text(encoding="ascii") == report["configuration"]
            for runtime in (exe, exe.replace(".EXE", ".RES"), "RTM.EXE", "DPMI16BI.OVL"):
                assert sha((directory / "game" / runtime).read_bytes()) == source_hashes[runtime]
            offset = 0x1fae if language == "ru" else 0x205e
            popup_data = (directory / "attempt-popup.data.bin").read_bytes()
            assert struct.unpack_from("<h", popup_data, offset)[0] == report["popup"]["attempt_counter"] == 1
            assert sha(popup_data) == report["popup"]["data_sha256"]
            popup_files = {}
            for extension, key in (("idx", "indices_sha256"), ("vga6", "vga6_sha256"), ("rgb8", "rgb8_sha256"), ("api.rgb", "api_rgb_sha256"), ("png", "png_sha256")):
                payload = (directory / ("attempt-popup." + extension)).read_bytes()
                assert sha(payload) == report["popup"][key]
                popup_files[extension] = payload
            assert len(popup_files["idx"]) == 64000 and len(popup_files["vga6"]) == 768 and len(popup_files["api.rgb"]) == 192000
            assert popup_files["rgb8"] == bytes((v * 255 + 31) // 63 for v in popup_files["vga6"])
            assert popup_files["api.rgb"] == b"".join(popup_files["rgb8"][v * 3:v * 3 + 3] for v in popup_files["idx"])
            assert png_contents(popup_files["png"]) == (popup_files["idx"], popup_files["rgb8"])
            assert all(report["popup"][k] for k in ("framebuffer_two_reads_identical", "rendered_rgb_two_reads_identical", "dac_and_vga_indices_match_raw_rgb", "camera_and_hero_bracket_identical"))
            clean = report["clean"]
            assert clean["attempt_counter"] == 2 and clean["effect_phase"] == "unknown"
            assert all(clean[k] for k in ("dac_before_after_identical", "two_vga_frames_and_two_rgb_frames_identical", "dac_and_vga_indices_match_raw_rgb"))
            contents = {}
            for extension, metadata in clean["files"].items():
                payload = (directory / ("play-clean." + extension)).read_bytes()
                assert len(payload) == metadata["bytes"] and sha(payload) == metadata["sha256"]
                contents[extension] = payload
            assert len(contents["idx"]) == 64000 and len(contents["vga6"]) == len(contents["rgb8"]) == 768 and len(contents["api.rgb"]) == 192000 and len(contents["data.bin"]) == 65536
            assert struct.unpack_from("<h", contents["data.bin"], offset)[0] == 2
            assert contents["rgb8"] == bytes((v * 255 + 31) // 63 for v in contents["vga6"])
            assert contents["api.rgb"] == b"".join(contents["rgb8"][v * 3:v * 3 + 3] for v in contents["idx"])
            png = (directory / "play-clean.png").read_bytes()
            assert sha(png) == clean["png_sha256"] and png_contents(png) == (contents["idx"], contents["rgb8"])
            before, after = clean["state_before"], clean["state_after"]
            assert before["hero"] == after["hero"] and before["hero"]["heading"] == 90
            assert before["camera_xyz"] == after["camera_xyz"] == before["hero"]["xyz"] == report["popup"]["state_before"]["camera_xyz"]
            assert before["D"] == after["D"] == 200 and before["clip"] == after["clip"] == [0, 0, 319, 159]
            assert before["width_height_cx_cy"] == after["width_height_cx_cy"] == [320, 161, 159, 79]
            assert before["viewport_scale"] == after["viewport_scale"] == 8 and before["focal_q12"] == after["focal_q12"] == 4096
            camera = 0xaa16 + (0 if language == "ru" else 0xb0)
            assert list(struct.unpack_from("<iii", contents["data.bin"], camera + 48)) == before["hero"]["xyz"]
            tick_delta = (clean["scheduler_after_dac"]["scheduler_elapsed"] - clean["scheduler_before_dac"]["scheduler_elapsed"]) & 0xffffffff
            assert tick_delta > 0
            assert (directory / "attempt-popup.idx").read_bytes() != contents["idx"]
            result["clean_sessions"].append({"language": language, "level": level, "report": (directory / "report.json").relative_to(ROOT).as_posix(), "report_sha256": sha(raw_report), "phase": clean["phase"], "attempt_counter_before_after": [1, 2], "popup_indices_sha256": report["popup"]["indices_sha256"], "scheduler_elapsed_delta": tick_delta, "capture_wall_seconds": clean["capture_wall_seconds"], "files": clean["files"], "png_sha256": clean["png_sha256"], "camera": before})
    result.update(status="passed", session_count=12, popup_session_count=6, clean_session_count=6, frame_count=48, collector_sha256=collector_sha, clean_collector_sha256=clean_collector_sha, audit_sha256=sha(Path(__file__).read_bytes()), real_hardware_tested=False)
    (BASE / "manifest.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(json.dumps({k: result[k] for k in ("status", "session_count", "frame_count")}))


if __name__ == "__main__":
    main()
