"""Observe N07's actual ARM9 3D renderer without modifying emulated memory.

The unchanged N02 frontend supplies libretro transport only. N07 expectations
are derived independently from the immutable scene packet and N06 camera
contract, not from probe pass flags or previously rendered bitmap fixtures.
"""
from __future__ import annotations

import ctypes as C
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import traceback

import make_assets_fixture as fat
import run_diag_emulator as transport


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/nds/n07"
CORE_SHA = "c7e6b58c667075a2a9868f2c3ba7a98e24200410a6da3051a7c555f1f0eef8ef"
PACKAGE_SHA = "2f03ba02c9986b4e0bd321e94ca00df8a355f7dbb1dff5598a950d3366061511"


def safe_output(path: Path) -> Path:
    """Validate fixed-root outputs before any open, mkdir or core startup."""
    path = Path(path)
    if ".." in path.parts:
        raise ValueError("Parent traversal in N07 output")
    path = path.absolute()
    if path == OUT or not path.is_relative_to(OUT):
        raise ValueError("N07 output escapes build/nds/n07")
    for item in (path, *path.parents):
        if item == ROOT:
            break
        if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
            raise ValueError(f"N07 output alias: {item}")
    if path.exists() and (not path.is_file() or path.stat().st_nlink != 1):
        raise ValueError("N07 output is not an unaliased regular file")
    return path


def prepare_directory(path: Path) -> None:
    """Protect every existing destination the core may choose below its root."""
    safe_output(path / "reserved")
    if path.exists():
        pending = [path]
        while pending:
            for item in pending.pop().iterdir():
                if item.is_symlink() or (hasattr(item, "is_junction") and item.is_junction()):
                    raise ValueError(f"N07 core directory alias: {item}")
                if item.is_dir():
                    pending.append(item)
                elif not item.is_file() or item.stat().st_nlink != 1:
                    raise ValueError(f"N07 core file alias or unsupported kind: {item}")
    path.mkdir(parents=True, exist_ok=True)


def write_json(path: Path, value: dict) -> None:
    target = safe_output(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf8", newline="\n")


def ram(frontend, address: int, count: int) -> bytes:
    pointer = frontend.core.retro_get_memory_data(2)
    size = frontend.core.retro_get_memory_size(2)
    offset = address - 0x02000000
    if not pointer or count < 0 or not 0 <= offset <= size - count:
        raise ValueError("N07 observation outside exposed main RAM")
    return C.string_at(pointer + offset, count)


def scalar_fields(header: Path, typedef: str) -> list[str]:
    source = header.read_text(encoding="utf8")
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)
    match = re.search(r"typedef\s+struct\s+" + re.escape(typedef) + r"\s*\{(.*?)\}",
                      source, re.S)
    if not match:
        raise ValueError(f"Missing observation type {typedef}")
    fields = []
    for declaration in match.group(1).split(";"):
        declaration = declaration.strip()
        if not declaration:
            continue
        if not declaration.startswith("uint32_t "):
            raise ValueError("N07 observation ABI must contain uint32_t only")
        for field in declaration[len("uint32_t "):].split(","):
            field = field.strip()
            if not field.isidentifier():
                raise ValueError("N07 observation ABI requires scalar fields")
            fields.append(field)
    if len(set(fields)) != len(fields):
        raise ValueError("Duplicate observation field")
    return fields


def observe(frontend, address: int, fields: list[str], magic: int,
            version: int = 1) -> dict | None:
    if fields[:4] != ["magic", "abi_version", "size_bytes", "sequence"]:
        raise ValueError("N07 seqlock header layout changed")
    first = ram(frontend, address + 12, 4)
    data = ram(frontend, address, len(fields) * 4)
    last = ram(frontend, address + 12, 4)
    result = dict(zip(fields, struct.unpack("<" + "I" * len(fields), data)))
    if first != last or struct.unpack("<I", first)[0] != result["sequence"] or result["sequence"] & 1:
        return None
    if result["magic"] != magic:
        return None
    if result["abi_version"] != version or result["size_bytes"] != len(data):
        raise ValueError("N07 observation ABI mismatch")
    return result


def signed32(value: int) -> int:
    return value - 0x100000000 if value & 0x80000000 else value


def continuous_projection(lateral: int, relative_y: int, native_depth: int) -> tuple[Fraction, Fraction]:
    """Independent N06 edge-coordinate camera; no renderer module import."""
    eye = native_depth + 200
    if eye <= 0:
        raise ValueError("Projection control behind effective eye")
    return (Fraction(638, 5) + Fraction(160 * lateral, eye) - Fraction(1, 2),
            Fraction(12243, 160) + Fraction(385 * relative_y, 2 * eye) - Fraction(1, 2))


def capture(frontend, stem: str) -> dict:
    for suffix in (".png", "-top.png", "-bottom.png"):
        safe_output(frontend.directory / (stem + suffix))
    return frontend.capture(stem)


def compiled_memory_partitions(elf: Path) -> dict:
    """Accept reviewed slabs only for an ELF-bound compiled build input.

    Historical targets retain N03 ceilings. Never infer their budget from the
    mutable current header, and never enlarge a ceiling from a probe value.
    """
    legacy = dict(world=393216, events=65536, scratch=131072, scene=262144,
                  assets=1048576, audio=262144, io=131072)
    rebalanced = dict(legacy, events=36864, assets=1077248)
    manifest_path = elf.parent/'build-manifest.json'
    n23_target = elf.parent.resolve().is_relative_to(ROOT/'build/nds/n23')
    if n23_target:
        assert manifest_path.is_file(), 'N23 compiled memory manifest is required'
    if not manifest_path.exists():
        return dict(ceilings=legacy, provenance='Historical N03 partition ceilings')
    manifest = json.loads(manifest_path.read_text('utf8'))
    task = manifest.get('task')
    if n23_target:
        assert task == 'N23', 'N23 compiled memory manifest task mismatch'
    if task == 'N23':
        assert manifest.get('label') == elf.parent.name, 'N23 memory manifest label mismatch'
        assert elf.parent.resolve().is_relative_to(ROOT/'build/nds/n23'), 'N23 memory manifest target mismatch'
    if task not in ('N22', 'N23') or manifest.get('label') != elf.parent.name:
        return dict(ceilings=legacy, provenance='Historical N03 partition ceilings')
    relative = 'ports/nds/include/dok/memory_budget.h'
    row = next((r for r in manifest.get('sources', []) if r['path'] == relative), None)
    if row is None:
        assert task != 'N23', 'N23 compiled memory header is absent from the manifest'
        return dict(ceilings=legacy, provenance='Historical N03 partition ceilings')
    actual_elf = dict(path=elf.resolve().relative_to(ROOT).as_posix(), bytes=elf.stat().st_size,
                      sha256=hashlib.sha256(elf.read_bytes()).hexdigest())
    assert actual_elf in manifest['artifacts'], 'Memory ledger is not bound to the executed ELF'
    frozen_root = ROOT/'research/evidence/nds'/task.lower()/'candidates'/manifest['label']
    frozen = frozen_root/'sources'/relative
    if task == 'N23':
        frozen_manifest = frozen_root/'build-manifest.json'
        assert frozen_manifest.is_file() and frozen_manifest.read_bytes() == manifest_path.read_bytes(), 'N23 compiled manifest is not frozen exactly'
        assert frozen.is_file() and frozen.stat().st_size == row['bytes'] and hashlib.sha256(frozen.read_bytes()).hexdigest() == row['sha256'], 'N23 frozen compiled memory header mismatch'
    paths = (frozen,) if task == 'N23' else (frozen, ROOT/relative)
    header = next((p for p in paths if p.exists() and p.stat().st_size == row['bytes']
                   and hashlib.sha256(p.read_bytes()).hexdigest() == row['sha256']), None)
    if header is None:
        # This fallback cannot admit the enlarged assets region. It permits
        # historical targets with missing optional source snapshots to retain
        # the original strict N03 limits.
        return dict(ceilings=legacy, provenance='Historical N03 partition ceilings; bound header unavailable')
    definitions = dict(re.findall(r'^#define\s+DOK_NDS_(\w+)\s+UINT32_C\((\d+)\)', header.read_text('utf8'), re.M))
    sizes = {name: int(definitions[name.upper()+'_BYTES']) for name in legacy}
    assert sizes in (legacy, rebalanced), 'Unreviewed RAM partition change'
    assert sum(sizes.values()) == int(definitions['SLAB_BYTES']) == 2293760
    assert int(definitions['RESERVE_BYTES']) == 655360 and int(definitions['SDK_HEAP_MIN']) == 196608
    assert int(definitions['STATIC_CEILING']) == 524288 and int(definitions['ARM9_BYTES']) == 3670016
    return dict(ceilings=sizes, provenance='ELF-bound compiled memory_budget.h; approved N03 or N22 partition',
                header=dict(path=header.relative_to(ROOT).as_posix(), bytes=row['bytes'], sha256=row['sha256']),
                elf=actual_elf)


def verify_memory(frontend, probe: dict, elf: Path) -> dict:
    """Recompute the whole ARM9 closure, including heap alignment/startup."""
    if frontend.core.retro_get_memory_size(2) != 4194304 or probe["is_dsi"]:
        raise AssertionError("N07 must execute in ordinary DS mode")
    base, limit = probe["region_base"], probe["region_limit"]
    if (base, limit) != (0x02000000, 0x02380000):
        raise AssertionError("N03 ARM9 region changed")
    static_end, _ = transport.elf_symbol(elf, "__end__")
    linker_end, _ = transport.elf_symbol(elf, "__eheap_end")
    if static_end != probe["static_end"] or linker_end != limit:
        raise AssertionError("Runtime RAM bounds differ from ELF linker symbols")
    if not base <= static_end <= probe["heap_start"] <= probe["heap_end"] <= probe["heap_limit_final"]:
        raise AssertionError("N07 static/heap ordering")
    if probe["heap_limit_before"] < limit or probe["heap_limit_policy"] != limit:
        raise AssertionError("N07 omitted SDK heap policy clamp")
    if probe["reserve_bytes"] != 655360 or limit - probe["heap_limit_final"] != 655360:
        raise AssertionError("N07 emergency reserve was not excluded from allocator")
    if probe["sdk_heap_min"] != 196608:
        raise AssertionError("N07 SDK growth minimum changed")
    available = probe["heap_limit_final"] - probe["heap_end"]
    if probe["sdk_heap_available"] != available or available < 196608:
        raise AssertionError("N07 insufficient actual SDK heap growth headroom")
    managed = probe["managed_bytes"]
    if managed:
        if probe["managed_base"] % 32 or not probe["heap_start"] <= probe["managed_base"] <= probe["heap_end"] - managed:
            raise AssertionError("N07 managed pool is not aligned live heap storage")
        if probe["malloc_request_bytes"] < managed or probe["malloc_request_bytes"] > managed + 31:
            raise AssertionError("N07 managed allocation includes hidden payload")
    startup = probe["heap_end"] - base - managed
    if startup != probe["static_startup_bytes"] or startup > 524288:
        raise AssertionError("N07 static+startup+alignment exceeds N03 code envelope")
    closure = startup + managed + available + probe["reserve_bytes"]
    if closure != limit - base:
        raise AssertionError("N07 ARM9 byte budget does not close")
    partition = compiled_memory_partitions(elf)
    if managed and managed != sum(partition['ceilings'].values()):
        raise AssertionError('Managed RAM differs from the compiled slab ledger')
    for name in ('world', 'scratch', 'scene', 'assets'):
        capacity = partition['ceilings'][name]
        if not 0 <= probe[name + "_used"] <= probe[name + "_high_water"] <= capacity:
            raise AssertionError(f"N07 {name} arena exceeded accepted capacity")
    return dict(static_bytes=static_end-base, heap_start_gap=probe["heap_start"]-static_end,
                static_startup_bytes=startup, managed_bytes=managed,
                sdk_heap_available=available, reserve_bytes=probe["reserve_bytes"],
                closure_bytes=closure, partition=partition)


def history_record(frontend, probe: dict, ordinal: int,
                   fields: list[str]) -> dict | None:
    if probe["history_capacity"] != 128 or probe["history_record_bytes"] != len(fields) * 4:
        raise ValueError("N07 history ABI changed")
    address = probe["history_address"] + ((ordinal - 1) % 128) * len(fields) * 4
    value = observe(frontend, address, fields, 0x37484B44)
    if value is None:
        return None
    if value["ordinal"] != ordinal:
        raise AssertionError("N07 ring record was overwritten before observation")
    return value


def validate_history(records: list[dict], probe: dict, immutable_cases: list[dict]) -> dict:
    if not records:
        raise AssertionError("N07 executed no observed frame records")
    steady = []
    loading = []
    diagnostic=[]
    sync=[]
    moving = []
    cases = {}
    budget_violations=[]
    for record in records:
        if record["disp3d_flags"] & 0x3000 or record["gxstat_flags"] & (1 << 15):
            raise AssertionError("Actual DS geometry/rasterizer overflow or matrix stack error")
        if record["hardware_polygons"] > 2048 or record["hardware_vertices"] > 6144:
            raise AssertionError("Actual DS geometry RAM count exceeds hardware capacity")
        if record["submitted_vertices"] != record["submitted_quads"] * 4:
            raise AssertionError("N07 submission count does not describe quads")
        if record["submitted_quads"] > record["input_quads"]:
            raise AssertionError("N07 submitted more quads than admitted")
        if record["texture_bytes"] > probe["texture_capacity_bytes"] or record["palette_bytes"] > probe["palette_capacity_bytes"]:
            raise AssertionError("N07 texture/palette working set exceeds admitted VRAM")
        delta = (record["vblank_end"] - record["vblank_start"]) & 0xFFFFFFFF
        if delta >= 0x80000000:
            raise AssertionError("N07 VBlank record ordering")
        item = dict(record, vblank_delta=delta)
        cases.setdefault(record["case_id"], []).append(item)
        if record["flags"] & 64:
            if record["flags"]!=64:
                raise AssertionError("N07 SYNC cannot hide presented/loading/diagnostic render work")
            zero_fields=("prepare_ticks","io_upload_ticks","submit_ticks","geometry_wait_ticks",
                         "input_quads","submitted_quads","submitted_vertices",
                         "hardware_polygons","hardware_vertices","uploads","upload_bytes")
            if any(record[name] for name in zero_fields):
                raise AssertionError("N07 SYNC contains geometry or upload work")
            sync.append(item)
        elif record["flags"] & 32:
            if record["flags"] & 8:
                raise AssertionError("N07 presented render frame cannot be hidden as diagnostic UI")
            diagnostic.append(item)
        elif record["flags"] & 1:
            loading.append(item)
        else:
            steady.append(item)
            if delta > 2:
                budget_violations.append(dict(case_id=record["case_id"],frame_id=record["frame_id"],
                                              reason="vblank_delta",actual=delta,limit=2))
            if record["flags"] & 2:
                moving.append(item)
    if not steady or not moving:
        raise AssertionError("N07 lacks actual steady and moving frame observations")
    if len(sync)!=len(immutable_cases):
        raise AssertionError("N07 requires exactly one inactive-to-ACTIVE synchronization per case")
    sync_links=[]
    for case in immutable_cases:
        sync_rows=[row for row in sync if row["case_id"]==case["case_id"]]
        active_rows=[row for row in steady if row["case_id"]==case["case_id"]]
        if len(sync_rows)!=1 or not active_rows:
            raise AssertionError("N07 case synchronization identity/order")
        boundary,first=sync_rows[0],active_rows[0]
        if boundary["ordinal"]+1!=first["ordinal"] or boundary["level"]!=first["level"]:
            raise AssertionError("N07 synchronization must immediately precede its first render")
        if first["vblank_start"]!=boundary["vblank_end"] or boundary["frame_id"]+1!=first["frame_id"]:
            raise AssertionError("N07 first render interval did not start at the synchronization boundary")
        sync_links.append(dict(case_id=case["case_id"],sync_ordinal=boundary["ordinal"],
                               first_frame_id=first["frame_id"],vblank=boundary["vblank_end"]))
    # Tick duration is an additional budget. A narrow VBlank-start/end bracket
    # alone could conceal a nearly-three-frame interval straddling boundaries.
    hz, divider = probe["timer_hz"], probe["timer_divider"]
    if not hz or not divider or not probe["vblank_ticks"]:
        raise AssertionError("N07 timer scale was not published")
    for row in steady:
        if row["total_ticks"] > 2 * probe["vblank_ticks"]:
            budget_violations.append(dict(case_id=row["case_id"],frame_id=row["frame_id"],
                                          reason="total_ticks",actual=row["total_ticks"],
                                          limit=2*probe["vblank_ticks"]))
    return dict(records=len(records), steady_frames=len(steady), moving_frames=len(moving),
                loading_frames=len(loading), cases={str(k):len(v) for k,v in cases.items()},
                diagnostic_records=len(diagnostic),
                sync_records=len(sync),sync_links=sync_links,
                max_sync_ticks=max((r["total_ticks"] for r in sync),default=0),
                total_sync_ticks=sum(r["total_ticks"] for r in sync),
                max_diagnostic_ticks=max((r["total_ticks"] for r in diagnostic),default=0),
                total_diagnostic_ticks=sum(r["total_ticks"] for r in diagnostic),
                max_vblank_delta=max(r["vblank_delta"] for r in steady),
                max_total_ticks=max(r["total_ticks"] for r in steady),
                max_loading_ticks=max((r["total_ticks"] for r in loading), default=0),
                timer_source_hz=hz, timer_divider=divider,
                budget_passed=not budget_violations,budget_violations=budget_violations,
                scope="emulated ARM9/GE counters; not physical DS FPS or complete rasterizer GPU time")


def nearest_div8(value: int) -> int:
    return (value + 4) // 8 if value >= 0 else -((-value + 4) // 8)


def cell_clip_matrix(projection: list[int] | tuple[int, ...], camera: tuple[int, ...],
                     origin: tuple[int, ...], sine: int, cosine: int) -> list[int]:
    """Independent, origin-linear clip matrix; one shared camera anchor."""
    if any(value % 512 for value in origin):
        raise AssertionError("N07 cell origin violates the 512-world lattice")
    anchor = tuple(value//512*512 for value in camera)
    dx,dy,dz = (anchor[i]-camera[i] for i in range(3))
    s,c = nearest_div8(sine),nearest_div8(cosine)
    rotation = [c,0,-s, 0,4096,0, s,0,c]
    columns = [sum(projection[k*4+r]*rotation[col*3+k] for k in range(3))//4096
               for col in range(3) for r in range(4)]
    translation = [(dx*c+dz*s)//512,dy*8,(dz*c-dx*s)//512+1600]
    lattice = [(origin[i]-anchor[i])//512 for i in range(3)]
    tail = [sum(projection[k*4+r]*translation[k] for k in range(3))//4096
            +projection[12+r]+sum(columns[k*4+r]*lattice[k] for k in range(3))
            for r in range(4)]
    return columns+tail


def verify_positions(frontend, probe: dict, fields: list[str]) -> dict:
    """Verify source pose -> exact matrices -> real GE test output separately."""
    if probe["projection_words"] != 16:
        raise AssertionError("N07 projection must expose sixteen matrix words")
    projection = struct.unpack("<16i", ram(frontend, probe["projection_address"], 64))
    allowed = {0:5120, 5:-10240, 8:-13, 9:26, 11:4096}
    for index, value in enumerate(projection):
        if index in (10,14):
            continue
        if value != allowed.get(index, 0):
            raise AssertionError(f"N07 off-axis projection coefficient {index} differs from N06")
    if projection[10] <= 4096 or projection[14] >= 0:
        raise AssertionError("N07 near/far depth ordering")
    near_world = Fraction(-512 * projection[14], projection[10] + 4096)
    far_world = Fraction(-512 * projection[14], projection[10] - 4096)
    if abs(near_world-200) > 1:
        raise AssertionError("N07 effective-eye near plane must retain native depth0")
    if probe["position_record_bytes"] != len(fields)*4 or not 1 <= probe["position_count"] <= 32:
        raise AssertionError("N07 actual GE PosTest records missing or ABI changed")
    results = []
    endpoints = {}
    for index in range(probe["position_count"]):
        address = probe["position_address"] + index*len(fields)*4
        row = observe(frontend, address, fields, 0x37504B44)
        if row is None or row["ordinal"] != index+1:
            raise AssertionError("N07 PosTest record is unpublished or incoherent")
        origin = tuple(signed32(row["origin_"+axis]) for axis in "xyz")
        local = tuple(signed32(row["local_"+axis]) for axis in "xyz")
        camera = tuple(signed32(row["camera_"+axis]) for axis in "xyz")
        sine, cosine = signed32(row["sin_q15"]), signed32(row["cos_q15"])
        s12, c12 = nearest_div8(sine), nearest_div8(cosine)
        if any(value % 512 for value in origin):
            raise AssertionError("N07 origin is outside accepted 512-world lattice")
        dx, dy, dz = (origin[i]-camera[i] for i in range(3))
        model12 = [c12,0,-s12, 0,4096,0, s12,0,c12,
                   (dx*c12+dz*s12)//512, dy*8,
                   (dz*c12-dx*s12)//512+1600]
        actual12 = [signed32(row[f"modelview_{i}"]) for i in range(12)]
        if actual12 != model12:
            raise AssertionError("N07 modelview differs from independent pose/lattice formula")
        expected_combined = cell_clip_matrix(projection,camera,origin,sine,cosine)
        actual_combined = [signed32(row[f"clipmatrix_{i}"]) for i in range(16)]
        if actual_combined != expected_combined:
            raise AssertionError("Actual GE combined matrix differs from independent integer composition")
        vector = [v*8 for v in local]+[4096]
        if any(not -32768 <= value <= 32767 for value in vector[:3]):
            raise AssertionError("N07 PosTest input overflows signed v16")
        expected_clip = tuple(sum(actual_combined[c*4+r]*vector[c]
                                  for c in range(4))//4096 for r in range(4))
        actual_clip = tuple(signed32(row["clip_"+axis]) for axis in "xyzw")
        if actual_clip != expected_clip:
            raise AssertionError("Actual GE PosTest differs from matrix/vector oracle")
        world = tuple(origin[i]+local[i] for i in range(3))
        key = (row["case_id"], world, camera, sine, cosine)
        endpoints.setdefault(key, []).append((origin,actual_clip))
        dx, dy, dz = (world[i]-camera[i] for i in range(3))
        lateral = Fraction(dx*cosine+dz*sine,32768)
        depth = Fraction(dz*cosine-dx*sine,32768)
        expected_xy = None
        actual_xy = None
        if actual_clip[3] > 0 and depth+200 > 0:
            expected_xy = [Fraction(1271,10)+160*lateral/(depth+200),
                           Fraction(12163,160)+Fraction(385,2)*dy/(depth+200)]
            actual_xy = [128*(Fraction(actual_clip[0],actual_clip[3])+1)-Fraction(1,2),
                         77*(1-Fraction(actual_clip[1],actual_clip[3]))-Fraction(1,2)]
        results.append(dict(ordinal=index+1,case_id=row["case_id"],world=world,
                            clip=actual_clip, matrix_exact=True,position_exact=True,
                            raw_observation=row,
                            continuous_pixel_error=[float(actual_xy[i]-expected_xy[i]) for i in range(2)]
                            if expected_xy else None))
    shared = []
    for key, entries in endpoints.items():
        if len({entry[0] for entry in entries}) < 2:
            continue
        if len({entry[1] for entry in entries}) != 1:
            raise AssertionError("Shared world endpoint differs across GPU origins: visible seam risk")
        shared.append(dict(case_id=key[0],world=key[1],origins=[v[0] for v in entries],clip=entries[0][1]))
    if not shared:
        raise AssertionError("N07 lacks shared-endpoint PosTest across different origins")
    return dict(records=results, shared_endpoints=shared, projection=list(projection),
                near_world=float(near_world),far_world=float(far_world),
                scope="exact GE matrices/vertices plus separately measured Q15-to-Q12 projection approximation")


def read_flat_frame(frontend, probe: dict) -> dict:
    if (probe["render_frame_bytes"],probe["cell_record_bytes"],probe["quad_record_bytes"]) != (32,20,92):
        raise ValueError("N07 ARM9 frame/cell/quad ABI changed")
    raw = ram(frontend,probe["render_frame_address"],32)
    x,y,z,s,c,pointer,count,generation = struct.unpack("<5i3I",raw)
    if generation != probe["render_generation"] or not 1 <= count <= 4096:
        raise AssertionError("N07 observed frame generation or cell count")
    cells = []
    total = 0
    for index in range(count):
        ox,oy,oz,quads,quad_count = struct.unpack("<3i2I",ram(frontend,pointer+20*index,20))
        if not 1 <= quad_count <= 12:
            raise AssertionError("N07 cell quad count outside geometry contract")
        values = []
        for number in range(quad_count):
            data = ram(frontend,quads+92*number,92)
            vertices = [struct.unpack_from("<5i",data,20*i) for i in range(4)]
            slot,flags,surface,color,reserved = struct.unpack_from("<HHIHH",data,80)
            if slot != 65535 or flags or reserved or not 0 < color <= 32767:
                raise AssertionError("N07 flat diagnostic contains texture/transparent/invalid material")
            expected_color = (((surface*2654435761)&0xFFFFFFFF)>>17) or 1
            if color != expected_color:
                raise AssertionError("N07 flat color does not identify its source surface")
            values.append(dict(vertices=vertices,source_surface_id=surface,color=color))
        total += quad_count
        if total > 4096:
            raise AssertionError("N07 flat diagnostic unbounded surface stream")
        cells.append(dict(origin=(ox,oy,oz),quads=values))
    return dict(camera=(x,y,z),sine=s,cosine=c,generation=generation,cells=cells,quads=total)


def clip_polygon(vertices: list[tuple[float,...]]) -> list[tuple[float,...]]:
    """Homogeneous Sutherland-Hodgman clipping; independent of C scene culling."""
    result = vertices
    for axis,sign in ((0,1),(0,-1),(1,1),(1,-1),(2,1),(2,-1)):
        if not result:
            break
        output = []
        previous = result[-1]
        previous_d = previous[3]+sign*previous[axis]
        for current in result:
            distance = current[3]+sign*current[axis]
            if (distance >= 0) != (previous_d >= 0):
                t = previous_d/(previous_d-distance)
                output.append(tuple(previous[i]+t*(current[i]-previous[i]) for i in range(4)))
            if distance >= 0:
                output.append(current)
            previous,previous_d = current,distance
        result = output
    return result


def flat_coverage(frontend, probe: dict) -> dict:
    """Check polygon union for holes and guarded winner interiors for color.

    The union mask is eroded only at its external silhouette. Internal cell
    edges remain tested for black cracks even when their color is ambiguous.
    """
    frame = read_flat_frame(frontend,probe)
    projection = struct.unpack("<16i",ram(frontend,probe["projection_address"],64))
    width,height,rgb = frontend.rgb()
    if (width,height) != (256,384):
        raise AssertionError("N07 core display must be native 256x384")
    union = bytearray(256*154)
    depth = [float("inf")]*(256*154)
    winner = [0]*(256*154)
    winner_ids = [0]*(256*154)
    color_safe = bytearray(256*154)
    boundary_guard = bytearray(256*154)
    projected = 0
    polygons=[]
    for cell in frame["cells"]:
        matrix = cell_clip_matrix(projection,frame["camera"],cell["origin"],frame["sine"],frame["cosine"])
        for quad in cell["quads"]:
            vertices = []
            for vertex in quad["vertices"]:
                vector = [v*8 for v in vertex[:3]]+[4096]
                if any(not -32768 <= v <= 32767 for v in vector[:3]):
                    raise AssertionError("Actual frame vertex exceeds GPU v16")
                vertices.append(tuple(sum(matrix[c*4+r]*vector[c] for c in range(4))//4096
                                      for r in range(4)))
            clipped = clip_polygon(vertices)
            if len(clipped) < 3:
                continue
            points = [(128*(v[0]/v[3]+1)-0.5,77*(1-v[1]/v[3])-0.5,v[2]/v[3])
                      for v in clipped if v[3]>0]
            if len(points) != len(clipped):
                raise AssertionError("N07 post-clip vertex has nonpositive W")
            projected += 1
            polygons.append(dict(source_surface_id=quad["source_surface_id"],color=quad["color"],points=points))
            # Guard every potential occluder edge, including pixels just
            # outside that polygon. Guarding only the current depth winner
            # misclassifies a foreground edge's raster uncertainty as a
            # distant wall interior. The fixed two-pixel width is unchanged.
            for p,q in zip(points,points[1:]+points[:1]):
                vx,vy=q[0]-p[0],q[1]-p[1]
                length2=vx*vx+vy*vy
                for ey in range(max(0,math.floor(min(p[1],q[1])-2)),min(153,math.ceil(max(p[1],q[1])+2))+1):
                    for ex in range(max(0,math.floor(min(p[0],q[0])-2)),min(255,math.ceil(max(p[0],q[0])+2))+1):
                        t=max(0,min(1,((ex-p[0])*vx+(ey-p[1])*vy)/length2)) if length2 else 0
                        if (ex-p[0]-t*vx)**2+(ey-p[1]-t*vy)**2<=4:
                            boundary_guard[ey*256+ex]=1
            for triangle_index in range(1,len(points)-1):
                a,b,c = points[0],points[triangle_index],points[triangle_index+1]
                area = (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
                if abs(area)<1e-9:
                    continue
                left,right = max(0,math.ceil(min(a[0],b[0],c[0]))),min(255,math.floor(max(a[0],b[0],c[0])))
                top,bottom = max(0,math.ceil(min(a[1],b[1],c[1]))),min(153,math.floor(max(a[1],b[1],c[1])))
                for y in range(top,bottom+1):
                    for x in range(left,right+1):
                        wb = ((x-a[0])*(c[1]-a[1])-(y-a[1])*(c[0]-a[0]))/area
                        wc = ((b[0]-a[0])*(y-a[1])-(b[1]-a[1])*(x-a[0]))/area
                        wa = 1-wb-wc
                        if min(wa,wb,wc)<-1e-9:
                            continue
                        index = y*256+x
                        union[index] = 1
                        z = wa*a[2]+wb*b[2]+wc*c[2]
                        # A two-pixel guard covers subpixel raster boundary
                        # choices. It is fixed before observing actual pixels.
                        edge_distance = min(abs((q[0]-p[0])*(y-p[1])-(q[1]-p[1])*(x-p[0]))/
                                            max(1e-20,math.hypot(q[0]-p[0],q[1]-p[1]))
                                            for p,q in zip(points,points[1:]+points[:1]))
                        if z < depth[index]-1e-7:
                            depth[index] = z
                            winner[index] = quad["color"]
                            winner_ids[index] = quad["source_surface_id"]
                            color_safe[index] = edge_distance >= 2
                        elif abs(z-depth[index])<=1e-7 and winner[index]!=quad["color"]:
                            color_safe[index] = 0
    holes = []
    colors = []
    covered = 0
    compared = 0
    for y in range(2,152):
        for x in range(2,254):
            index = y*256+x
            if not union[index]:
                continue
            actual = (rgb[3*index]>>3)|((rgb[3*index+1]>>3)<<5)|((rgb[3*index+2]>>3)<<10)
            if all(union[(y+dy)*256+x+dx] for dy in range(-2,3) for dx in range(-2,3)):
                covered += 1
                if actual==0 and len(holes)<32:
                    holes.append((x,y))
            if color_safe[index] and not boundary_guard[index]:
                compared += 1
                if actual!=winner[index] and len(colors)<32:
                    colors.append(dict(x=x,y=y,expected=winner[index],actual=actual,
                                       expected_source_id=winner_ids[index],expected_depth=depth[index]))
    diagnostic=dict(case_id=probe["case_id"],frame=frame,projection=list(projection),
                    projected_polygons=polygons,holes=holes,color_mismatches=colors,
                    union_interior_pixels=covered,exact_color_interior_pixels=compared)
    write_json(frontend.directory/f"case-{probe['case_id']:02}-coverage.json",diagnostic)
    expected_rgb=bytearray(256*192*3)
    for index,color in enumerate(winner):
        for channel in range(3):
            value=(color>>(5*channel))&31
            expected_rgb[3*index+channel]=(value*255+15)//31
    expected_path=safe_output(frontend.directory/f"case-{probe['case_id']:02}-expected.png")
    transport.png(expected_path,256,192,expected_rgb)
    if covered<128 or compared<64:
        raise AssertionError("N07 flat pass lacks meaningful interior coverage")
    if holes or colors:
        raise AssertionError(f"N07 flat coverage mismatch: holes={holes}, colors={colors}")
    return dict(quads=frame["quads"],projected_polygons=projected,
                union_interior_pixels=covered,exact_color_interior_pixels=compared,
                guard_pixels=2,internal_edges_included_in_hole_check=True,
                render_generation=frame["generation"],passed=True)


def read_fixture(path: Path) -> tuple[bytes,list[dict],dict]:
    data = path.read_bytes()
    if len(data)<64 or data[:8]!=b"N07SCN1\0":
        raise ValueError("N07 immutable scene input magic")
    version,count,record_bytes,level_bytes,total_bytes,reserved = struct.unpack_from("<6I",data,8)
    if (version,record_bytes,level_bytes,reserved)!=(1,64,10240,0) or not 1<=count<=64:
        raise ValueError("N07 immutable scene input header")
    if total_bytes!=len(data) or len(data)!=64+count*64+3*10240:
        raise ValueError("N07 immutable scene input exact length")
    if hashlib.sha256(data[64:]).digest()!=data[32:64]:
        raise ValueError("N07 immutable scene input body SHA")
    cases=[]
    ids=set()
    names=("case_id","level","x","y","z","heading","dx","dz","dheading",
           "frames","flags","source_kind","cell_x","cell_z","reserved0","reserved1")
    for index in range(count):
        values=list(struct.unpack_from("<16I",data,64+index*64))
        for signed in (2,3,4,6,7,8):
            values[signed]=signed32(values[signed])
        row=dict(zip(names,values))
        if not row["case_id"] or row["case_id"] in ids or not 1<=row["level"]<=3:
            raise ValueError("N07 case identity/level")
        if row["heading"]>360 or not 1<=row["frames"]<=65536 or row["flags"]&~7 or row["reserved0"] or row["reserved1"]:
            raise ValueError("N07 case bounded profile")
        ids.add(row["case_id"])
        cases.append(row)
    # Reconstruct every diagnostic HUD pixel against the accepted N06 bitmap,
    # without executing previous-task scripts or using a new layout oracle.
    summary=json.loads((ROOT/"analysis/nds-n06-visual.json").read_text("utf8"))
    manifest_pin=summary["references"]["manifest"]
    manifest_path=ROOT/manifest_pin["path"]
    if transport.sha(manifest_path)!=manifest_pin["sha256"]:
        raise AssertionError("Accepted N06 reference manifest changed")
    reference_manifest=json.loads(manifest_path.read_text("utf8"))
    hud=[]
    for level in range(1,4):
        offset=64+count*64+(level-1)*10240
        palette=struct.unpack_from("<256H",data,offset)
        indices=data[offset+512:offset+10240]
        if any(word&32768 for word in palette) or any(index==0 for index in indices):
            raise ValueError("N07 opaque diagnostic HUD palette/index schema")
        actual=tuple(palette[index] for index in indices)
        reference=ROOT/f"build/nds/n06/frames/v{(level-1)*2:02}top.bin"
        reference_pin=reference_manifest["pairs"][(level-1)*2]["top"]["viewer"]
        reference_bytes=reference.read_bytes()
        if len(reference_bytes)!=98304 or transport.sha(reference)!=reference_pin["sha256"]:
            raise ValueError("Accepted N06 HUD source length changed")
        expected=tuple(word&32767 for word in struct.unpack("<49152H",reference_bytes)[256*154:])
        if actual!=expected:
            raise AssertionError("N07 HUD pixels differ from accepted N06 initial state")
        hud.append(dict(level=level,pixels=9728,reference_sha256=transport.sha(reference),passed=True))
    return data,cases,dict(bytes=len(data),sha256=transport.sha(path),cases=count,hud=hud)


def verify_runtime_limits(probe: dict) -> dict:
    banks=[probe["vram_"+letter] for letter in "abcdefghi"]
    expected=[131,139,132,155,129,131,128,128,128]
    if banks!=expected:
        raise AssertionError(f"N07 VRAM mapping differs from architecture: {banks}")
    if probe["texture_capacity_bytes"]!=384*1024 or probe["palette_capacity_bytes"]!=16*1024:
        raise AssertionError("N07 admitted texture/palette bank capacities changed")
    if probe["texture_bytes"]>384*1024 or probe["palette_bytes"]>16*1024:
        raise AssertionError("N07 actual working set exceeds VRAM admission")
    if probe["disp3d_sticky"]&0x3000 or probe["gxstat_sticky"]&(1<<15):
        raise AssertionError("N07 sticky DS overflow/matrix error")
    if probe["negative_pass_mask"]!=255:
        raise AssertionError("N07 eight invalid-input admission scenarios did not all preserve state")
    return dict(banks=banks,texture_bytes=probe["texture_bytes"],palette_bytes=probe["palette_bytes"],
                texture_capacity=384*1024,palette_capacity=16*1024,
                rdlines_min=probe["rdlines_min"],rdlines_max=probe["rdlines_max"],
                rdlines_samples=probe["rdlines_samples"])


def run_renderer(frontend, address: int, probe_fields: list[str], cases: list[dict],
                 elf: Path, report: dict, provisional: bool, packet: bytes) -> None:
    header=ROOT/"ports/nds/platform/render_probe/render_probe.h"
    history_fields=scalar_fields(header,"NdsRenderHistoryRecord")
    position_fields=scalar_fields(header,"NdsRenderPositionRecord")
    history=[]
    report["history_records"]=history
    ready_cases=[]
    ordinal=0
    stable_key=None
    stable_count=0
    next_case=0
    joypad=0
    moving_hashes={}
    final=None
    for _ in range(40000):
        frontend.joypad=joypad
        joypad=0
        if frontend.frame_time and frontend.frame_time.callback:
            frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
        frontend.core.retro_run()
        frontend.frames+=1
        if frontend.callback_error or frontend.shutdown:
            raise RuntimeError(frontend.callback_error or "Core shutdown")
        probe=observe(frontend,address,probe_fields,0x37524B44)
        if probe is None:
            continue
        report["last_observation"]=probe
        if probe["status"]==3 or probe["error_bits"]:
            raise AssertionError("ARM9 renderer published failure")
        total=probe["history_total"]
        if total<ordinal or total-ordinal>128:
            raise AssertionError("N07 frame history lost observations")
        for number in range(ordinal+1,total+1):
            row=history_record(frontend,probe,number,history_fields)
            if row is None:
                break
            history.append(row)
            ordinal=number
        if probe["flags"]&2 and frontend.last_video:
            moving_hashes.setdefault(probe["case_id"],set()).add(hashlib.sha256(frontend.rgb()[2][:256*154*3]).hexdigest())
        if probe["status"]==2:
            if next_case!=len(cases):
                raise AssertionError("N07 completed before every immutable case was observed")
            final=probe
            break
        if probe["status"]!=4:
            stable_key=None
            stable_count=0
            continue
        if next_case>=len(cases):
            continue
        expected=cases[next_case]
        if probe["case_id"]!=expected["case_id"]:
            # An A transition can span one frontend callback boundary. Only
            # the just-completed case may remain visible until release.
            if next_case and probe["case_id"]==cases[next_case-1]["case_id"]:
                continue
            raise AssertionError("N07 READY case order differs from immutable packet")
        key=(probe["case_id"],probe["frame_id"],probe["presented_frame_id"],probe["render_generation"])
        stable_count=stable_count+1 if key==stable_key else 1
        stable_key=key
        if stable_count<8:
            continue
        if probe["case_count"]!=len(cases) or probe["level"]!=expected["level"]:
            raise AssertionError("N07 READY case count/level mismatch")
        last=expected["frames"]-1
        heading=(expected["heading"]+expected["dheading"]*last)%360
        sine=int(math.sin(heading*3.14159265359/180.0)*32768)
        cosine=int(math.cos(heading*3.14159265359/180.0)*32768)
        expected_camera=(expected["x"]+expected["dx"]*last,expected["y"],
                         expected["z"]+expected["dz"]*last,sine,cosine)
        actual_camera=tuple(signed32(probe[name]) for name in ("camera_x","camera_y","camera_z","sin_q15","cos_q15"))
        if expected_camera!=actual_camera:
            raise AssertionError(f"N07 immutable camera sequence mismatch: {actual_camera} != {expected_camera}")
        case_report=dict(case_id=expected["case_id"],level=expected["level"],camera=actual_camera,
                         memory=verify_memory(frontend,probe,elf),vram=verify_runtime_limits(probe),
                         coherent_ready_frames=8)
        if probe["source_packet_bytes"]!=len(packet) or ram(frontend,probe["source_packet_address"],len(packet))!=packet:
            raise AssertionError("Actual ARM9 immutable packet differs from host input")
        offset=64+len(cases)*64+(expected["level"]-1)*10240
        palette=struct.unpack_from("<256H",packet,offset)
        width,height,rgb=frontend.rgb()
        if (width,height)!=(256,384):
            raise AssertionError("N07 display size changed")
        for index,palette_index in enumerate(packet[offset+512:offset+10240]):
            pixel=256*154+index
            actual=(rgb[3*pixel]>>3)|((rgb[3*pixel+1]>>3)<<5)|((rgb[3*pixel+2]>>3)<<10)
            if actual!=palette[palette_index]:
                raise AssertionError(f"Actual ARM9 HUD pixel differs at ({pixel%256},{pixel//256})")
        case_report["hud_pixels_exact_rgb555"]=9728
        case_report["immutable_packet_ram_exact"]=True
        flat=bool(expected["flags"]&1)
        if bool(probe["flat_diagnostic"])!=flat:
            raise AssertionError("N07 case flat/textured state mismatch")
        if flat:
            case_report["coverage"]=flat_coverage(frontend,probe)
        case_report["images"]=capture(frontend,f"case-{expected['case_id']:02}")
        ready_cases.append(case_report)
        report["cases"]=ready_cases
        next_case+=1
        stable_key=None
        stable_count=0
        joypad=1<<8  # libretro JOYPAD_A; actual emulated input, never RAM writes.
    if final is None:
        raise AssertionError("N07 full renderer did not complete within bounded observation window")
    report["history_records"]=history
    report["history"]=validate_history(history,final,cases)
    for case in cases:
        rows=[row for row in history if row["case_id"]==case["case_id"] and not row["flags"]&(1|32|64)]
        if len(rows)!=case["frames"]:
            raise AssertionError("N07 observed frame count differs from immutable case")
        for index,row in enumerate(rows):
            heading=(case["heading"]+case["dheading"]*index)%360
            expected=(case["x"]+case["dx"]*index,case["y"],case["z"]+case["dz"]*index,
                      int(math.sin(heading*3.14159265359/180.0)*32768),
                      int(math.cos(heading*3.14159265359/180.0)*32768))
            actual=tuple(signed32(row[name]) for name in ("camera_x","camera_y","camera_z","sin_q15","cos_q15"))
            if actual!=expected:
                raise AssertionError("N07 moving/static history pose differs from immutable sequence")
        if case["flags"]&2 and len(moving_hashes.get(case["case_id"],set()))<3:
            raise AssertionError("N07 moving case did not produce distinct displayed scene frames")
    report["moving_distinct_scene_hashes"]={str(k):len(v) for k,v in moving_hashes.items()}
    report["positions"]=verify_positions(frontend,final,position_fields)
    report["memory"]=verify_memory(frontend,final,elf)
    if not report["history"]["budget_passed"]:
        raise AssertionError("N07 heavy/static/moving renderer exceeded two VBlank; see complete history")
    report["passed"]=True
    report["renderer_readiness_proven"]=not provisional


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=OUT/"dok_nds_renderer.nds")
    parser.add_argument("--elf", type=Path, default=OUT/"dok_nds_renderer.elf")
    parser.add_argument("--geometry-only", action="store_true",
                        help="only GE matrix/shared-endpoint gate, never renderer readiness")
    parser.add_argument("--packet", type=Path, default=OUT/"scene.dat")
    parser.add_argument("--manifest", type=Path, default=OUT/"scene-manifest.json")
    parser.add_argument("--allow-provisional", action="store_true",
                        help="debug provisional poses; renderer readiness remains false")
    args = parser.parse_args()
    directory = OUT/"emulator"/("geometry-only" if args.geometry_only else "renderer")
    prepare_directory(directory)
    result_path = directory/"result.json"
    report = dict(task="N07",passed=False,geometry_only=args.geometry_only,
                  scope="actual GE projection/PosTest/shared endpoints only" if args.geometry_only
                  else "actual ARM9 3D renderer",
                  python=sys.version,command=sys.argv,
                  limitations=["Emulator only; physical DS/DS Lite/FPS remain N24.",
                               "GE busy/PosTest are not full rasterizer execution time."])
    write_json(result_path,report)
    frontend = None
    try:
        if transport.sha(args.core) != CORE_SHA:
            raise ValueError("N07 core differs from accepted melonDS DS 1.4.0 binary")
        header = ROOT/"ports/nds/platform/render_probe/render_probe.h"
        probe_fields = scalar_fields(header,"NdsRenderProbe")
        position_fields = scalar_fields(header,"NdsRenderPositionRecord")
        address,size = transport.elf_symbol(args.elf,"g_nds_render_probe")
        if size != len(probe_fields)*4:
            raise ValueError("N07 header/ELF observation ABI size mismatch")
        report.update(rom_sha256=transport.sha(args.rom),elf_sha256=transport.sha(args.elf),
                      core_sha256=transport.sha(args.core),harness_sha256=transport.sha(Path(__file__)),
                      abi_sha256=transport.sha(header),transport_sha256=transport.sha(Path(transport.__file__)),
                      observation_symbol=dict(address=address,bytes=size,access="read-only"))
        fixture_path=None
        fixture_after_load=None
        cases=[]
        provisional=False
        if not args.geometry_only:
            packet,cases,fixture_report=read_fixture(args.packet)
            manifest=json.loads(args.manifest.read_text("utf8"))
            provisional=bool(manifest.get("provisional",True))
            if provisional and not args.allow_provisional:
                raise ValueError("Provisional scene candidates cannot establish N07 readiness")
            if manifest["packet"]["sha256"]!=hashlib.sha256(packet).hexdigest():
                raise AssertionError("N07 manifest does not identify immutable packet")
            if len(manifest["cases"])!=len(cases):
                raise AssertionError("N07 manifest case count")
            for row,expected in zip(cases,manifest["cases"]):
                for name,value in row.items():
                    if name.startswith("reserved"):
                        continue
                    if expected.get("id" if name=="case_id" else name)!=value:
                        raise AssertionError("N07 manifest camera differs from packet")
            package=ROOT/"build/nds/assets/dok-assets.ndspak"
            if transport.sha(package)!=PACKAGE_SHA:
                raise AssertionError("Accepted N04 package changed")
            report.update(fixture=fixture_report,manifest_sha256=transport.sha(args.manifest),
                          provisional=provisional,package_sha256=PACKAGE_SHA)
        frontend = transport.Frontend(args.core,directory,"missing-media" if args.geometry_only else "happy")
        if not args.geometry_only:
            fixture_path=safe_output(frontend.save/"melonDS DS/dldi_sd_card.bin")
            fat.write_image(fixture_path,[("assets.ndp",b"ASSETS  NDP",package.read_bytes()),
                                          ("scene.dat",b"SCENE   DAT",packet)])
            report["fat_before_load_sha256"]=transport.sha(fixture_path)
        frontend.load(args.rom)
        report.update(core_name=frontend.info.library_name.decode(),
                      core_version=frontend.info.library_version.decode())
        if not args.geometry_only:
            transport.flush_native_stdio()
            fixture_after_load=transport.sha(fixture_path)
            report["fat_after_load_sha256"]=fixture_after_load
            run_renderer(frontend,address,probe_fields,cases,args.elf,report,provisional,packet)
            transport.flush_native_stdio()
            report["fat_after_run_sha256"]=transport.sha(fixture_path)
            if report["fat_after_run_sha256"]!=fixture_after_load:
                raise AssertionError("N07 runtime modified immutable FAT fixture")
        stable = []
        for _ in range(1200 if args.geometry_only else 0):
            if frontend.frame_time and frontend.frame_time.callback:
                frontend.frame_time.callback(round(1000000/frontend.av.timing.fps))
            frontend.core.retro_run()
            frontend.frames += 1
            if frontend.callback_error or frontend.shutdown:
                raise RuntimeError(frontend.callback_error or "Core shutdown")
            value = observe(frontend,address,probe_fields,0x37524B44)
            if value is None:
                continue
            report["last_observation"] = value
            if value["status"] == 3 or value["error_bits"]:
                raise AssertionError("ARM9 GE probe reports failure")
            if value["status"] == 2:
                stable.append(value)
                if len(stable) >= 3:
                    break
        if args.geometry_only and len(stable) != 3:
            raise AssertionError("No completed coherent ARM9 GE probe within observation window")
        if args.geometry_only and (frontend.core.retro_get_memory_size(2) != 4194304 or stable[-1]["is_dsi"]):
            raise AssertionError("GE-only gate did not execute in ordinary DS mode")
        if args.geometry_only:
            report["positions"] = verify_positions(frontend,stable[-1],position_fields)
            report["images"] = capture(frontend,"geometry-final")
            report["passed"] = True
            report["renderer_readiness_proven"] = False
    except Exception:
        report["passed"] = False
        report["error"] = traceback.format_exc()
    finally:
        if frontend:
            report["core_options"] = {k:v.decode() for k,v in frontend.variables.items()}
            report["frontend_frames"] = frontend.frames
            if not report["passed"] and frontend.last_video:
                report["failure_images"] = capture(frontend,"failure")
            try:
                frontend.close()
            except Exception:
                report["cleanup_error"] = traceback.format_exc()
                report["passed"] = False
            if not args.geometry_only and fixture_path is not None and fixture_after_load is not None:
                transport.flush_native_stdio()
                report["fat_after_unload_sha256"]=transport.sha(fixture_path)
                report["fat_runtime_unchanged"]=report["fat_after_unload_sha256"]==fixture_after_load
                if not report["fat_runtime_unchanged"]:
                    report["passed"]=False
                    report["fat_error"]="Immutable FAT image changed during renderer execution/unload"
        write_json(result_path,report)
    print(f"N07 {'geometry-only' if args.geometry_only else 'renderer'}: "
          f"{'PASS' if report['passed'] else 'FAIL'}; {result_path}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
