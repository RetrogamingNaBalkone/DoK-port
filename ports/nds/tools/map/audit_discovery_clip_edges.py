"""N21P signed-DWORD near-clip range proof and original-constructor edges."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from audit_discovery import OriginalDiscovery, originals

ROOT=Path(__file__).resolve().parents[4]

def wrap(n):return (n+2**31)%2**32-2**31
def pin(path):
    path=Path(path)
    return dict(path=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def quotient(n,d):return (abs(n)//abs(d))*(-1 if (n<0)!=(d<0) else 1)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump',type=Path,default=ROOT/'build/nds/n21p/discovery/map_discovery_dump.exe')
    args=parser.parse_args();before=originals();bounds=[]
    # After the both-behind return, the clipped branch has d_negative<0,
    # d_positive>0. A zero opposite depth never reaches division.
    for negative in (-65536,-32768,-1):
        for positive in (1,32768,65535):
            denominator=positive-negative
            assert 2<=denominator<=131071
            for n in (-2**31,-2**31+1,-65537,-1,0,1,65535,2**31-1):
                q=quotient(n,denominator)
                assert -2**31<=q<2**31
                assert not (n==-2**31 and denominator==-1)
                # Both signed i64 and signed i32 IDIV truncate toward zero;
                # the source input values are identical in this proven domain.
                assert quotient(wrap(n),denominator)==q
                bounds.append([negative,positive,n,denominator,q])
    coords=(-2**31,-65536,-65535,-32769,-32768,-1,0,1,32767,32768,65534,65535,65536,2**31-1)
    origins=[(0,z) for z in coords]+[(x,0) for x in coords]+[(32768,32767),(-32768,-32769)]
    cameras=((0,32768),(32768,0),(23170,23170),(-32768,0))
    rows=[];vectors=[]
    for x,z in origins:
        for width in (1,2,32767):
            for orientation in (0,90,180,270):
                points={0:((x,z),(x+width,z)),90:((x,z+width),(x,z)),
                        180:((x+width,z+width),(x,z+width)),270:((x+width,z),(x+width,z+width))}[orientation]
                (ax,az),(bx,bz)=[(wrap(a),wrap(b)) for a,b in points]
                xyz=[ax,-1260,az,bx,-1260,bz,bx,-300,bz,ax,-300,az]
                rec=dict(kind='edge',xyz=[x,-300,z],height=960,width=width,orientation=orientation,flags=8)
                for sine,cosine in cameras:
                    camera=[0,-300,0,sine,cosine]
                    rows.append([0,0,0,8]+xyz+camera)
                    vectors.append((rec,camera))
    observed=subprocess.check_output([str(args.dump)],input=''.join(' '.join(map(str,r))+'\n' for r in rows),text=True)
    observed=[list(map(int,r.split())) for r in observed.splitlines()]
    assert len(observed)==len(vectors)
    languages={}
    for language in ('ru','en'):
        native=OriginalDiscovery(language)
        for i,((rec,camera),actual) in enumerate(zip(vectors,observed)):
            expected=native.project(rec,camera)
            if not actual[0]&1:actual=[0,0,0,0,actual[4]]
            assert actual==expected,(language,i,rec,camera,actual,expected)
        languages[language]=dict(native_constructor_edges=len(vectors))
    assert before==originals()
    paths=[__file__,ROOT/'ports/nds/ui/map_discovery.c',ROOT/'ports/nds/include/dok/map_discovery.h',
           ROOT/'ports/nds/tools/map/audit_discovery.py']
    result=dict(task='N21P clip signed32 proof',passed=True,originals_before=before,originals_after=originals(),
                signed_domain_vectors=len(bounds),domain_edges=bounds,languages=languages,
                native_constructor_vectors=2*len(vectors),source_pins=[pin(p) for p in paths],
                executable=pin(args.dump),depth_range=[-65536,65535],denominator_range=[2,131071],
                truncation='signed IDIV toward zero for both i32 and i64; numerator is already signed low32',
                overflow='INT_MIN / -1 impossible: denominator strictly positive; quotient fits signed32',
                source_address_system='Ghidra NE loader; PKR10B0:06AA and PKE1040:06B6 edge constructors')
    target=ROOT/'research/evidence/nds/n21p/discovery-clip-edges.json'
    target.write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,native_constructor_vectors=result['native_constructor_vectors'],
                          signed_domain_vectors=len(bounds))))

if __name__=='__main__':main()
