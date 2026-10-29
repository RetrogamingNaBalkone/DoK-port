#!/usr/bin/env python3
"""N06: read preserved Ghidra/RAM evidence and derive the NDS camera contract.

No game-engine or GBA implementation imports. This checks original evidence;
continuous GPU projection and half-pixel edge policy are labelled adaptations.
"""
from __future__ import annotations

import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import random
import re
import struct

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "research/evidence/nds/n06/camera-audit.json"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def wrap32(value: int) -> int:
    return ((value + 2**31) % 2**32) - 2**31


def idiv(a: int, b: int) -> int:
    if b == 0 or (a == -(2**31) and b == -1):
        raise ArithmeticError("original signed division would fault")
    return (-1 if (a < 0) != (b < 0) else 1) * (abs(a) // abs(b))


def project_sample(point: tuple[int, int, int], camera: list[int]) -> tuple[int, int, int, int]:
    px, py, pz, sinq, cosq, d, k, focal, cx, cy = camera
    dx, dz = wrap32(point[0] - px), wrap32(point[2] - pz)
    lateral = wrap32(wrap32(dx * cosq) + wrap32(dz * sinq)) >> 15
    depth = wrap32(wrap32(dz * cosq) - wrap32(dx * sinq)) >> 15
    n = wrap32(wrap32(k * d) * focal)
    factor = idiv(n, wrap32(depth + d))
    return (wrap32(cx + (wrap32(lateral * factor) >> 15)),
            wrap32(cy + (wrap32(wrap32(point[1] - py) * factor) >> 15)), depth, factor)


def project(point: tuple[int, int, int], camera: list[int]) -> tuple[int, int, int]:
    return project_sample(point, camera)[:3]


def fraction(value: Fraction) -> dict[str, int | float]:
    return {"numerator": value.numerator, "denominator": value.denominator,
            "decimal": float(value)}


def mapped(index: int, multiplier: Fraction) -> Fraction:
    return (Fraction(index) + Fraction(1, 2)) * multiplier - Fraction(1, 2)


def height_fixtures(read) -> dict:
    """Original-data fixtures and ASM-derived arithmetic, not DOS frame captures."""
    summary = json.loads(read("analysis/nds-n05-balance.json").decode("utf8"))
    prior_runtime = read(summary["emulator"]["report"]["path"])
    assert digest(prior_runtime) == summary["emulator"]["report"]["sha256"]
    goldens = json.loads(prior_runtime)["goldens"]
    original = read("originals/pkr/RUS/PKR.RES")
    assert digest(original) == "af7135fbcfc765c1e963682a29b108ffe90d652d96fa130cf9db6b217277b816"
    asm = read("research/evidence/rendering/ru/evidence.asm").decode("utf8")
    for line in ["1098:35e7\tNEG AX", "1098:361d\tINC AX",
                 "1098:361e\tSUB AX,word ptr [BP + 0xff6e]",
                 "1098:3622\tSUB AX,word ptr [BP + 0xff7c]",
                 "10b0:119a\tSAR EAX,0x5", "10b0:176b\tSAR EAX,0x5",
                 "10b0:020d\tJLE 0x10b0:03c9", "10b0:0d78\tJLE 0x10b0:0eec"]:
        assert line in asm, line
    levels = []
    for level in (1, 2, 3):
        data = read(f"research/evidence/nds/n05/oracle/level{level}-lang1.json")
        pin = next(c for c in goldens if Path(c["path"]).name == f"level{level}-lang1.json")
        assert digest(data) == pin["sha256"]
        model = json.loads(data)["base"]
        source = bytes.fromhex(model["source_hex"])
        record = original[2 + model["source_index"] * 30:32 + model["source_index"] * 30]
        size, offset = struct.unpack_from("<II", record, 16)
        assert original[offset:offset+size] == source
        assert digest(source) == model["source_sha256"]
        fc, tm = [bytes.fromhex(model["maps"][i]).replace(b".", b" ") for i in (2, 0)]
        section, fc_rows = b"", []
        for match in re.finditer(rb"[^\r\n]+", source):
            if match[0].startswith(b"["):
                section = match[0]
            elif section == b"[FCMap]" and not match[0].startswith(b"#"):
                assert len(match[0]) == 64
                fc_rows.append(match.start())
        assert len(fc_rows) == 64
        cases = []
        for glyph in sorted(set(fc)):
            index = model["glyph_index"][glyph]
            assert index != 0
            obj = model["objects"][index-1]
            parameter = next(f for f in obj["fields"] if f[0] == 0x54)
            assert parameter[1] == 0 and int(source[parameter[3]:parameter[3]+parameter[4]]) == parameter[2]
            line_start, line_bytes = obj["raw_line"]
            tokens = list(re.finditer(rb"\S+", source[line_start:line_start+line_bytes]))
            height_span = [line_start + tokens[4].start(), len(tokens[4][0])]
            assert int(tokens[4][0]) == obj["height_parameter"]
            placements = [i for i, value in enumerate(fc) if value == glyph]
            open_cells = [i for i in placements if model["objects"][model["glyph_index"][tm[i]]-1]["class_id"] != 2]
            h = obj["height_parameter"]
            sign16 = lambda v: ((v+32768) % 65536)-32768
            floor = sign16(-h)
            extent = sign16(model["settings"][4] + 1 - parameter[2] - h)
            samples = []
            for i in open_cells[:3]:
                x, source_y = i % 64, i // 64
                samples.append(dict(source_cell=[x,source_y], runtime_cell=[x,63-source_y],
                                    source_fcmap_byte_offset=fc_rows[source_y]+x,
                                    original_file_offset=offset+fc_rows[source_y]+x,
                                    raw_source_glyph=source[fc_rows[source_y]+x]))
            cases.append(dict(glyph=glyph, class_id=obj["class_id"], definition_line_offset=line_start,
                              definition_line_bytes=line_bytes, height_parameter_native_offset=0x19,
                              height_parameter=h, height_parameter_source_span=height_span,
                              ceiling_parameter_native_offset=0x54, ceiling_parameter=parameter[2],
                              ceiling_parameter_source_span=parameter[3:5],
                              floor_y=floor, extent_signed16=extent,
                              ceiling_y=wrap32(floor-extent) if parameter[2] >= 0 else None,
                              horizontal_planes=2 if parameter[2] >= 0 else 1,
                              all_source_cells=len(placements), non_class2_tmap_cells=len(open_cells),
                              samples=samples))
        levels.append(dict(level=level, source_index=model["source_index"], source_sha256=digest(source),
                           original_res_file_offset=offset, cell_side=model["settings"][3],
                           cell_height=model["settings"][4], cases=cases))
    near = []
    for depths in [(-1,-1), (0,0), (-1,0), (0,1), (-1,1), (1,0), (1,-1), (-32,32)]:
        lateral, u = [-120,120], [0,95]
        d = list(depths)
        rejected = d[0] <= 0 and d[1] <= 0
        if not rejected:
            if d[0] < 0:
                denominator = wrap32(d[1]-d[0])
                lateral[0] = wrap32(idiv(wrap32((lateral[0]-lateral[1])*d[1]), denominator) + lateral[1])
                u[0] = idiv(wrap32(u[1]*wrap32(-d[0])), denominator)
                d[0] = 0
            elif d[1] < 0:
                denominator = wrap32(d[0]-d[1])
                lateral[1] = wrap32(idiv(wrap32((lateral[1]-lateral[0])*d[0]), denominator) + lateral[0])
                u[1] = idiv(wrap32(u[1]*d[0]), denominator)
                d[1] = 0
        near.append(dict(input_depths=depths, rejected_before_projection=rejected,
                         clipped_depths=None if rejected else d,
                         clipped_lateral=None if rejected else lateral,
                         clipped_u=None if rejected else u,
                         billboard_visible=[depth > 0 for depth in depths]))
    horizontal = []
    for raw in [-209715200,-1048577,-1048576,-1048575,-32769,-32768,-32767,-1,0,1,32767,32768,1048576]:
        depth = raw >> 15
        adjusted = depth >> 5 if depth < 0 else depth
        assert depth >= 0 or adjusted == raw >> 20
        denominator = adjusted + 200
        horizontal.append(dict(rotation_low32=raw, initial_sar15=depth, adjusted_depth=adjusted,
                               denominator=denominator,
                               quotient=None if denominator == 0 else idiv(6553600,denominator),
                               divide_fault=denominator == 0))
    analytic_camera = [0,-300,0,0,32768,200,8,4096,159,79]
    analytic = []
    for parameter, ceiling in [(10,-951),(400,-561)]:
        vertices = [(-240,ceiling,1440),(240,ceiling,1440),(240,0,1440),(-240,0,1440)]
        native = [project(p, analytic_camera) for p in vertices]
        analytic.append(dict(label="synthetic height control, not DOS execution", ceiling_parameter=parameter,
                             floor_y=0, ceiling_y=ceiling, camera=analytic_camera, vertices=vertices,
                             native_xyz=native,
                             target_xy=[[fraction(mapped(p[0],Fraction(4,5))),
                                         fraction(mapped(p[1],Fraction(77,80)))] for p in native]))
    return dict(task="N06", passed=True, scope="Original RU LEVEL FCMap fields and ASM-derived numerical fixtures; not executed DOS height screenshots",
                constructor_addresses=dict(height="RU1098:35CE..3626", two_or_one_planes="RU1098:36A0..36B4",
                                           vertical_near="RU10B0:0201..0357", billboard_near="RU10B0:0D73..0D78",
                                           horizontal_sar="RU10B0:10E9..11D7 /16BA..17A8"),
                levels=levels, vertical_near=near, horizontal_boundary=horizontal,
                analytic_height_controls=analytic,
                valid_cell_note="Mixed-sign 480-unit cell depth span is at most ceil(480*sqrt(2))=679; negative adjusted depth >=-22, denominator >=178. The -200 fault control is adversarial and not reachable for an ordinary mixed-sign single cell.",
                boundary_review="All horizontal corner depth<=0 rejects the whole quad before per-corner adjustment; scalar controls do not claim a full visible polygon.",
                future_gates=["N07: low ceilings and neighbour-height edges with exact FCMap source fixtures",
                              "N07: native vertical near0, billboard >0 and GPU adaptation compared separately",
                              "N13: moving FCMap surfaces, water, reflection and LIGHT; no static forever assumption"])


def run(output_directory: Path) -> dict:
    inventory = json.loads((ROOT / "analysis/nds-n01-evidence.json").read_text("utf8"))
    originals = []
    for sample in inventory["samples"]:
        data = (ROOT / sample["path"]).read_bytes()
        sha = digest(data)
        assert sha == sample["expected_sha256"], sample["path"]
        originals.append({"path": sample["path"], "sha256_before": sha})
    inputs: list[dict] = []

    def read(path: str) -> bytes:
        data = (ROOT / path).read_bytes()
        inputs.append({"path": path, "bytes": len(data), "sha256": digest(data)})
        return data

    runs = []
    for lang, run_name, delta, anchor_offset, camera_z, record_addrs in [
        ("ru", "ru-03", 0, 0x1082, 22800, (0x281522, 0x2C6722)),
        ("en", "en-02", 0xB0, 0x1132, 23280, (0x2CA28A, 0x2C9BD2)),
    ]:
        asm = read(f"research/evidence/rendering/{lang}/evidence.asm").decode("utf8")
        read(f"research/evidence/rendering/{lang}/annotated.c")
        asm_checks = (["1100:011b\tSAR AX,0x1", "1100:0196\tSAR EAX,0x1",
                       "1100:019e\tSAR EDX,0x1", "1098:3428\tMOV dword ptr ES:[BX],0xc8",
                       "10b0:0da7\tMOVSX EDX,word ptr [0x934]", "10b0:0db5\tADD EBX,dword ptr [0xaa16]",
                       "10b0:0dbc\tIDIV EBX", "10b0:0de2\tSAR EDX,0xf"] if lang == "ru" else
                      ["1060:104b\tSAR AX,0x1", "1060:10c6\tSAR EAX,0x1",
                       "1060:10ce\tSAR EDX,0x1", "1040:0dc5\tMOVSX EDX,word ptr [0xa44]",
                       "1040:0dd3\tADD EBX,dword ptr [0xaac6]", "1040:0dda\tIDIV EBX"])
        for line in asm_checks:
            assert line in asm, line
        ram = read(f"research/evidence/reference/{run_name}/level1-memory.bin")
        idx = read(f"research/evidence/reference/{run_name}/level1-start.idx")
        assert len(ram) == 16 * 1024 * 1024 and len(idx) == 64000
        anchor = b"OVERFLOW\0UNDERFLOW\0TLOSS\0PLOSS"
        assert ram.count(anchor) == 1
        base = ram.index(anchor) - anchor_offset

        def i16(offset: int) -> int:
            return struct.unpack_from("<h", ram, base + offset + delta)[0]

        def i32(offset: int) -> int:
            return struct.unpack_from("<i", ram, base + offset + delta)[0]

        # Focal is deliberately NOT covered by the camera-global +0xB0 delta.
        focal_offset = 0x934 if lang == "ru" else 0xA44
        focal = struct.unpack_from("<h", ram, base + focal_offset)[0]
        camera = [i32(0xAA46), i32(0xAA4A), i32(0xAA4E), i32(0xAA3E), i32(0xAA42),
                  i32(0xAA16), i16(0xAA32), focal, i16(0xAA1E), i16(0xAA20)]
        assert camera == [15120, -300, camera_z, 32768, 0, 200, 8, 4096, 159, 79]
        clip = [i32(off) for off in (0xAA22, 0xAA26, 0xAA2A, 0xAA2E)]
        assert clip == [0, 0, 319, 159]
        assert i16(0xAA1A) == 320 and i16(0xAA1C) == 161
        assert ram[0xA0000:0xA0000 + 64000] == idx
        doors = []
        for cell, z0, z1, address in [("24,16", 22560, 23040, record_addrs[0]),
                                      ("24,17", 22560, 22080, record_addrs[1])]:
            points = [(11760, -640, z0), (11760, -640, z1),
                      (11760, 0, z1), (11760, 0, z0)]
            predicted = [project(p, camera) for p in points]
            actual = struct.unpack_from("<12i", ram, address + 0x13)
            assert tuple(v for p in predicted for v in p) == actual
            pattern = struct.pack("<12i", *actual)
            assert ram.count(pattern) == 1
            doors.append({"cell": cell, "world_vertices": points,
                          "native_xyz": predicted, "record_linear_address": hex(address),
                          "unique_full_record_match": True,
                          "target_xy": [[fraction(mapped(p[0], Fraction(4, 5))),
                                         fraction(mapped(p[1], Fraction(77, 80)))] for p in predicted]})
        runs.append({"language": lang, "data_base_linear": hex(base), "camera": camera,
                     "clip": clip, "height_argument": 161, "focal_ds_offset": hex(focal_offset),
                     "checked_asm_lines": asm_checks, "frame_matches_vga_memory": True,
                     "doors": doors})

    # Inclusive pixel-centre bounds alone do not specify a continuous frustum.
    # N06 uses the natural outer-edge extension, +/- 1/2 pixel, for GPU fitting.
    hfov = math.degrees(math.atan(319 / 400) + math.atan(321 / 400))
    vfov = math.degrees(math.atan(159 / 400) + math.atan(161 / 400))
    target = {
        "policy": "band-local outer-edge mapping; no vertical-view expansion",
        "scene": [256, 154], "hud_y_and_height": [154, 38],
        "ideal_scene_height": fraction(Fraction(768, 5)),
        "scene_scale_x": fraction(Fraction(4, 5)),
        "scene_scale_y": fraction(Fraction(77, 80)),
        "hud_scale_y": fraction(Fraction(19, 20)),
        "static_scale_y": fraction(Fraction(24, 25)),
        "principal_index_xy": [fraction(mapped(159, Fraction(4, 5))),
                               fraction(mapped(79, Fraction(77, 80)))],
        "continuous_focal_xy": [fraction(Fraction(160)), fraction(Fraction(385, 2))],
        "scene_vertical_rounding_relative": fraction(Fraction(1, 384)),
        "horizontal_fov_degrees_outer_edges": hfov,
        "vertical_fov_degrees_outer_edges": vfov,
        "gpu_clip_positive_forward": {
            "x": "5*lateral/4 - eye_z/320", "y": "-5*dy/2 + eye_z/160",
            "w": "eye_z", "eye_z": "native_depth + 200"},
        "near_vertical_plane_eye_z": 200,
        "reference_nearest_mapping": {
            "x": "floor((2*target_x+1)*5/8)",
            "scene_y": "floor((2*target_y+1)*40/77)",
            "hud_source_y": "160+floor((2*(target_y-154)+1)*10/19)",
            "static_y": "floor((2*target_y+1)*25/48)"},
    }
    rng = random.Random(0xD06)
    vectors = []
    for n in range(5000):
        if n < 4000:
            theta = rng.randrange(361) * 3.14159265359 / 180
            camera = [rng.randrange(30721), rng.randrange(-2000, 1001), rng.randrange(30721),
                      math.trunc(math.sin(theta) * 32768), math.trunc(math.cos(theta) * 32768),
                      200, 8, 4096, 159, 79]
            point = (rng.randrange(30721), rng.randrange(-3000, 1001), rng.randrange(30721))
        else:
            camera = [rng.randrange(-(2**31), 2**31) for _ in range(6)] + [
                rng.randrange(-32768, 32768) for _ in range(4)]
            point = tuple(rng.randrange(-(2**31), 2**31) for _ in range(3))
        try:
            result = [0, *project_sample(point, camera)]
        except ArithmeticError:
            result = [2, 71, 72, 73, 74]
        vectors.append([*camera, *point, *result])
    for camera, point in [([0,0,0,0,32768,0,8,4096,159,79], (0,0,0)),
                          ([0,0,0,0,32768,32768,-32768,2,159,79], (0,0,-32769))]:
        try:
            project_sample(point, camera)
        except ArithmeticError:
            vectors.append([*camera, *point, 2, 71, 72, 73, 74])
        else:
            raise AssertionError("fault control did not fault")

    def nearest_q16(value: Fraction) -> int:
        scaled = value * 65536
        magnitude = abs(scaled)
        rounded = (magnitude.numerator * 2 + magnitude.denominator) // (magnitude.denominator * 2)
        return -rounded if scaled < 0 else rounded

    maps = []
    cases = [(band, x, y) for band in range(3) for x, y in
             [(0,0), (319,159), (159,79), (0,160), (319,199), (-1,-1),
              (-(2**31),2**31-1), (2**31-1,-(2**31))]]
    cases += [(rng.randrange(3), rng.randrange(-50000,50001), rng.randrange(-50000,50001))
              for _ in range(1500)]
    for band, x, y in cases:
        xx = nearest_q16(mapped(x, Fraction(4,5)))
        yy = nearest_q16(mapped(y, Fraction(77,80)) if band == 0 else
                         (Fraction(154) + mapped(y-160, Fraction(19,20)) if band == 1 else
                          mapped(y, Fraction(24,25))))
        maps.append([band,x,y,0,xx,yy] if -(2**31) <= xx < 2**31 and -(2**31) <= yy < 2**31
                    else [band,x,y,3,81,82])
    maps.append([99,0,0,1,81,82])
    outputs = []
    for name, values in [("projection-vectors.tsv", vectors), ("projection-map-vectors.tsv", maps)]:
        output = safe_output(output_directory / name)
        data = ("\n".join("\t".join(map(str, row)) for row in values) + "\n").encode("ascii")
        output.write_bytes(data)
        outputs.append({"path": output.relative_to(ROOT).as_posix(), "rows": len(values),
                        "bytes": len(data), "sha256": digest(data)})
    heights = height_fixtures(read)
    height_path = safe_output(output_directory / "height-near-fixtures.json")
    height_bytes = (json.dumps(heights, ensure_ascii=False, indent=2) + "\n").encode("utf8")
    height_path.write_bytes(height_bytes)
    outputs.append(dict(path=height_path.relative_to(ROOT).as_posix(), bytes=len(height_bytes),
                        sha256=digest(height_bytes), levels=3))
    for original in originals:
        original["sha256_after"] = digest((ROOT / original["path"]).read_bytes())
        assert original["sha256_after"] == original["sha256_before"]
    return {"schema": 1, "task": "N06", "passed": True,
            "originals": originals, "inputs": inputs, "original_evidence": runs,
            "target_adaptation": target, "independent_vectors": outputs,
            "limitations": ["Preserved Ghidra export inspected; no new EXE analysis or DOS execution",
                            "No GBA implementation imported; numerical formulas re-derived from ASM",
                            "Continuous outer-edge frustum is NDS policy, not an EXE FOV constant",
                            "No native raster/material/clipping/visibility equivalence claimed",
                            "Horizontal negative-depth SAR20 branch and water/reflections remain N07/N13"]}


def safe_output(path: Path) -> Path:
    evidence_root = ROOT / "research/evidence/nds/n06"
    absolute = path.absolute()
    if not absolute.is_relative_to(evidence_root) or ".." in path.parts:
        raise ValueError("report must remain under research/evidence/nds/n06")
    for ancestor in [absolute, *absolute.parents]:
        if ancestor == ROOT:
            break
        if ancestor.is_symlink() or (hasattr(ancestor, "is_junction") and ancestor.is_junction()):
            raise ValueError("output path crosses a symlink or junction")
    if absolute.exists() and (not absolute.is_file() or absolute.stat().st_nlink != 1):
        raise ValueError("output is not a regular unshared file")
    absolute.parent.mkdir(parents=True, exist_ok=True)
    return absolute


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=OUT)
    args = parser.parse_args()
    destination = safe_output(args.report)
    destination.write_text('{"task":"N06","passed":false}\n', encoding="utf8")
    report = run(destination.parent)
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(f"N06 camera audit PASS: 2 cameras, 4 unique full native door records; {destination}")


if __name__ == "__main__":
    main()
