"""Initial-world geometry template storage census, without a runtime cache or speed claim."""
import argparse
import collections
import json
import os
from pathlib import Path
import subprocess

from audit_pipeline import ROOT, pin


import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'map'))
from compiler_cache import compiler_environment


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT/'research/evidence/nds/n23') and not output.exists()
    target = ROOT/'build/nds/n23/topology-census'
    target.mkdir(exist_ok=True)
    sources = ['ports/nds/tests/geometry_dump.c', 'ports/nds/render/geometry.c',
               'ports/nds/core/world.c', 'ports/nds/core/sha256.c']
    before = [pin(ROOT/p) for p in sources]
    zig = Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env = compiler_environment(ROOT)
    command = [str(zig), 'cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-pedantic', '-Iports/nds/include', *sources, '-o', str(target/'geometry_dump.exe')]
    subprocess.run(command, cwd=ROOT, env=env, capture_output=True, check=True)
    assert before == [pin(ROOT/p) for p in sources]
    levels = []
    for level in (1, 2, 3):
        source = ROOT/f'build/nds/n07/geometry-level{level}.cp866'
        bindings = ROOT/f'build/nds/n07/geometry-bindings{level}.bin'
        run = subprocess.run([target/'geometry_dump.exe', source, bindings],
                             capture_output=True, text=True, check=True)
        cells = collections.defaultdict(list)
        for line in run.stdout.splitlines():
            row = list(map(int, line.split()))
            assert len(row) == 29
            x, z = row[:2]
            quad = row[3:]
            for vertex in range(4):
                quad[6+vertex*5] -= x*480
                quad[8+vertex*5] -= z*480
            cells[x, z].append(tuple(quad))
        templates = {tuple(cells.get((x, z), [])) for z in range(64) for x in range(64)}
        faces = sum(len(t) for t in templates)
        all_faces = sum(len(t) for t in cells.values())
        levels.append(dict(level=level, inputs=[pin(source), pin(bindings)],
            cells_with_faces=len(cells), all_faces=all_faces, unique_templates=len(templates),
            template_face_histogram=dict(sorted(collections.Counter(len(t) for t in templates).items())),
            template_faces=faces, fixed_max_cell_storage_bytes=4096*(12*92+4),
            active_quad_storage_bytes=all_faces*92, shared_template_quad_bytes=faces*92,
            cell_index_u16_bytes=8192))
    result = dict(task='N23', passed=True, acceptance_eligible=False, scope=__doc__,
        driver=pin(Path(__file__)), compiler_command=command, compiler=pin(zig) if zig.is_relative_to(ROOT) else str(zig),
        sources=before, levels=levels,
        limitations=['Initial N07 source maps/bindings only; no live mutations or current animation state.',
            'Source cell identity and surface ID must be reconstructed when using a translated template.',
            'No cache, allocator, invalidation protocol, ARM speed or GPU pixel equivalence is proved.',
            'Storage figures omit template metadata/alignment/capacity overflow and do not reserve RAM.'])
    output.write_text(json.dumps(result, indent=2)+'\n', 'utf8')
    print(json.dumps(dict(passed=True, output=pin(output), levels=levels)))


if __name__ == '__main__':
    main()
