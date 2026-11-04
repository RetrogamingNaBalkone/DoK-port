"""Read-only Ghidra evidence for the original Kremlin README composition."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
RANGES = {
    'ru': [('screens', '1008:0000', '0x400'), ('menu', '10c0:0000', '0x19cc'),
           ('data', '1190:0000', '0x1000')],
    'en': [('screens', '1008:0000', '0x44b'), ('menu', '1050:0000', '0x1d65'),
           ('data', '10d0:0000', '0x1000')],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    local = Path(os.environ['LOCALAPPDATA']) / 'Programs/Ghidra'
    script = ROOT / 'tools/ghidra/ExportSourcePortScheduler.java'
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())
    for profile in inventory['profiles']:
        if profile['id'] not in RANGES:
            continue
        sample = ROOT / profile['sample']
        before = sha(sample)
        assert before == profile['sha256_before']
        project = ROOT / profile['project']
        out = ROOT / 'research/evidence/source-port/sp15-ui/r4-originals' / profile['id']
        out.mkdir(parents=True, exist_ok=True)
        ranges = RANGES[profile['id']]
        args = [local / 'java/jdk-21.0.12.1+1/bin/java.exe',
                '-Djava.system.class.loader=ghidra.GhidraClassLoader',
                f'-Duser.home={ROOT}/research/ghidra/home',
                f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings',
                '-Djava.awt.headless=true', '-Xmx2G', '-cp',
                local / 'ghidra_12.1.2_PUBLIC/Ghidra/Framework/Utility/lib/Utility.jar',
                'ghidra.Ghidra', 'ghidra.app.util.headless.AnalyzeHeadless',
                project.parent, project.stem, '-process', sample.name, '-readOnly',
                '-noanalysis', '-scriptPath', script.parent, '-postScript', script.name,
                out, before, *[value for row in ranges for value in row]]
        with (out / 'ghidra.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(list(map(str, args)), stdout=log, stderr=subprocess.STDOUT,
                                    env=dict(os.environ, LOCALAPPDATA=str(ROOT / 'research/ghidra/local')))
        assert result.returncode == 0 and 'SOURCE_PORT_SCHEDULER_EXPORTED' in (out / 'ghidra.log').read_text()
        assert sha(sample) == before
        (out / 'manifest.json').write_text(json.dumps(dict(
            sample=profile['sample'], project=profile['project'], sha256_before=before,
            sha256_after=sha(sample), address_system='Ghidra NE segment:offset; not file offsets or DPMI selectors',
            ranges=ranges, script_sha256=sha(script), driver_sha256=sha(Path(__file__)),
            outputs={p.name: sha(p) for p in out.iterdir() if p.suffix in ('.bin', '.asm')}
        ), indent=2) + '\n')
    print('PASS: original README code, menu background and data exported; EXE hashes unchanged')


if __name__ == '__main__':
    main()
