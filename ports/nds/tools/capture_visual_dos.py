"""N06 genuine original DOS captures via the independent DOSBox Automation API.

No GBA implementation imports, patches, pose writes, or original file writes.
The only I/O write selects the VGA DAC read index; input uses i8042 replay.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
import time
import urllib.request
import zlib

ROOT = Path(__file__).resolve().parents[3]
ANCHOR = b"OVERFLOW\0UNDERFLOW\0TLOSS\0PLOSS"
DEFAULT_DOSBOX = Path(os.environ["LOCALAPPDATA"]) / "Programs/DOSBox-Automation/0.85.1/dosbox-automation-0.85.1/dosbox.exe"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def png(path, width, height, indices, palette):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    rows = b"".join(b"\0" + indices[y * width:(y + 1) * width] for y in range(height))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)) + chunk(b"PLTE", palette) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


class Api:
    def __init__(self, port, token):
        self.url = f"http://127.0.0.1:{port}/api/v1/"
        self.token = token

    def request(self, path, data=None, method="GET"):
        headers = {"Authorization": "Bearer " + self.token}
        if data is not None:
            headers["Content-Type"] = "application/json"
            data = json.dumps(data).encode()
        req = urllib.request.Request(self.url + path, data=data, method=method, headers=headers)
        with urllib.request.urlopen(req, timeout=20) as response:
            return response.read()

    def get(self, path):
        return json.loads(self.request(path))

    def memory(self):
        return self.request("memory/0/16777216")

    def key(self, name):
        result = json.loads(self.request("input/sequence", {"events": [
            {"type": "key", "key": "KBD_" + name, "pressed": True, "t": 0},
            {"type": "key", "key": "KBD_" + name, "pressed": False, "t": 100},
        ]}, "POST"))
        time.sleep(.3)
        return result

    def raw(self):
        raw = self.request("video/frame?format=raw&mode=raw")
        width, height, pitch, fmt, count = struct.unpack_from("<IIiBH", raw)
        if (width, height, fmt, count) != (320, 200, 32, 0) or pitch < width * 4:
            raise ValueError(f"Unexpected raw framebuffer: {(width, height, pitch, fmt, count)} length={len(raw)} header={raw[:48].hex()}")
        if len(raw) != 15 + pitch * height:
            raise ValueError("Unexpected raw framebuffer length")
        pixels = raw[15:]
        rgb = bytearray()
        for y in range(height):
            row = pixels[y * pitch:y * pitch + width * 4]
            for x in range(width):
                rgb.extend((row[x * 4 + 2], row[x * 4 + 1], row[x * 4]))
        return bytes(rgb)

    def dac(self):
        self.request("io/port", {"port": 0x3c7, "value": 0, "width": 1}, "PUT")
        result = bytes(self.get("io/port?port=0x3C9&width=1")["value"] for _ in range(768))
        if max(result) > 63:
            raise ValueError("VGA DAC is not six bit")
        return result


def wait_for(operation, timeout=30):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            result = operation()
            if result:
                return result
        except (OSError, ValueError) as error:
            last = error
        time.sleep(.1)
    raise TimeoutError(str(last))


def base_of(memory, language):
    anchor_offset = 0x1082 if language == "ru" else 0x1132
    if memory.count(ANCHOR) != 1:
        return None
    base = memory.find(ANCHOR) - anchor_offset
    width_offset = 0xc1fc + (0 if language == "ru" else 0xb0)
    if struct.unpack_from("<HH", memory, base + width_offset) == (320, 200):
        return base
    return None


def native_state(memory, language, base, pose):
    delta = 0 if language == "ru" else 0xb0
    camera = base + 0xaa16 + delta
    focal = base + (0x934 if language == "ru" else 0xa44)
    result = {
        "data_linear_base": base,
        "camera_linear_address": camera,
        "D": struct.unpack_from("<i", memory, camera)[0],
        "width_height_cx_cy": list(struct.unpack_from("<hhhh", memory, camera + 4)),
        "clip": list(struct.unpack_from("<iiii", memory, camera + 12)),
        "viewport_scale": struct.unpack_from("<h", memory, camera + 28)[0],
        "sin_cos": list(struct.unpack_from("<ii", memory, camera + 40)),
        "camera_xyz": list(struct.unpack_from("<iii", memory, camera + 48)),
        "focal_q12": struct.unpack_from("<h", memory, focal)[0],
        "irq_count": struct.unpack_from("<I", memory, base + 0xe70 + delta)[0],
        "scheduler_elapsed": struct.unpack_from("<I", memory, base + 0x20cc + delta)[0],
        "effect_phase": "unknown",
    }
    pattern = struct.pack("<iii", *pose)
    found = []
    at = 0
    while (at := memory.find(pattern, at)) >= 0:
        hero = at - 0x26
        if hero >= 0 and struct.unpack_from("<H", memory, hero + 0x12)[0] == 1:
            found.append(hero)
        at += 1
    result["matching_hero_addresses"] = found
    if len(found) == 1:
        hero = found[0]
        result["hero"] = {"linear_address": hero, "xyz": list(pose),
            "heading": struct.unpack_from("<h", memory, hero + 0x32)[0],
            "health_energy": list(struct.unpack_from("<hh", memory, hero + 0x5e)),
            "lives_score": list(struct.unpack_from("<hh", memory, hero + 0xd6))}
    return result


def capture(api, directory, name, language, base, pose, *, require_play=False):
    for attempt in range(10):
        started = time.monotonic()
        memory_a = api.memory()
        rgb_a = api.raw()
        dac = api.dac()
        rgb_b = api.raw()
        memory_b = api.memory()
        elapsed = time.monotonic() - started
        state_a = native_state(memory_a, language, base, pose)
        state_b = native_state(memory_b, language, base, pose)
        pixels_a = memory_a[0xa0000:0xafa00]
        pixels_match = pixels_a == memory_b[0xa0000:0xafa00]
        palette_a = bytes((v * 255 + 31) // 63 for v in dac)
        expected_rgb = b"".join(palette_a[v * 3:v * 3 + 3] for v in pixels_a)
        palette_match = rgb_a == rgb_b
        dac_match = expected_rgb == rgb_a
        camera_match = all(state_a[k] == state_b[k] for k in ("camera_xyz", "sin_cos", "D", "width_height_cx_cy", "clip", "focal_q12", "hero" ) if k in state_a)
        if pixels_match and palette_match and dac_match and camera_match:
            if require_play:
                if "hero" not in state_a or state_a["camera_xyz"] != list(pose):
                    raise ValueError("Selected world did not naturally reach its own starting pose")
                if (state_a["D"], state_a["width_height_cx_cy"], state_a["clip"], state_a["viewport_scale"], state_a["focal_q12"]) != (200, [320, 161, 159, 79], [0, 0, 319, 159], 8, 4096):
                    raise ValueError("Native PLAY camera disagrees with original camera evidence")
            (directory / (name + ".idx")).write_bytes(pixels_a)
            (directory / (name + ".vga6")).write_bytes(dac)
            (directory / (name + ".rgb8")).write_bytes(palette_a)
            (directory / (name + ".api.rgb")).write_bytes(rgb_a)
            (directory / (name + ".data.bin")).write_bytes(memory_a[base:base + 65536])
            png(directory / (name + ".png"), 320, 200, pixels_a, palette_a)
            return {"name": name, "width": 320, "height": 200, "indices_sha256": digest(pixels_a),
                "vga6_sha256": digest(dac), "rgb8_sha256": digest(palette_a),
                "api_rgb_sha256": digest(rgb_a), "api_rgb_format": "RGB888 320x200, row-major, decoded from BGRX32 raw API, native pitch 2560",
                "dac_rgb_expansion": "round_nearest(vga6*255/63), integer (v*255+31)//63",
                "png_sha256": digest((directory / (name + ".png")).read_bytes()),
                "data_sha256": digest(memory_a[base:base + 65536]), "state_before": state_a, "state_after": state_b,
                "capture_wall_seconds": elapsed, "stability_attempt": attempt + 1,
                "framebuffer_two_reads_identical": pixels_match, "rendered_rgb_two_reads_identical": palette_match,
                "dac_and_vga_indices_match_raw_rgb": dac_match, "camera_and_hero_bracket_identical": camera_match,
                "consistency": "Each endpoint atomic; independent VGA memory, raw RGB capture and DAC reads bracketed, not globally paused. IRQ/ticks may advance; unknown animation phases remain unknown."}
        mismatches = [(i, expected_rgb[i], rgb_a[i], dac[pixels_a[i // 3] * 3 + i % 3]) for i in range(len(rgb_a)) if expected_rgb[i] != rgb_a[i]][:12]
        print(json.dumps({"retry": name, "pixels": pixels_match, "palette": palette_match, "dac": dac_match, "camera": camera_match, "mismatches": mismatches}), flush=True)
        time.sleep(.5)
    raise RuntimeError(f"No stable frame/palette/state bracket for {name}")


def trial(language, level, directory, dosbox, port):
    directory = directory.resolve()
    allowed = (ROOT / "research/evidence/nds/n06/dos").resolve()
    if not directory.is_relative_to(allowed) or directory == allowed:
        raise ValueError("Capture output must be a new child of research/evidence/nds/n06/dos")
    directory.mkdir(parents=True, exist_ok=False)
    source = ROOT / "originals/pkr" / ("RUS" if language == "ru" else "ENG")
    exe = "PKR.EXE" if language == "ru" else "PKE.EXE"
    inventory = json.loads((ROOT / f"analysis/originals/inventory-{language}.json").read_text(encoding="utf8"))
    before = {p.name: digest(p.read_bytes()) for p in source.iterdir() if p.is_file()}
    if before[exe] != inventory["sha256"] or before[exe.replace(".EXE", ".RES")] != inventory["resource"]["sha256"]:
        raise ValueError("Original inventory SHA mismatch")
    game = directory / "game"
    shutil.copytree(source, game)
    world_path = ROOT / f"analysis/originals/worlds-{language}.json"
    world = json.loads(world_path.read_text(encoding="utf8"))[level - 1]["player"]
    pose = [world[k] for k in ("x", "y", "z")]
    config = f'''[sdl]
fullscreen = off
output = texture
[dosbox]
machine = svga_s3
memsize = 32
[cpu]
core = normal
cputype = pentium
cpu_cycles = 60000
cpu_cycles_protected = 60000
[mixer]
nosound = true
[sblaster]
sbtype = sb16
sbbase = 220
irq = 7
dma = 1
hdma = 5
[webserver]
webserver_enabled = on
webserver_bind_address = 127.0.0.1
webserver_port = {port}
webserver_osd = off
[autoexec]
mount -u c
mount c "{game}"
c:
{exe}
'''
    cfg = directory / "dosbox.conf"
    cfg.write_text(config, encoding="ascii")
    token = secrets.token_hex(32)
    api = Api(port, token)
    try:
        urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=1)
    except OSError:
        pass
    else:
        raise RuntimeError("Refusing occupied HTTP port")
    report = {"status": "failed", "language": language, "level": level, "configuration": config,
        "source_file_hashes_before": before, "dosbox": str(dosbox), "dosbox_sha256": digest(dosbox.read_bytes()),
        "collector_sha256": digest(Path(__file__).read_bytes()), "events": [], "frames": [],
        "expected_pose_source": {"file": world_path.relative_to(ROOT).as_posix(), "sha256": digest(world_path.read_bytes()), "role": "prior original-data research used only to locate/verify natural PLAY pose; no GBA implementation import"},
        "input_method": "DOSBox Automation i8042 key replay; no game memory writes, pose injection, or code patching",
        "command": [str(dosbox), "--noprimaryconf", "--nolocalconf", "-conf", str(cfg), "-noconsole"]}
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DOSBOX_API_TOKEN=token)
    with (directory / "stdout.log").open("wb") as out, (directory / "stderr.log").open("wb") as err:
        proc = subprocess.Popen(report["command"], cwd=directory, env=env, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            report["emulator"] = wait_for(lambda: api.get("dosbox/info"))
            base = wait_for(lambda: base_of(api.memory(), language))
            time.sleep(1.5)
            def snap(name, play=False):
                frame = capture(api, directory, name, language, base, pose, require_play=play)
                report["frames"].append(frame)
                print(json.dumps({"captured": name, "indices": frame["indices_sha256"], "bracket_seconds": frame["capture_wall_seconds"]}), flush=True)
            def key(name, wait=1.5):
                result = api.key(name)
                report["events"].append({"key": name, "pressed_pic_ms": 100, "following_wall_wait_seconds": wait, "api_result": result})
                time.sleep(wait)
            snap("boot")
            key("enter")
            snap("story")
            key("enter")
            snap("menu-default")
            for _ in range(level - 1):
                key("down", .5)
            snap("menu-selected")
            key("enter")
            snap("bintro")
            key("enter")
            key("enter")
            snap("play-start", True)
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
            after = {p.name: digest(p.read_bytes()) for p in source.iterdir() if p.is_file()}
            copied = {p.name: digest(p.read_bytes()) for p in game.iterdir() if p.is_file()}
            report["source_file_hashes_after"] = after
            report["copy_file_hashes_after"] = copied
            report["originals_unchanged"] = after == before
            report["copied_runtime_unchanged"] = all(copied.get(k) == before[k] for k in (exe, exe.replace(".EXE", ".RES"), "RTM.EXE", "DPMI16BI.OVL"))
            if not report["originals_unchanged"] or not report["copied_runtime_unchanged"]:
                report["status"] = "failed"
            (directory / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("language", choices=("ru", "en"))
    parser.add_argument("level", type=int, choices=(1, 2, 3))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dosbox", type=Path, default=DEFAULT_DOSBOX)
    parser.add_argument("--port", type=int, default=8147)
    args = parser.parse_args()
    result = trial(args.language, args.level, args.output, args.dosbox, args.port)
    print(json.dumps({"status": result["status"], "language": args.language, "level": args.level, "originals_unchanged": result["originals_unchanged"]}))


if __name__ == "__main__":
    main()
