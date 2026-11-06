"""Export SP12 monster instructions and transient OG callback disassembly through read-only Ghidra projects."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    local = Path(os.environ['LOCALAPPDATA']) / 'Programs/Ghidra'
    java = local / 'java/jdk-21.0.12.1+1/bin/java.exe'
    ghidra = local / 'ghidra_12.1.2_PUBLIC'
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())
    for profile in inventory['profiles']:
        sample = ROOT / profile['sample']
        before = digest(sample)
        if before != profile['sha256_before']:
            raise RuntimeError('Original hash mismatch: ' + str(sample))
        project = ROOT / profile['project']
        output = ROOT / 'research/evidence/source-port/sp12-native-complete/ghidra' / profile['id']
        output.mkdir(parents=True, exist_ok=True)
        ranges = {
          'ru':[('actor','1068:0000','0x4400'),('world','1098:0000','0x1600'),('math','10c8:0000','0xb2c'),('runtime','1000:0000','0x667f')],
          'en':[('actor','1020:0000','0x5000'),('world','1030:0000','0x1c00'),('math','1058:0000','0x1978'),('runtime','1000:0000','0x667f')],
          'old-gold':[('actor','1ae1:0000','0x4300'),('world','21b8:0000','0x1600'),('math','26f6:0000','0x741'),('runtime','1000:0000','0x7828')]
        }[profile['id']]
        script = ROOT / 'tools/ghidra/ExportSourcePortMonsters.java'
        args = [str(java), '-Djava.system.class.loader=ghidra.GhidraClassLoader',
                f'-Duser.home={ROOT}/research/ghidra/home',
                f'-Dapplication.settingsdir={ROOT}/research/ghidra/settings',
                '-Djava.awt.headless=true', '-Xmx3G', '-cp',
                str(ghidra / 'Ghidra/Framework/Utility/lib/Utility.jar'),
                'ghidra.Ghidra', 'ghidra.app.util.headless.AnalyzeHeadless',
                str(project.parent), project.stem, '-process', sample.name,
                '-readOnly', '-noanalysis', '-scriptPath', str(script.parent),
                '-postScript', script.name, str(output), before, profile['id']] + [v for row in ranges for v in row]
        env = dict(os.environ, LOCALAPPDATA=str(ROOT / 'research/ghidra/local'))
        with (output / 'ghidra.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, env=env)
        after = digest(sample)
        if before != after:
            raise RuntimeError('Original changed during export')
        log_text = (output / 'ghidra.log').read_text(encoding='utf-8')
        if result.returncode or 'SOURCE_PORT_MONSTERS_EXPORTED' not in log_text:
            raise RuntimeError('Ghidra failed; inspect ' + str(output / 'ghidra.log'))
        manifest = dict(profile=profile['id'], sample=profile['sample'],
                        sha256_before=before, sha256_after=after,
                        project=profile['project'], ranges=ranges,
                        address_system='Ghidra MZ/NE, not runtime selector or file offset',
                        script_sha256=digest(script), driver_sha256=digest(Path(__file__)),
                        bytes_sha256={r[0]:digest(output/(r[0]+'.bin')) for r in ranges})
        (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(profile['id'], 'monsters exported; original hashes unchanged', flush=True)


if __name__ == '__main__':
    main()

