"""G05 independent package/camera/door checks and native-vs-ARM frame comparison.

Raw assets, frames and visualizations stay in ignored build/. This script does
not claim whole-DOS pixel equivalence or measure GBA performance.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from verify_gba import png
from verify_packing import originals, Package, unified_oracle

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/gba/renderer-host-check"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def check(actual, expected, context):
    if actual != expected:
        raise AssertionError(context + ": values differ; raw outputs remain in build/gba/renderer-host-check")


def artifact(path):
    return {"path": path.relative_to(ROOT).as_posix(), "size": path.stat().st_size,
            "sha256": sha(path.read_bytes())}


def palette555(palette):
    check(len(palette), 768, "256 RGB6 palette triplets")
    if max(palette) > 63:
        raise AssertionError("palette is not VGA6")
    return b"".join(struct.pack("<H", (r >> 1) | ((g >> 1) << 5) | ((b >> 1) << 10))
                    for r, g, b in zip(palette[::3], palette[1::3], palette[2::3]))


def write_frame_png(path, frame, palette, quantized=False):
    check(len(frame), 240 * 160, "indexed frame size")
    colors = [tuple(((palette[index * 3 + channel] >> 1) * 255 // 31 if quantized else
                     palette[index * 3 + channel] * 255 // 63) for channel in range(3))
              for index in range(256)]
    png(path, 240, 160, bytes(channel for pixel in frame for channel in colors[pixel]))


def font_atlas(path, font):
    check(len(font), 2048, "FONT8 256 times 8 bytes")
    rgb = bytearray(128 * 128 * 3)
    for glyph in range(256):
        for y in range(8):
            row = font[glyph * 8 + y]
            for x in range(8):
                if row & (128 >> x):
                    at = (((glyph // 16) * 8 + y) * 128 + (glyph % 16) * 8 + x) * 3
                    rgb[at:at + 3] = b"\xff\xff\xff"
    png(path, 128, 128, bytes(rgb))


def project_point(camera, world):
    """Independent G04 scalar equations for the actual bounded door points."""
    x, y, z = world
    dx, dz = x - camera["x"], z - camera["z"]
    lateral = (dx * camera["cosine"] + dz * camera["sine"]) // 32768
    depth = (dz * camera["cosine"] - dx * camera["sine"]) // 32768
    numerator = camera["scale"] * camera["distance"] * camera["focal"]
    denominator = depth + camera["distance"]
    if denominator <= 0:
        raise AssertionError("door oracle unexpectedly behind projection plane")
    factor = numerator // denominator
    dos_x = camera["center_x"] + (lateral * factor) // 32768
    dos_y = camera["center_y"] + ((y - camera["y"]) * factor) // 32768
    return {"dos": [dos_x, dos_y], "gba": [dos_x * 3 // 4, dos_y * 9 // 10],
            "depth": depth, "factor": factor}


def scene_oracle(original, package, number, language):
    source, native, states, _ = unified_oracle(original, number, language)
    hero = states[native["player_index"] - 1]
    camera = {"x": hero.get(0x26, 4, sign=True), "y": hero.get(0x2a, 4, sign=True),
              "z": hero.get(0x2e, 4, sign=True), "sine": 32768, "cosine": 0,
              "distance": 200, "scale": 8, "focal": 4096, "center_x": 159, "center_y": 79}
    if number == 1:
        check([camera[k] for k in ("x", "y", "z")], [15120, -300, 22800], "G03 shared RUS initial pose")
        check(hero.get(0x32, sign=True), 90, "initial heading90")
    # Production camera must come from shared runtime, not original English start.
    summary = package.view(number, language, source, native, states)
    return source, native, states, camera, summary


def door_oracle(source, package, camera):
    obj = next(obj for obj in source["objects"] if obj["glyph"] == 0x64)
    check(obj["class"], 7, "real two-panel door class")
    resource_id = package.names_to_ids[obj["sprite"]]
    resource = package.resources[resource_id - 1]["variants"]["ru"]
    width, height = struct.unpack_from("<HH", package.data, resource["offset"])
    check((width, height, obj["width"]), (96, 128, 5), "G04 measured door texture/scale")
    world_width, world_height = width * obj["width"], height * obj["width"]
    cells, output = [], []
    tmap = bytes.fromhex(source["maps"][0])
    for at, glyph in enumerate(tmap):
        if glyph == 0x64:
            cells.append((at % 64, at // 64))
    for column, row in ((24, 16), (24, 17)):
        check((column, row) in cells, True, "door source-cell placement")
        origin_x, origin_z = column * 480, (63 - row) * 480
        points = [project_point(camera, (origin_x + world_width // 2, y, z))
                  for y in (-world_height, 0) for z in (origin_z, origin_z + world_width)]
        dos = [min(p["dos"][0] for p in points), min(p["dos"][1] for p in points),
               max(p["dos"][0] for p in points), max(p["dos"][1] for p in points)]
        gba = [min(p["gba"][0] for p in points), min(p["gba"][1] for p in points),
               max(p["gba"][0] for p in points), max(p["gba"][1] for p in points)]
        output.append({"source_cell": [column, row], "resource_id": resource_id,
                       "dos_bounds": dos, "gba_bounds": gba, "depth": points[0]["depth"], "factor": points[0]["factor"]})
    check([panel["dos_bounds"] for panel in output], [[145, 59, 172, 95], [118, 59, 145, 95]], "G04 independent native-record door bounds")
    check([panel["gba_bounds"] for panel in output], [[108, 53, 129, 85], [88, 53, 108, 85]], "GBA selected viewport scaling")
    return output


def resource_bytes(package, name, language):
    resource_id = package.names_to_ids[name.encode("cp866").hex()]
    record = package.resources[resource_id - 1]["variants"][language]
    return package.data[record["offset"]:record["offset"] + record["size"]], resource_id


def with_hud(frame, font, palette, language, hero):
    """Independent FONT8 compositor for the explicit G05 UI contract."""
    output = bytearray(frame)
    colors = [sum(palette[i * 3:i * 3 + 3]) for i in range(256)]
    black, white = min(range(256), key=colors.__getitem__), max(range(256), key=colors.__getitem__)

    def box(x, y, width, height):
        for row in range(y, min(160, y + height)):
            output[row * 240 + x:row * 240 + min(240, x + width)] = bytes((black,)) * min(width, 240 - x)

    def text(x, y, message):
        for glyph in message:
            if x + 8 > 240:
                break
            for row in range(8):
                for col in range(8):
                    if y + row < 160 and font[glyph * 8 + row] & (128 >> col):
                        output[(y + row) * 240 + x + col] = white
            x += 8

    box(0, 0, 56, 12)
    text(2, 2, b"HP")
    text(18, 2, str(hero.get(0x5e, sign=True)).encode("ascii"))
    box(192, 0, 48, 12)
    text(194, 2, b"E")
    text(202, 2, str(hero.get(0x60, sign=True)).encode("ascii"))
    keys = hero.get(0x72)
    if keys:
        box(192, 132, 48, 12)
        text(194, 134, b"K")
        text(202, 134, str(keys).encode("ascii"))
    box(0, 144, 240, 16)
    text(0, 148, "Уровень 1".encode("cp866") if language == "ru" else b"Level 1")
    return bytes(output)


def verify_faces(actual, source, package, camera, expected_doors):
    check((actual["width"], actual["height"], actual["scene_height"]), (240, 160, 144), "renderer dimensions")
    check(actual["camera"], camera, "runtime camera equals independent shared LEVEL1 oracle")
    for name in ("frame_guards", "runtime_unchanged", "unsupported_camera", "null_workspace"):
        check(actual["checks"][name], True, "native production tool " + name)
    stats, faces = actual["stats"], actual["faces"]
    check((stats["face_count"], stats["faces_built"]), (len(faces), len(faces)), "bounded face counts")
    check(stats["face_capacity"], 256, "declared face capacity")
    if not 2 <= len(faces) <= 256 or not 0 < stats["faces_drawn"] <= len(faces) or stats["pixels_written"] <= 0:
        raise AssertionError("actual scene did not produce bounded textured faces")
    previous_key, resources, doors = None, set(), []
    for face in faces:
        index = face["image_index"]
        if not 0 <= index < len(source["objects"]):
            raise AssertionError("face definition ID out of range")
        definition = source["objects"][index]
        check(face["glyph"], definition["glyph"], "face references the actual source glyph")
        if definition["class"] not in (2, 4, 7):
            raise AssertionError("G05 face unexpectedly reinterprets a billboard as a wall")
        resource_id = package.names_to_ids[definition["sprite"]]
        resource = package.resources[resource_id - 1]["variants"]["ru"]
        payload = package.data[resource["offset"]:resource["offset"] + resource["size"]]
        width, height = struct.unpack_from("<HH", payload)
        check(len(payload), 18 + width * height, "actual bound image pixel layout")
        if not (0 <= face["u0"] < width and 0 <= face["u1"] < width and 0 <= face["v0"] < height):
            raise AssertionError("face source coordinates outside actual bound texture")
        if face["depth0"] < 0 or face["depth1"] < 0:
            raise AssertionError("face survived near clipping with negative depth")
        # G04 centered-plane sort bias is one quarter of the signed world width.
        # Ordinary edge walls retain average depth; this is not pixel depth.
        key = (face["depth0"] + face["depth1"]) // 2
        if definition["class"] in (4, 7):
            world_width = (width * definition["width"]) & 0xffff
            if world_width & 0x8000:
                world_width -= 0x10000
            key -= world_width // 4
        check(face["key"], max(0, key), "G04 edge/centered-plane sort key")
        if previous_key is not None and face["key"] > previous_key:
            raise AssertionError("face list not sorted back to front")
        previous_key = face["key"]
        resources.add(resource_id)
        if definition["glyph"] == 0x64:
            check((face["depth0"], face["depth1"]), (3360, 3360), "G04 measured two-panel door depth")
            doors.append([min(face["x0"], face["x1"]), min(face["top0"], face["top1"]),
                          max(face["x0"], face["x1"]), max(face["bottom0"], face["bottom1"])])
    check(sorted(doors), sorted(door["gba_bounds"] for door in expected_doors), "actual built doors equal independent G04 world/projection measurements")
    return {"faces": len(faces), "unique_bound_resource_ids": sorted(resources), "door_bounds_verified": doors,
            "guarded_frame_bytes": 38400, "native_negative_API_probes": ["unsupported camera", "null workspace"], "stats": stats}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tool", type=Path, default=ROOT / "build/native/dok-render.exe")
    parser.add_argument("--package", type=Path, default=ROOT / "build/gba/assets/assets.bin")
    parser.add_argument("--arm-frame", type=Path)
    parser.add_argument("--arm-palette", type=Path)
    parser.add_argument("--arm-language", choices=("ru", "en"), default="ru")
    parser.add_argument("--arm-dir", type=Path,
                        help="Compare renderer-ru/en/ru-return.idx and .pal captures together")
    parser.add_argument("--capacity-tool", type=Path,
                        help="Same production tool built with DOK_RENDER_FACE_CAPACITY=1")
    args = parser.parse_args()
    original = originals()
    package = Package(args.package.read_bytes(), original)
    OUT.mkdir(parents=True, exist_ok=True)
    report = {"task": "G05", "scope": "bounded static LEVEL1 and native/ARM indexed render verification",
              "whole_DOS_pixel_equivalence": False, "performance_measured": False,
              "script": artifact(Path(__file__)), "package": artifact(args.package.resolve()),
              "host_tool": artifact(args.tool.resolve()), "languages": {}, "ARM_comparison": "not supplied"}
    for language in ("ru", "en"):
        directory = OUT / language
        directory.mkdir(parents=True, exist_ok=True)
        frame_path, palette_path = directory / "frame.idx", directory / "palette.rgb6"
        completed = subprocess.run([str(args.tool.resolve()), str(args.package.resolve()), "1", language,
                                    str(frame_path), str(palette_path)], cwd=ROOT, capture_output=True, text=True, check=True)
        actual = json.loads(completed.stdout)
        (directory / "tool.json").write_text(json.dumps(actual, indent=2) + "\n", encoding="utf-8")
        source, native, states, camera, summary = scene_oracle(original, package, 1, language)
        frame, palette = frame_path.read_bytes(), palette_path.read_bytes()
        check(len(frame), 38400, "complete 240x160 indexed frame")
        check(frame[240 * 144:], bytes(240 * 16), "native renderer reserves bottom sixteen rows")
        expected_palette, palette_id = resource_bytes(package, "GAMEP1", language)
        check(palette, expected_palette, "actual scene palette exact original resource")
        palette_ram = palette555(palette)
        (directory / "palette.gba555").write_bytes(palette_ram)
        font, font_id = resource_bytes(package, "FONT8", language)
        font_atlas(directory / "FONT8.png", font)
        hud = with_hud(frame, font, palette, language, states[native["player_index"] - 1])
        (directory / "with-ui.idx").write_bytes(hud)
        write_frame_png(directory / "native-vga6.png", frame, palette)
        write_frame_png(directory / "native-gba555.png", frame, palette, quantized=True)
        write_frame_png(directory / "with-ui.png", hud, palette, quantized=True)
        door = door_oracle(source, package, camera)
        face_checks = verify_faces(actual, source, package, camera, door)
        if len(set(frame[:240 * 144])) < 3:
            raise AssertionError("scene is only flat diagnostic filling")
        report["languages"][language] = {"camera_expected": camera, "door_oracle": door,
                                           "resource_ids": {"palette": palette_id, "font8": font_id},
                                           "frame": artifact(frame_path), "palette": artifact(palette_path),
                                           "palette_gba555_sha256": sha(palette_ram), "FONT8_sha256": sha(font),
                                           "with_ui_sha256": sha(hud), "tool": artifact(directory / "tool.json"),
                                           "geometry_and_bounds": face_checks}
    check((OUT / "ru/frame.idx").read_bytes(), (OUT / "en/frame.idx").read_bytes(), "RU/EN shared geometry and initial frame")
    check((OUT / "ru/palette.gba555").read_bytes(), (OUT / "en/palette.gba555").read_bytes(), "RU/EN shared scene palette")
    captures = []
    if args.arm_dir:
        captures = [(name, language, args.arm_dir / ("renderer-" + name + ".idx"),
                     args.arm_dir / ("renderer-" + name + ".pal"))
                    for name, language in (("ru", "ru"), ("en", "en"), ("ru-return", "ru"))]
    elif args.arm_frame:
        if args.arm_palette is None:
            raise AssertionError("ARM frame check requires the actual 512-byte palette RAM capture")
        captures = [(args.arm_language, args.arm_language, args.arm_frame, args.arm_palette)]
    comparisons = []
    for name, language, arm_frame_path, arm_palette_path in captures:
        arm_frame = arm_frame_path.read_bytes()
        expected = (OUT / language / "with-ui.idx").read_bytes()
        check(arm_frame, expected, "all 38400 visible ARM indices including FONT8 UI")
        check(arm_palette_path.read_bytes(), (OUT / language / "palette.gba555").read_bytes(), "ARM palette RAM RGB555")
        comparisons.append({"capture": name, "language": language, "indices_compared": 38400,
                            "palette_bytes_compared": 512, "status": "identical",
                            "frame": artifact(arm_frame_path.resolve()), "palette": artifact(arm_palette_path.resolve())})
    if comparisons:
        report["ARM_comparison"] = comparisons
        if args.arm_dir and (args.arm_dir / "dok-diagnostic.gba").exists():
            report["ARM_ROM"] = artifact((args.arm_dir / "dok-diagnostic.gba").resolve())
    negative_cli = []
    for level, language in (("0", "ru"), ("4", "ru"), ("1", "invalid")):
        completed = subprocess.run([str(args.tool.resolve()), str(args.package.resolve()), level, language,
                                    str(OUT / "invalid.idx"), str(OUT / "invalid.pal")], cwd=ROOT, capture_output=True)
        check(completed.returncode, 2, "host tool rejects invalid scene arguments without crash")
        negative_cli.append({"level": level, "language": language, "exit_code": completed.returncode})
    report["negative_cli"] = negative_cli
    if args.capacity_tool:
        bounded_frame, bounded_palette = OUT / "capacity.idx", OUT / "capacity.pal"
        # Previous artifacts must not hide an accidental successful write.
        for path in (bounded_frame, bounded_palette):
            path.unlink(missing_ok=True)
        completed = subprocess.run([str(args.capacity_tool.resolve()), str(args.package.resolve()), "1", "ru",
                                    str(bounded_frame), str(bounded_palette)], cwd=ROOT, capture_output=True, text=True)
        check(completed.returncode, 1, "one-face capacity returns a controlled error")
        check(completed.stderr.strip(), "Render status 3 guards 1 runtime_unchanged 1", "capacity guard and immutable runtime")
        check(bounded_frame.exists() or bounded_palette.exists(), False, "capacity failure emits no successful frame/palette")
        report["capacity_probe"] = {"tool": artifact(args.capacity_tool.resolve()), "capacity": 1,
                                    "renderer_status": 3, "frame_guards": True,
                                    "runtime_unchanged": True, "successful_outputs_written": False}
    report["status"] = "passed"
    (OUT / "verification.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "languages": 2, "ARM_comparison": report["ARM_comparison"]}, indent=2))


if __name__ == "__main__":
    main()
