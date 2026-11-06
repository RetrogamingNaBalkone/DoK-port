"""SP13 read-only Ghidra export of original sound admission and hardware paths."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
RANGES = {
    'ru': [('audio-data', '1190:0b80', '0x300'), ('admission', '10f0:0000', '0x1fb'), ('wrappers', '1108:0000', '0x321'),
           ('hardware', '1118:0000', '0xbff')],
    'en': [('audio-data', '10d0:0c20', '0x300'), ('admission', '1058:186a', '0x10e'), ('wrappers', '1060:153a', '0x32d'),
           ('hardware', '1070:0000', '0xbe2')],
    'old-gold': [('audio-data', '3255:0000', '0x1000'), ('admission', '1823:0008', '0x829'), ('wrappers', '18a6:0001', '0x2ef'),
                 ('timer', '180b:0002', '0x186'), ('hardware', '2ad5:0008', '0x5ce'),
                 ('irq', '2b41:000f', '0xe1'), ('irq-tail', '2b50:0000', '0x2'),
                 ('startup', '20c4:000f', '0xf40'), ('screen-keys', '3255:08a0', '0xc0')],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--profile',action='append',choices=['ru','en','old-gold']);args_cli=parser.parse_args()
    local = Path(os.environ['LOCALAPPDATA']) / 'Programs/Ghidra'
    java = local / 'java/jdk-21.0.12.1+1/bin/java.exe'
    ghidra = local / 'ghidra_12.1.2_PUBLIC'
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())
    script = ROOT / 'tools/ghidra/ExportSourcePortSoundComplete.java'
    for profile in inventory['profiles']:
        if args_cli.profile and profile['id'] not in args_cli.profile:continue
        sample = ROOT / profile['sample']
        before = sha(sample)
        assert before == profile['sha256_before'], 'Original sample differs'
        project = ROOT / profile['project']
        output = ROOT / 'research/evidence/source-port/sp13-native-complete/ghidra' / profile['id']
        output.mkdir(parents=True, exist_ok=True)
        ranges = RANGES[profile['id']]
        args = [str(java), '-Djava.system.class.loader=ghidra.GhidraClassLoader',
                f'-Duser.home={ROOT}/research/ghidra/home',
                f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings',
                '-Djava.awt.headless=true', '-Xmx3G', '-cp',
                str(ghidra / 'Ghidra/Framework/Utility/lib/Utility.jar'),
                'ghidra.Ghidra', 'ghidra.app.util.headless.AnalyzeHeadless',
                str(project.parent), project.stem, '-process', sample.name,
                '-readOnly', '-noanalysis', '-scriptPath', str(script.parent),
                '-postScript', script.name, str(output), before] + [v for r in ranges for v in r]
        env = dict(os.environ, LOCALAPPDATA=str(ROOT / 'research/ghidra/local'))
        with (output / 'ghidra.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, env=env)
        after = sha(sample)
        assert before == after, 'Original modified'
        assert result.returncode == 0 and 'SOURCE_PORT_SCHEDULER_EXPORTED' in (output / 'ghidra.log').read_text(), str(output)
        manifest = dict(profile=profile['id'], sample=profile['sample'], sha256_before=before,
                        sha256_after=after, project=profile['project'], ranges=ranges,
                        address_system='Ghidra MZ/NE; not file offsets or runtime selectors',
                        script_sha256=sha(script), driver_sha256=sha(Path(__file__)),
                        bytes_sha256={r[0]: sha(output / (r[0] + '.bin')) for r in ranges})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(profile['id'], 'audio exported; original SHA unchanged', flush=True)


if __name__ == '__main__':
    main()
