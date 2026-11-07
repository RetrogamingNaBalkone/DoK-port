"""Compile actual DS clock adapter with a scripted SDK source, plus world clock."""
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_items import ROOT, run
from map.compiler_cache import compiler_environment

out = ROOT / 'build/nds/n23/clock37-host'
out.mkdir(parents=True, exist_ok=True)
command = [Path.home()/'AppData/Local/Programs/Zig/0.13.0/zig.exe', 'cc',
           '-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
           '-Iports/nds/tests/stubs/clock', '-Iports/nds/include',
           'ports/nds/platform/time/clock.c', 'ports/nds/core/time.c',
           'ports/nds/tests/test_platform_clock_retry.c', '-o', out/'test.exe']
run(command, env=compiler_environment(ROOT))
report = json.loads(run([out/'test.exe']))
report['command'] = list(map(str, command))
(out/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf8')
print(json.dumps(report))
