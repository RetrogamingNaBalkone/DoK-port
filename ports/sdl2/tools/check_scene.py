"""Initial scene smoke across six Kremlin levels and several camera headings."""
import hashlib
import json
import subprocess
from check_render import ROOT,OUT,png,read_archive


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    report={'scope':'Initial static Kremlin scene; no gameplay or animation equivalence','scenes':[]}
    for language,folder,filename,snapshot in [('ru','RUS','PKR.RES','ru-03'),('en','ENG','PKE.RES','en-02')]:
        source=ROOT/'originals/pkr'/folder/filename
        before=hashlib.sha256(source.read_bytes()).hexdigest()
        entries=dict(read_archive(source.read_bytes()))
        for level in range(1,4):
            for heading in (None,0,45,135,180,225,270,315):
                label=f'{language}-level{level}' if heading is None else f'{language}-level{level}-{heading}'
                output=OUT/'scene-current.idx'
                args=[str(ROOT/'build/sdl2/windows/dok_render_probe.exe'),str(source),str(output),f'LEVEL{level}']
                if heading is not None:args.append(str(heading))
                result=subprocess.run(args,capture_output=True,text=True,check=True)
                pixels=output.read_bytes()
                if len(pixels)!=51200 or not any(pixels):raise RuntimeError('Empty/wrong scene: '+label)
                item={'scene':label,'sha256':hashlib.sha256(pixels).hexdigest(),'geometry':result.stdout.strip()}
                if heading is None:
                    (OUT/f'scene-{label}.idx').write_bytes(pixels)
                    png(OUT/f'scene-{label}.png',pixels,320,160,entries[f'GAMEP{level}'.encode()])
                    if level==1:
                        reference=(ROOT/'research/evidence/reference'/snapshot/'level1-start.idx').read_bytes()[:51200]
                        item['matching_DOS_indices']=sum(a==b for a,b in zip(pixels,reference))
                        item['total_indices']=51200
                        # Metric only: animation phase/getter side effects are not restored.
                report['scenes'].append(item)
        if hashlib.sha256(source.read_bytes()).hexdigest()!=before:raise RuntimeError('Original changed')
        print(language,'24 initial/rotated scene renders passed',flush=True)
    (OUT/'scene-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps([r for r in report['scenes'] if 'matching_DOS_indices' in r],indent=2))


if __name__=='__main__':main()
