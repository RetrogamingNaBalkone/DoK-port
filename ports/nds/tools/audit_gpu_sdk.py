"""Record the installed primary SDK inputs used for N07 architecture."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SDK = Path(os.environ.get("USERPROFILE", "C:/Users/dspev")) / ".local/msys64/opt/wonderful/thirdparty/blocksds/core"
FILES = (
    "libs/libnds/include/nds/arm9/videoGL.h",
    "libs/libnds/include/nds/arm9/video.h",
    "libs/libnds/include/nds/arm9/postest.h",
    "libs/libnds/include/nds/system.h",
    "examples/graphics_3d/paletted_textures/source/main.c",
    "examples/graphics_3d/2d_and_3d/source/main.c",
    "examples/graphics_3d/box_test/source/main.c",
    "docs/content/tutorial/intermediate/3d_graphics/index.md",
)


def main() -> None:
    records = []
    for relative in FILES:
        data = (SDK / relative).read_bytes()
        records.append({"path": str(SDK / relative), "bytes": len(data),
                        "sha256": hashlib.sha256(data).hexdigest()})
    gl = (SDK / FILES[0]).read_text(encoding="utf-8")
    video = (SDK / FILES[1]).read_text(encoding="utf-8")
    checks = {
        "vertex16_signed_short": "typedef short int v16;" in gl,
        "matrix_4x3_supported": "glLoadMatrix4x3" in gl,
        "matrix_4x4_supported": "glLoadMatrix4x4" in gl,
        "line_underflow_bit12": "GL_COLOR_UNDERFLOW = (1 << 12)" in gl,
        "polygon_overflow_bit13": "GL_POLY_OVERFLOW   = (1 << 13)" in gl,
        "palette_E_F_G": all(f"VRAM_{bank}_TEX_PALETTE" in video for bank in "EFG"),
        "palette_H_forbidden": "VRAM_H_TEX_PALETTE" not in video,
        "actual_geometry_counters": all(name in video for name in
            ("GFX_VERTEX_RAM_USAGE", "GFX_POLYGON_RAM_USAGE", "GFX_RDLINES_COUNT")),
    }
    output = ROOT / "research/evidence/nds/n07/sdk-gpu-sources.json"
    for parent in (ROOT / "research", ROOT / "research/evidence",
                   ROOT / "research/evidence/nds", output.parent):
        if parent.is_symlink() or parent.is_junction():
            raise RuntimeError(f"Refuse linked output ancestor: {parent}")
    if output.is_symlink() or (output.exists() and output.stat().st_nlink != 1):
        raise RuntimeError("Refuse linked report")
    output.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "schema_version": 1, "passed": all(checks.values()),
        "scope": "Installed SDK source inventory and API checks; no original EXE analysis, no runtime proof",
        "sdk_root": str(SDK), "sources": records, "checks": checks,
        "online_primary_sources": [
            "https://blocksds.skylyrac.net/libnds/videoGL_8h.html",
            "https://blocksds.skylyrac.net/libnds/video_8h.html",
            "https://blocksds.skylyrac.net/tutorial/intermediate/3d_graphics/",
            "https://problemkaputt.de/gbatek-ds-3d-status.htm",
        ],
        "online_checked_date": "2026-10-29",
    }
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"N07 SDK GPU inventory: {'PASS' if report['passed'] else 'FAIL'}; {len(records)} source files")
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
