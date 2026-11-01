"""Differential host regression against the pinned pre-PERF01/02 combat code."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[3]
REFERENCE='4741642e77d93fab4a5f571b2264960d85be597e'

def main():
    os.chdir(ROOT)
    out=Path('build/gba/battle-equivalence');out.mkdir(parents=True,exist_ok=True)
    os.environ['ZIG_GLOBAL_CACHE_DIR']=str(ROOT/'build/gba/zig-cache')
    zig=Path(os.environ['LOCALAPPDATA'])/'Programs/Zig/0.13.0/zig.exe'
    original=subprocess.check_output(['git','show',REFERENCE+':ports/gba/src/combat.c'])
    reference=out/'reference_combat.c';reference.write_bytes(original)
    names=re.findall(r'\b(dok_combat_\w+)\s*\(',Path('ports/gba/include/dok/combat.h').read_text())
    flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-O2','-I','ports/gba/include','-DDOK_RUNTIME_ROM_ONLY']
    commands=[]
    def run(args):
        args=list(map(str,args));commands.append(args)
        return subprocess.check_output(args,stderr=subprocess.STDOUT,text=True)
    run([zig,*flags,*['-D'+n+'=reference_combat_'+n.removeprefix('dok_combat_') for n in names],'-c',reference,'-o',out/'reference.o'])
    modules='combat items runtime rom_assets image animation movement collision'.split()
    executable=out/'battle-tests.exe'
    run([zig,*flags,*['ports/gba/src/'+n+'.c' for n in modules],out/'reference.o','ports/gba/tests/test_battle_equivalence.c','-o',executable])
    # G21 intentionally changes player-shot geometry. Keep the original
    # differential for all unaffected owners; centered player contacts have a
    # separate independent rational geometry oracle on the production ARM ROM.
    stdout=run([executable,'build/gba/assets/assets.bin','enemy-only'])
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    report=dict(status='passed',reference_commit=REFERENCE,reference_source_sha256=sha(reference),
                source_sha256=sha('ports/gba/src/combat.c'),stdout=stdout.strip(),commands=commands,
                package_sha256=sha('build/gba/assets/assets.bin'),
                scope='Windows host differential, 6 LEVEL/locales, 480/511-unit cells, overlapping/edge/outside/dead/reclaimable actors, '
                      'non-player projectile owners and simultaneous volleys. Player shots excluded after the intentional G21 centered-footprint fix. '
                      'Not a DOS or hardware equivalence claim.')
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(stdout.strip())

if __name__=='__main__':main()
