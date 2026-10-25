"""Publish memory/provenance only when G06A evidence refers to the current ROM."""
import argparse
import hashlib
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=Path,default=Path('build/g06a-fast'))
a=p.parse_args()
read=lambda path:json.loads(Path(path).read_text(encoding='utf-8'))
write=lambda path,value:Path(path).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
build=read(a.build_dir/'build-report.json');rom=sha(a.build_dir/'dok-diagnostic.gba')
assert build['rom_sha256']==rom
benchmark=read('analysis/render-early-benchmark.json')
regression=read('analysis/g06a-regression.json')
for name in ('render-early-benchmark','g06a-regression','g06a-time-regression','g06a-column-verification'):
    assert read(f'analysis/{name}.json')['rom_sha256']==rom,('stale evidence',name)
assert read('analysis/render-early-startup.json')['rom']==rom
bank=read(a.build_dir/'render-columns.json')
assert sha(a.build_dir/'render-columns.bin')==bank['bank_sha256']
s=build['symbols'];iwram=s['__iwram_end']-s['__iwram_start']
stack=max((benchmark['stack'],regression['stack_before_reset'],regression['stack_after_reset']),
          key=lambda row:row['high_water_bytes'])
memory=dict(task='G06A',rom_sha256=rom,rom_bytes=build['rom_bytes'],rom_capacity=32*1024*1024,
            rom_remaining=32*1024*1024-build['rom_bytes'],ewram_static_bytes=build['ewram_static_bytes'],
            ewram_remaining=256*1024-build['ewram_static_bytes'],
            renderer_workspace_bytes=regression['ru']['diag']['workspace_bytes'],
            iwram_code_bytes=iwram,iwram_code_limit=24576,iwram_code_remaining=24576-iwram,
            stack_bios_reserve=8192,stack_observed=stack,coverage_bytes=34800,
            derived_column_bank_bytes=bank['bytes'],reciprocal_table_bytes=262144,
            scope='Current bounded renderer + G06 time probe; free RAM is not a full-game reservation')
write('analysis/g06a-memory-budget.json',memory)
paths=list(read('analysis/g06a-provenance.json')['source_sha256'])
paths+=['scripts/profile_renderer.py','scripts/verify_render_columns.py',
        'scripts/report_renderer_early.py','scripts/build-gba-early.ps1']
write('analysis/g06a-provenance.json',dict(rom_sha256=rom,
      compiler='Shared Zig0.13.0 / bundled LLD18.1.6',package_sha256=bank['package_sha256'],
      source_sha256={path:sha(path) for path in dict.fromkeys(paths)},column_bank=bank))
print(f"G06A evidence consistent: {rom}; worst {benchmark['worst']['cycles']}, budget 400000")
