"""Read-only audit of current playtest03: existing ARM timings + host I/O counts."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]


def main():
    out = ROOT / 'build/nds/n23/loading-audit'
    out.mkdir(parents=True, exist_ok=True)
    zig = Path.home() / 'AppData/Local/Programs/Zig/0.13.0/zig.exe'
    env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(ROOT/'build/zig-global-cache'),
               ZIG_LOCAL_CACHE_DIR=str(ROOT/'build/zig-local-cache'))
    core = [f'ports/nds/core/{n}.c' for n in ('contracts', 'assets', 'sha256', 'world', 'balance')]
    render = [f'ports/nds/render/{n}.c' for n in ('scene', 'geometry', 'materials', 'door_render',
              'sprites', 'sprite_assets', 'texture_cache', 'frame_store')]
    exe = out / 'loading_io.exe'
    subprocess.run([str(zig), 'cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
                    '-DN23_COMPACT_SOURCE=1', '-I', 'ports/nds/include', '-I', 'build/nds/n07/generated',
                    *core, *render, 'ports/nds/platform/assets/file_reader.c',
                    'ports/nds/tools/performance/loading_io.c', '-o', str(exe)],
                   cwd=ROOT, env=env, check=True)
    target = ROOT / 'build/nds/n23/playtest03-ru'
    package = target / 'nitrofs/dok/assets.ndp'
    manifest = json.loads((ROOT/'artifacts/nds/manifest.json').read_text('utf8'))
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    assert digest(package) == manifest['payload'][0]['sha256']
    before = digest(ROOT/'artifacts/nds/dok.nds')
    assert before == manifest['published']['sha256']
    sample_sha = digest(target/'dok_nds_gameplay.nds')
    run = subprocess.run([str(exe), str(package)], cwd=ROOT, check=True, capture_output=True, text=True)
    counts = [json.loads(line) for line in run.stdout.splitlines()]
    evidence = target / 'joint-frontend/live/result.json'
    d = json.loads(evidence.read_text('utf8'))
    assert d['passed'] and d['rom_sha256'] == sample_sha
    report = dict(rom_sha256=sample_sha, package_sha256=digest(package),
                  existing_emulator_report=str(evidence.relative_to(ROOT)),
                  existing_emulator_report_sha256=digest(evidence), boot=d['boot'],
                  transitions=[{k:e.get(k) for k in ('category', 'latency_upper_vblanks', 'latency_upper_ms', 'map_key')}
                               for e in d['input_latency'] if e.get('category')=='level_loading'],
                  arm_loading=[dict(level=r['level'], vblanks=r['vblank_end']-r['vblank_start'],
                                    ticks=r['total_ticks'], milliseconds=r['total_ticks']*1000/33513982)
                               for r in d['history_records'] if r['flags']==1],
                  host_logical_reads=counts,
                  scope='Host counts cover world, current RU localization/rebind, sprite metadata and frame store; exclude audio, effects, static geometry, UI and frontend append. No host timing is used as DS timing. Reader calls are not physical media transactions.')
    assert digest(ROOT/'artifacts/nds/dok.nds') == before
    destination = ROOT/'analysis/nds-loading-audit-20261031.json'
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', 'utf8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
