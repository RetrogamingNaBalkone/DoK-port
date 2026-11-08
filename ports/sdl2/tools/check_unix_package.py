"""Verify exact package payload, hashes and archive executable permissions."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
import zipfile
import xml.etree.ElementTree as ET


def validate_portmaster_layout(files):
    # Independent of our manifest: a self-consistent manifest can describe a
    # wrongly laid out ZIP. Match PortMaster's build_port_zip output contract.
    required = {'OGDoK.sh', 'ogdok/OGDoK', 'ogdok/port.json',
                'ogdok/gameinfo.xml', 'ogdok/ogdok.md'}
    if not required <= files.keys():
        raise ValueError('PortMaster metadata must be inside ogdok/')
    if {name for name in files if '/' not in name} != {'OGDoK.sh'}:
        raise ValueError('Only the launcher belongs at the PortMaster ZIP root')
    if any(not name.startswith('ogdok/') for name in files if name != 'OGDoK.sh'):
        raise ValueError('Unexpected PortMaster payload directory')
    metadata = json.loads(files['ogdok/port.json'])
    if metadata['items'] != ['OGDoK.sh', 'ogdok/'] or metadata['name'] != 'ogdok.zip':
        raise ValueError('PortMaster items do not match install layout')
    if ET.fromstring(files['ogdok/gameinfo.xml']).findtext('game/path') != './OGDoK.sh':
        raise ValueError('Gameinfo launch path must be relative to ports directory')


def verify(package):
    files = {}
    modes = {}
    if zipfile.is_zipfile(package):
        with zipfile.ZipFile(package) as archive:
            if archive.testzip():
                raise ValueError('Corrupt ZIP')
            for entry in archive.infolist():
                if not entry.is_dir():
                    files[entry.filename] = archive.read(entry)
                    modes[entry.filename] = entry.external_attr >> 16
    else:
        with tarfile.open(package) as archive:
            for entry in archive.getmembers():
                if entry.isfile():
                    name = str(PurePosixPath(entry.name).relative_to(PurePosixPath(entry.name).parts[0]))
                    files[name] = archive.extractfile(entry).read()
                    modes[name] = entry.mode
                elif not entry.isdir():
                    raise ValueError('Unexpected special archive member')
    for name in files:
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or path.suffix.lower() == '.res' or 'user' in path.parts:
            raise ValueError('Unexpected payload path: ' + name)
    manifests = [name for name in files if name.endswith('manifest.json')]
    if len(manifests) != 1:
        raise ValueError('Expected exactly one manifest')
    manifest_name = manifests[0]
    manifest = json.loads(files[manifest_name])
    if manifest['platform'] == 'portmaster-aarch64':
        validate_portmaster_layout(files)
    if set(files) != set(manifest['files']) | {manifest_name}:
        raise ValueError('Manifest file set mismatch')
    for name, digest in manifest['files'].items():
        if hashlib.sha256(files[name]).hexdigest() != digest:
            raise ValueError('File hash mismatch: ' + name)
    for name in files:
        if PurePosixPath(name).name in ['OGDoK', 'OGDoK.sh'] and not modes[name] & 0o111:
            raise ValueError('Executable bit missing: ' + name)
    checksum = package.with_name(package.name + '-SHA256.txt').read_text().split()[0]
    if hashlib.sha256(package.read_bytes()).hexdigest() != checksum:
        raise ValueError('Archive checksum mismatch')
    return {'platform': manifest['platform'], 'files': len(files), 'sha256': checksum}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path)
    print(json.dumps(verify(parser.parse_args().archive)))
