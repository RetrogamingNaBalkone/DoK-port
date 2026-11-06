"""Export SP11 item, weapon and projectile instructions through read-only Ghidra projects."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument("--profile",choices=("ru","en","old-gold"));options=parser.parse_args()
    local = Path(os.environ['LOCALAPPDATA']) / 'Programs/Ghidra'
    java = local / 'java/jdk-21.0.12.1+1/bin/java.exe'
    ghidra = local / 'ghidra_12.1.2_PUBLIC'
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())
    for profile in inventory['profiles']:
        if options.profile and profile['id']!=options.profile:continue
        sample = ROOT / profile['sample']
        before = digest(sample)
        if before != profile['sha256_before']:
            raise RuntimeError('Original hash mismatch: ' + str(sample))
        project = ROOT / profile['project']
        output = ROOT / 'research/evidence/source-port/sp11-native-complete/ghidra' / profile['id']
        output.mkdir(parents=True, exist_ok=True)
        ranges = {
            'ru': [('objects','1068:0000','0x4400'),('animation','1050:0000','0x142'),('world','1098:0000','0x1600'),('vtables','1190:0400','0x500'),('copy-stack','1000:5cd5','0x30'),('copy-memory','1000:5cb3','0x22')],
            'en': [('heap-size','1090:028b','0x70'),('objects','1020:0000','0x5000'),('animation','1018:0f0e','0x142'),('world','1030:0000','0x1600'),('vtables','10d0:0400','0x500'),('copy-stack','1000:5cd5','0x30'),('copy-memory','1000:5cb3','0x22')],
            'old-gold': [('objects','1ae1:0000','0x4500'),('animation','1aa1:0000','0x142'),('world','21b8:0000','0x1600'),('vtables','3255:0400','0x500'),('copy-stack','1000:1347','0x30'),('copy-memory','1000:1126','0x22'),('reclaim-flag','2000:4745','0x25')]
        }[profile['id']]
        script = ROOT / 'tools/ghidra/ExportSourcePortProjectiles.java'
        args = [str(java), '-Djava.system.class.loader=ghidra.GhidraClassLoader',
                f'-Duser.home={ROOT}/research/ghidra/home',
                f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings',
                '-Djava.awt.headless=true', '-Xmx3G', '-cp',
                str(ghidra / 'Ghidra/Framework/Utility/lib/Utility.jar'),
                'ghidra.Ghidra', 'ghidra.app.util.headless.AnalyzeHeadless',
                str(project.parent), project.stem, '-process', sample.name,
                '-readOnly', '-noanalysis', '-scriptPath', str(script.parent),
                '-postScript', script.name, str(output), before] + [v for row in ranges for v in row]
        env = dict(os.environ, LOCALAPPDATA=str(ROOT / 'research/ghidra/local'))
        with (output / 'ghidra.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, env=env)
        after = digest(sample)
        if before != after:
            raise RuntimeError('Original changed during export')
        log_text = (output / 'ghidra.log').read_text(encoding='utf-8')
        if result.returncode or 'SOURCE_PORT_SCHEDULER_EXPORTED' not in log_text:
            raise RuntimeError('Ghidra failed; inspect ' + str(output / 'ghidra.log'))
        manifest = dict(profile=profile['id'], sample=profile['sample'],
                        sha256_before=before, sha256_after=after,
                        project=profile['project'], ranges=ranges,
                        address_system='Ghidra MZ/NE, not runtime selector or file offset',
                        script_sha256=digest(script), driver_sha256=digest(Path(__file__)),
                        bytes_sha256={r[0]:digest(output/(r[0]+'.bin')) for r in ranges})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(profile['id'], 'world/animation exported; original hashes unchanged', flush=True)


if __name__ == '__main__':
    main()

