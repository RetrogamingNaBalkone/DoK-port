"""Create resource-free Unix playtest packages from a verified ELF build."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[3]


def elf_machine(path):
    data = path.read_bytes()[:20]
    if len(data) != 20 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'Expected little-endian ELF64: {path}')
    return struct.unpack_from('<H', data, 18)[0]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('platform', choices=['linux-x86_64', 'portmaster-aarch64'])
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--name', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.name):
        parser.error('--name must be a new directory name')
    exe = args.build.resolve() / 'OGDoK'
    expected = 62 if args.platform == 'linux-x86_64' else 183
    if elf_machine(exe) != expected:
        parser.error('ELF architecture does not match package platform')
    cache = (args.build / 'CMakeCache.txt').read_text(encoding='utf-8')
    api = re.search(r'^DOK_GRAPHICS_API:STRING=(\w+)$', cache, re.M).group(1)
    if api != ('OPENGL' if expected == 62 else 'GLES2'):
        parser.error('Wrong graphics API for this package')
    symbols = subprocess.check_output(['readelf', '--version-info', str(exe)], text=True)
    versions = [tuple(map(int, v.split('.'))) for v in re.findall(r'GLIBC_(\d+\.\d+(?:\.\d+)?)', symbols)]
    maximum = max(versions, default=(0, 0))
    if maximum > (2, 35):
        parser.error(f'ELF requires glibc {maximum}, exceeds 2.35 baseline')
    out = ROOT / 'artifacts/sdl2' / args.name
    archive = out.with_suffix('.tar.gz' if expected == 62 else '.zip')
    if out.exists() or archive.exists():
        parser.error('Output exists; use a new name (user profiles are never overwritten)')
    git = ['git', '-c', f'safe.directory={ROOT}']
    revision = subprocess.check_output(git + ['rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(git + ['status', '--porcelain', '--', 'ports/sdl2'], cwd=ROOT, text=True).strip())
    dependencies = re.findall(r'Shared library: \[(.*?)\]', subprocess.check_output(['readelf', '-d', str(exe)], text=True))
    source_hashes = {str(p.relative_to(ROOT)): digest(p) for p in sorted((ROOT / 'ports/sdl2').rglob('*'))
                     if p.is_file() and '__pycache__' not in p.parts}
    out.mkdir(parents=True)
    game = out if expected == 62 else out / 'ogdok'
    game.mkdir(exist_ok=True)
    shutil.copy2(exe, game / 'OGDoK')
    (game / 'OGDoK').chmod(0o755)
    licenses = game / 'licenses'
    licenses.mkdir()
    for source, name in [(ROOT / 'LICENSE', 'OGDoK.txt'),
                         (ROOT / 'ports/sdl2/NOTICES.txt', 'NOTICES.txt'),
                         (ROOT / 'ports/sdl2/third_party/xbr-LICENSE.txt', 'xBR.txt')]:
        shutil.copyfile(source, licenses / name)
    if expected == 62:
        shutil.copyfile(ROOT / 'ports/sdl2/README-linux.txt', game / 'README.txt')
    else:
        shutil.copyfile(ROOT / 'ports/sdl2/portmaster/OGDoK.sh', out / 'OGDoK.sh')
        for name in ['port.json', 'gameinfo.xml']:
            shutil.copyfile(ROOT / 'ports/sdl2/portmaster' / name, game / name)
        shutil.copyfile(ROOT / 'ports/sdl2/portmaster/README.md', game / 'ogdok.md')
        (out / 'OGDoK.sh').chmod(0o755)
        shutil.copyfile(ROOT / 'ports/sdl2/portmaster/default-settings.ini', game / 'default-settings.ini')
        shutil.copyfile(ROOT / 'ports/sdl2/tools/collect-device.sh', game / 'collect-device.sh')
    if expected == 183:
        metadata_path = game / 'port.json'
        metadata = json.loads(metadata_path.read_text())
        metadata['attr']['min_glibc'] = '.'.join(map(str, maximum))
        metadata_path.write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    manifest = dict(platform=args.platform, graphics_api=api, revision=revision, source_dirty=dirty,
                    source_sha256=source_hashes, needed=dependencies,
                    glibc_required='.'.join(map(str, maximum)), original_resources=False,
                    acceptance='playtest; hardware acceptance pending',
                    files={str(p.relative_to(out)): digest(p) for p in sorted(out.rglob('*')) if p.is_file()})
    (game / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    if expected == 62:
        with tarfile.open(archive, 'w:gz') as bundle:
            bundle.add(out, arcname=out.name)
    else:
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(out.rglob('*')):
                if path.is_file():
                    info = zipfile.ZipInfo(str(path.relative_to(out)))
                    info.create_system = 3
                    info.external_attr = (0o100755 if path.name in ['OGDoK', 'OGDoK.sh'] else 0o100644) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    bundle.writestr(info, path.read_bytes())
    archive.with_name(archive.name + '-SHA256.txt').write_text(f'{digest(archive)}  {archive.name}\n', encoding='ascii')
    print(archive)


if __name__ == '__main__':
    main()
