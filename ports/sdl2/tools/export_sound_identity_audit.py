"""SP13 read-only Ghidra export of sound wrapper identity allocation and raw cache."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
RANGES = {
 'ru':[('getter','1060:0145','0x5f'),('cache','10e0:0000','0x59a'),('actor-binding','1068:3eca','0x300')],
 'en':[('getter','1018:18d9','0xce'),('cache','1058:0c1a','0x650'),('actor-binding','1020:4633','0x300')],
 'old-gold':[('getter','19b1:0157','0x6d'),('cache','276a:000a','0x574'),('actor-binding','1ae1:3989','0x300')],
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    local = Path(os.environ['LOCALAPPDATA']) / 'Programs/Ghidra'
    java = local / 'java/jdk-21.0.12.1+1/bin/java.exe'
    ghidra = local / 'ghidra_12.1.2_PUBLIC'
    inventory = json.loads((ROOT / 'analysis/source-port/sp01-inventory.json').read_text())
    script = ROOT / 'tools/ghidra/ExportSourcePortScheduler.java'
    for profile in inventory['profiles']:
        sample = ROOT / profile['sample']
        before = sha(sample)
        assert before == profile['sha256_before'], 'Original sample differs'
        project = ROOT / profile['project']
        output = ROOT / 'research/evidence/source-port/sp13-native-complete/sound-identity/ghidra' / profile['id']
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

