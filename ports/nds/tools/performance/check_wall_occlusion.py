"""Count whole quads covered by a foreground opaque wall in real NDS scenes."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'ports/nds/tools'))
from check_items import artifact, run
from map.compiler_cache import compiler_environment

# glViewport(0, 38, 255, 191). Coordinates are centred, with eight
# fractional bits. The absolute viewport offset cancels in edge tests.
WIDTH, HEIGHT, SUBPIXEL = 256, 154, 256
HALF_WIDTH, HALF_HEIGHT = WIDTH * SUBPIXEL // 2, HEIGHT * SUBPIXEL // 2


def make_dump(out):
    backend = ROOT / 'ports/nds/platform/render_probe/render_backend.c'
    source = backend.read_text('utf8')
    parts = [
        source[source.index('static const int32_t projection['):source.index('typedef struct ResidentTexture')],
        source[source.index('static int64_t floor_div('):source.index('DOK_NDS_HOT_CODE static dok_nds_render_result fence(')],
        source[source.index('DOK_NDS_HOT_CODE static dok_nds_render_result modelview('):
               source.index('static dok_nds_render_result cell_clip(')],
    ]
    loader = ROOT / 'ports/nds/tests/scene_prepare_equivalence.c'
    source = loader.read_text('utf8')
    start = source.index('static dok_level_definition')
    end = source.index('static int normalize(')
    main = source[source.index('int main('):source.index('    baseline_dok_render_scene_init(&old_scene);')]
    main = main.replace(',cases=0,errors=0', '').replace('uint64_t compared_bytes=0;', '')
    generated = out / 'geometry-dump.c'
    generated.write_text(
        '#include "static_geometry.h"\n#include "dok/render_scene.h"\n#include "render_backend.h"\n'
        '#include <inttypes.h>\n#include <stdio.h>\n'
        '#define DOK_NDS_HOT_CODE\n'
        'typedef struct {int32_t m[12];} m4x3;\ntypedef struct {int32_t m[16];} m4x4;\n'
        'static dok_render_scene scene;\nstatic _Alignas(32) unsigned char backing[32768];\n'
        + '\n'.join(parts) + source[start:end] + main + r'''
    uint32_t used;
    if(!dok_static_geometry_load(&world,bindings,backing,sizeof(backing),&used))return 7;
    dok_render_scene_init(&scene);
    while(scanf("%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNu32,&camera.x,&camera.y,&camera.z,&heading)==4){
        const dok_nds_render_frame *frame;
        if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 8;
        if(dok_render_scene_prepare(&scene,&world,bindings,&camera,heading,&frame)!=DOK_RENDER_SCENE_OK)return 9;
        CameraPlan plan;
        if(camera_plan(&camera,&plan)!=DOK_NDS_RENDER_OK)return 10;
        printf("view %d %d %d %u %u\n",camera.x,camera.y,camera.z,heading,scene.counts.published_quads);
        for(uint32_t n=0;n<frame->cell_count;++n){
            const dok_nds_render_cell *cell=&frame->cells[n];
            m4x4 clip;
            if(planned_cell_clip(&plan,cell,&clip)!=DOK_NDS_RENDER_OK)return 11;
            for(uint32_t q=0;q<cell->quad_count;++q){
                const dok_nds_render_quad *quad=&cell->quads[q];
                uint32_t id=quad->source_surface_id-1u,grid=id/DOK_GEOMETRY_CELL_MAX,count;
                const dok_static_quad *span=dok_static_geometry_span(&world,bindings,grid%64u,grid/64u,&count);
                uint32_t ordinal=id%DOK_GEOMETRY_CELL_MAX;
                if(!span||ordinal>=count)return 12;
                const dok_static_quad *source=&span[ordinal];
                int wall=source->kind==DOK_GEOMETRY_WALL&&!(source->image_flags&8u);
                for(uint32_t m=0;m<scene.counts.merged_quads;++m)
                    if(scene.merges[m].retained_surface_id==quad->source_surface_id)wall=0;
                printf("%u %d",grid,wall);
                for(uint32_t k=0;k<4u;++k){
                    const dok_nds_render_vertex *v=&quad->vertex[k];
                    for(uint32_t r=0;r<4u;++r){
                        int64_t sum=(int64_t)clip.m[r]*v->x+(int64_t)clip.m[4+r]*v->y+
                            (int64_t)clip.m[8+r]*v->z;
                        printf(" %" PRId64,floor_div(sum,512)+clip.m[12+r]);
                    }
                }
                puts("");
            }
        }
        if(dok_render_scene_release(&scene,frame->generation)!=DOK_RENDER_SCENE_OK)return 13;
    }
    return 0;
}
''', encoding='ascii')
    executable = out / 'geometry-dump.exe'
    zig = Path.home() / 'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    sources = ['ports/nds/render/' + name + '.c' for name in ('scene', 'geometry', 'static_geometry')]
    sources += ['ports/nds/core/world.c', 'ports/nds/core/sha256.c']
    run([zig, 'cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
         '-DN23_STATIC_GEOMETRY=1', '-DN23_FUSED_CULL=1', '-Iports/nds/include',
         '-Iports/nds/render', '-Iports/nds/platform/render_probe', '-Ibuild/nds/n07/generated',
         generated, *sources, '-o', executable], env=compiler_environment(ROOT))
    return executable


def screen_quad(fields):
    assert len(fields) == 16
    vertices = [fields[i:i+4] for i in range(0,16,4)]
    # Keep the probe away from near/far clipping and perspective singularities.
    if any(w <= 256 or abs(z) + 8 >= w for x, y, z, w in vertices):
        return None
    return [(x * HALF_WIDTH // w, y * HALF_HEIGHT // w, z * 65536 // w, w)
            for x, y, z, w in vertices]


def wall_edges(wall):
    area = sum(a[0]*b[1] - a[1]*b[0] for a, b in zip(wall, wall[1:] + wall[:1]))
    if not area:
        return None
    sign = 1 if area > 0 else -1
    edges = []
    for a, b in zip(wall, wall[1:] + wall[:1]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        if not dx and not dy:
            return None
        # Expand the erosion by two pixels in each screen dimension; the
        # extra subpixel covers conversion to our Q8 screen coordinates.
        margin = 513 * (abs(dx) + abs(dy))
        edges.append((-dy*sign, dx*sign, (dy*a[0] - dx*a[1])*sign - margin))
    if any(a*x + b*y + c + 513*(abs(a)+abs(b)) < 0
           for a, b, c in edges for x, y, *_ in wall):
        return None
    return edges


def coarse_coverage(walls, size=16):
    """Assign each fully covered tile its closest accepted blocker."""
    width, height = WIDTH // size, (HEIGHT + size - 1) // size
    step = size * SUBPIXEL
    left, top = -HALF_WIDTH, -HALF_HEIGHT
    tiles = [None] * (width * height)
    for far_z, far_w, edges, points in walls:
        xs, ys = [p[0] for p in points], [p[1] for p in points]
        # Only tiles entirely inside the wall bounds can pass the edge test.
        min_x = max(0, (min(xs) - left + step - 1) // step)
        max_x = min(width, (max(xs) - left) // step)
        min_y = max(0, (min(ys) - top + step - 1) // step)
        max_y = height if max(ys) >= HALF_HEIGHT else (max(ys) - top) // step
        for ty in range(min_y, max_y):
            for tx in range(min_x, max_x):
                slot = ty * width + tx
                if tiles[slot] and tiles[slot][1] <= far_w:
                    continue
                x, y = left + tx * step, top + ty * step
                row_step = min(step, HALF_HEIGHT - y)
                if all(a * (x + (step if a < 0 else 0)) +
                       b * (y + (row_step if b < 0 else 0)) + c > 0 for a,b,c in edges):
                    tiles[slot] = (far_z, far_w)
    return width, height, step, tiles


def coarse_occluded(bounds, points, near_z, near_w):
    width, height, step, tiles = bounds
    xs, ys = [p[0]+HALF_WIDTH for p in points], [p[1]+HALF_HEIGHT for p in points]
    if min(xs) >= WIDTH*SUBPIXEL or min(ys) >= HEIGHT*SUBPIXEL:
        return False
    min_x, max_x = max(0, min(xs)//step), min(width-1, max(xs)//step)
    min_y, max_y = max(0, min(ys)//step), min(height-1, max(ys)//step)
    # Empty or only clipped bounds do not establish occlusion.
    if min_x > max_x or min_y > max_y:
        return False
    for ty in range(min_y, max_y+1):
        for tx in range(min_x, max_x+1):
            depths = tiles[ty*width+tx]
            if not depths or near_z <= depths[0] or near_w <= depths[1]:
                return False
    return True


def classify(lines):
    quads, walls = [], []
    for line in lines:
        cell, opaque, *wire = map(int, line.split())
        points = screen_quad(wire)
        quads.append((cell, points))
        if opaque and points:
            edges = wall_edges(points)
            if edges:
                walls.append((max(p[2] for p in points) + 8, max(p[3] for p in points) + 16, edges, points))
    bounds = coarse_coverage(walls)
    hidden = coarse_hidden = 0
    by_cell = {}
    for cell, points in quads:
        counts = by_cell.setdefault(cell, [0, 0, 0])
        counts[0] += 1
        if not points:
            continue
        near_z, near_w = min(p[2] for p in points), min(p[3] for p in points)
        if any(near_z > far_z and near_w > far_w and
               all(a*x+b*y+c > 0 for a,b,c in edges for x,y,*_ in points)
               for far_z,far_w,edges,_ in walls):
            hidden += 1
            counts[1] += 1
        if coarse_occluded(bounds, points, near_z, near_w):
            coarse_hidden += 1
            counts[2] += 1
    return dict(submitted=len(quads), blocker_walls=len(walls), covered=hidden,
                covered_cells=sum(total==covered for total,covered,_ in by_cell.values()),
                coarse_covered=coarse_hidden,
                coarse_covered_cells=sum(total==covered for total,_,covered in by_cell.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('out', type=Path)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    executable = make_dump(out)
    controls = [(31,47), (23,47), (19,52), (41,47)]
    poses = [(x*480+240, -300, z*480+240, h)
             for x,z in controls
             for h in range(0,360,5)]
    level = ROOT / 'build/nds/n07/geometry-level1.cp866'
    bindings = ROOT / 'build/nds/n07/geometry-bindings1.bin'
    lines = iter(subprocess.check_output([executable, level, bindings],
        input=''.join('%d %d %d %d\n' % pose for pose in poses), text=True, cwd=ROOT,
        env=compiler_environment(ROOT)).splitlines())
    rows = []
    for pose in poses:
        tag, *header = next(lines).split()
        x,y,z,heading,count = map(int, header)
        assert tag == 'view' and (x,y,z,heading) == pose
        result = classify([next(lines) for _ in range(count)])
        rows.append(dict(pose=pose, **result))
    assert next(lines, None) is None
    summary = []
    for x,z in controls:
        subset = [row for row in rows if (row['pose'][0]//480,row['pose'][2]//480)==(x,z)]
        totals = {k:sum(row[k] for row in subset)
                  for k in ('submitted','covered','coarse_covered','coarse_covered_cells')}
        summary.append(dict(cell=[x,z], poses=len(subset), **totals))
    source_pins = [artifact(ROOT / path) for path in (
        'ports/nds/render/scene.c', 'ports/nds/render/static_geometry.c',
        'ports/nds/platform/render_probe/render_backend.c',
        'ports/nds/render/materials.c', 'ports/nds/tests/scene_prepare_equivalence.c',
    )]
    report = dict(scope='Shadow count using quantized clip transforms. No geometry is removed. '
                        'GE raster equivalence and ARM9 cost are not measured.',
                  sources=source_pins, inputs=[artifact(level),artifact(bindings)],
                  summary=summary, rows=rows)
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n', 'utf8')
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
