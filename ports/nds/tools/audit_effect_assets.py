"""Actual LEVEL effect images vs original crop instructions and fresh DOS DAC."""
from __future__ import annotations
import argparse,json,struct,subprocess,hashlib
from pathlib import Path
from audit_effects import Original,originals,resources,ROOT,OUT
from audit_geometry import NativeCellOracle
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dump',type=Path,required=True);args=p.parse_args()
    before=originals();report=dict(passed=False,originals_before=before,languages={},dos=[])
    for language in ('ru','en'):
        native=Original(language);source=resources(language)
        catalog=json.loads((ROOT/f'analysis/originals/resources-{language}.json').read_text('utf8'))
        by_name={bytes.fromhex(r['name_hex']):r['index'] for r in catalog['entries']}
        requests=[];expected=[];cases=[]
        for level in (1,2,3):
            model=NativeCellOracle(level)
            for obj in model.base['objects']:
                if obj['class_id']!=12:continue
                name=model.source[obj['sprite'][0]:sum(obj['sprite'])];index=by_name[name];raw=source[index]
                w,h=struct.unpack_from('<2H',raw);pixels=raw[18:];assert len(pixels)==w*h
                sw=1<<max(3,(w-1).bit_length());sh=1<<max(3,(h-1).bit_length())
                padded=b''.join(pixels[y*w:(y+1)*w]+bytes(sw-w) for y in range(h))+bytes(sw*(sh-h))
                for phase in range(18):
                    result=native.crop(w,h,obj['sprite_flags'],18,phase,pixels)
                    requests.append(f'{w} {h} {sw} {sh} {obj["sprite_flags"]} 18 {phase} 0\n'+padded.hex()+'\n'+bytes(range(256)).hex()+'\n')
                    expected.append(b''.join(result[y*w:(y+1)*w]+bytes(sw-w) for y in range(h))+bytes(sw*(sh-h)))
                    cases.append(dict(level=level,glyph=obj['glyph'],resource_index=index,phase=phase,
                        sha256=hashlib.sha256(result).hexdigest(),pixels=w*h))
        actual=[bytes.fromhex(v) for v in subprocess.check_output([args.dump,'--pixels'],input=''.join(requests),text=True).splitlines()]
        assert actual==expected
        report['languages'][language]=dict(crop_cases=len(cases),source_pixels=sum(c['pixels'] for c in cases),cases=cases)
    source=resources('ru')
    for level in (1,2,3):
        directory=OUT/f'dos/ru/level{level}-phase-pair';r=json.loads((directory/'report.json').read_text('utf8'))
        assert r['status']=='passed' and r['originals_unchanged'] and r['copied_runtime_unchanged']
        dac=(directory/'play-clean.vga6').read_bytes();original=source[20+level*2]
        assert len(dac)==len(original)==768 and dac==bytes(v*99//100 for v in original)
        pixels=(directory/'play-clean.idx').read_bytes();assert len(pixels)==64000
        colors=[tuple((dac[i*3+k]>>1) for k in range(3)) for i in range(256)]
        report['dos'].append(dict(level=level,palette_percentage=99,indexed_pixels=len(pixels),
            distinct_indices=len(set(pixels)),distinct_rgb5=len(set(colors)),capture_sha256=hashlib.sha256(pixels).hexdigest(),
            report_sha256=hashlib.sha256((directory/'report.json').read_bytes()).hexdigest()))
    report['originals_after']=originals();assert before==report['originals_after'];report['passed']=True
    (OUT/'actual-assets-report.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print('N13 actual effect images:540 original/C phase pairs; three fresh DOS VGA6 captures PASS')
if __name__=='__main__':main()
