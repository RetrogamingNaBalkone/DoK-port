"""Export renderer, world bounds and mode selection through read-only Ghidra projects."""
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
        output = ROOT / 'research/evidence/source-port/sp07-render/ghidra' / profile['id']
        output.mkdir(parents=True, exist_ok=True)
        ranges = {'ru': [('video-select','1138:0180','0x134'),('span','1010:0000','0x1ca'),('world-bounds','1098:30c1','0x500'),('draw-tail','1100:0593','0x19'),('vtables','1190:0500','0x200'),('main','1008:0719','0xd36'),('raster','1120:0b12','0xae2')], 'en': [('video-select','1080:0180','0x134'),('world-bounds','1030:3571','0x500'),('draw-tail','1060:1521','0x19'),('vtables','10d0:0500','0x300'),('main','1008:08bd','0x1721'),('raster','1078:0c42','0xe62')], 'old-gold': [('draw-tail','203d:04dd','0xa'),('vtables','3255:0400','0x400'),('entry-raster-wrapper','2b57:0a0d','0x129'),('entry-camera','203d:00cb','0x1f6'),('entry-center-draw','24cf:06a5','0x133'),('entry-billboard-draw','24cf:1271','0xf2'),('main','20c4:03b0','0xb9f'),('world','21b8:1000','0x217b'),('geometry','24cf:000b','0x2265'),('raster','2b57:0001','0x138c')]}[profile['id']]
        script = ROOT / 'tools/ghidra/ExportSourcePortRender.java'
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
        print(profile['id'], 'renderer exported; original hashes unchanged', flush=True)


if __name__ == '__main__':
    main()

