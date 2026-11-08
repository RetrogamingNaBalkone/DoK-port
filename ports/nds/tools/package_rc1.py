"""Package the approved Nintendo DS RC1 with its public name and user guide."""
import hashlib
import json
from pathlib import Path
import zipfile

from package_rom import nitro_files

ROOT=Path(__file__).resolve().parents[3]
TAG='nds-1.0-rc1'
ROM_NAME='Dungeons of Kremlin.nds'
ZIP_NAME='Dungeons.of.Kremlin.DS.1.0-RC1.zip'
APPROVED_SHA='f2634c7697db63ed2df53a2956a8632c4f6707061c6e14f227645bbea8f90703'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def pin(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=sha(data))


def main():
    source=ROOT/'artifacts/nds/dok.nds'
    raw=source.read_bytes()
    assert sha(raw)==APPROVED_SHA,'Approved ROM changed'
    manifest=ROOT/'build/nds/n23/resident53c-ru/build-manifest.json'
    build=json.loads(manifest.read_text('utf8'))
    assert build['embedded_resources'] and not build['audit'] and not build['frontend_fixtures']
    assert next(r for r in build['artifacts']if r['path'].endswith('.nds'))['sha256']==APPROVED_SHA
    files=nitro_files(raw)
    assert set(files)=={'dok/assets.ndp','dok/ui.dat','dok/frontend.dat','dok/frames.dat','dok/sounds.dat'}
    for name,data in files.items():
        entry=next(r for r in build['generated_inputs']if r['path'].endswith('/nitrofs/'+name))
        assert sha(data)==entry['sha256']
    directory=ROOT/'artifacts/nds/releases'/TAG
    directory.mkdir(parents=True,exist_ok=True)
    readme=(ROOT/'docs/nds/releases/1.0-RC1.md').read_bytes()
    sums=f'{APPROVED_SHA}  {ROM_NAME}\n{sha(readme)}  README.md\n'.encode('ascii')
    entries={ROM_NAME:raw,'README.md':readme,'SHA256SUMS.txt':sums}
    for name,data in entries.items():(directory/name).write_bytes(data)
    archive=directory/ZIP_NAME
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9)as z:
        for name,data in entries.items():
            info=zipfile.ZipInfo(name,date_time=(2026,11,8,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=0o100644<<16
            z.writestr(info,data,compresslevel=9)
    with zipfile.ZipFile(archive)as z:
        assert z.testzip()is None
        assert set(z.namelist())==set(entries)
        for name,data in entries.items():assert z.read(name)==data
    (directory/(ZIP_NAME+'.sha256')).write_text(f'{sha(archive.read_bytes())}  {ZIP_NAME}\n','ascii')
    result=dict(tag=TAG,title='Dungeons of Kremlin — Nintendo DS 1.0-RC1',
        prerelease=True,publication='prepared',
        release_url='https://github.com/RetrogamingNaBalkone/DoK-port/releases/tag/'+TAG,
        rom=pin(directory/ROM_NAME),archive=pin(archive),entries=list(entries),
        source_build_manifest=pin(manifest),notes=pin(ROOT/'docs/nds/releases/1.0-RC1.md'))
    path=ROOT/'analysis/nds-release-1.0-rc1-20261108.json'
    path.write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n','utf8')
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':main()
