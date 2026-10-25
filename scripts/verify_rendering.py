"""Aggregate G04 reproducible Ghidra and host-probe evidence.

Run scripts/analyze-rendering.ps1 first (exclusive access to both projects).
This command repeats data/math probes and verifies original/evidence hashes;
it does not claim full DOS frame equivalence or measured GBA performance.
"""
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def artifact(path):
    return {'path':path.relative_to(ROOT).as_posix(),'size':path.stat().st_size,'sha256':sha(path)}


def main():
    samples = {}
    ghidra = {}
    for language in ('ru','en'):
        inv = read(ROOT/f'analysis/inventory-{language}.json')
        samples[language] = {'exe':artifact(ROOT/inv['file']),
                             'res':artifact(ROOT/inv['resource']['file'])}
        assert samples[language]['exe']['sha256'] == inv['sha256']
        assert samples[language]['res']['sha256'] == inv['resource']['sha256']
        directory = ROOT/f'build/rendering/{language}'
        manifest = read(directory/'analysis-manifest.json')
        assert manifest['sha256_before'] == manifest['sha256_after'] == inv['sha256']
        assert manifest['script_sha256'] == sha(ROOT/manifest['script'])
        assert manifest['driver_sha256'] == sha(ROOT/'scripts/analyze-rendering.ps1')
        roles = list(csv.DictReader((directory/'roles.tsv').read_text(encoding='utf-8').splitlines(),delimiter='\t'))
        required = {'render_camera_initialize','render_edge_vertical_plane','render_billboard_project',
                    'render_textured_horizontal_quad','render_sort_descending_depth','render_column_visibility',
                    'render_draw_sorted_list','world_build_render_geometry','rasterize_vertical',
                    'rasterize_horizontal','render_horizontal_span_asm','render_vertical_span_asm',
                    'render_light_row_select','image_read_stream','image_flags_crop_configure'}
        assert required <= {role['role'] for role in roles}
        assert all(int(role['bytes']) > 0 for role in roles)
        ghidra[language] = {'roles':roles,'artifacts':[artifact(directory/name) for name in
                            ('analysis-manifest.json','roles.tsv','evidence.asm','annotated.c','ghidra.log')]}
    for script in ('probe_projection.py','probe_render_layout.py','probe_reference_scene.py'):
        subprocess.run([sys.executable,str(ROOT/'scripts'/script)],cwd=ROOT,check=True)
    reports = {name:read(ROOT/f'analysis/{name}.json') for name in
               ('projection-probe','render-layout-probe','render-reference')}
    assert reports['projection-probe']['status'] == 'passed'
    assert reports['render-layout-probe']['status'] == 'passed_resource_probe'
    assert reports['render-reference']['status'] == 'passed_saved_scene_probe'
    for scene in reports['render-reference']['scenes']:
        assert scene['runtime_trig_table']['integer_values_checked'] == 722
        assert scene['runtime_trig_table']['mismatches'] == 0
        assert scene['door_geometry']['projected_records_match']
        assert len(scene['door_geometry']['quads']) == 2
        assert scene['vga']['snapshot_equals_saved_start_frame']
        assert scene['viewport_aspect']['aspect_preserving_height'] == 144
    for language in ('ru','en'):
        for sample in samples[language].values():
            assert sha(ROOT/sample['path']) == sample['sha256']
    documents = [ROOT/'docs'/name for name in ('rendering.md','rendering-geometry.md',
                 'rendering-assets.md','rendering-reference.md','gba-ui.md')]
    result = {'task':'G04','status':'passed','originals_unchanged':True,'samples':samples,'ghidra':ghidra,
              'verifier':artifact(Path(__file__).resolve()),
              'host_probes':[artifact(ROOT/f'analysis/{name}.json') for name in reports],
              'documents':[artifact(path) for path in documents],
              'scalar_vectors':reports['projection-probe']['vectors'],
              'gba_viewport':[240,144],'message_strip':[0,144,240,16],
              'limits':['No full DOS renderer pixel equivalence claimed.',
                        'No GBA renderer execution/performance measured by G04.',
                        'Unverified geometry/lighting branches remain listed in rendering.md.']}
    (ROOT/'analysis/rendering-verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'task':'G04','status':'passed','scalar_vectors':result['scalar_vectors'],
                      'ghidra_roles':{lang:len(item['roles']) for lang,item in ghidra.items()}}))


if __name__ == '__main__':
    main()
