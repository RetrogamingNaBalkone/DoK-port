"""Compare VESA trap frames with native state at measured dimensions."""
import subprocess,sys,json,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];parser=argparse.ArgumentParser();parser.add_argument('--language',choices=['ru','en']);args=parser.parse_args();report=[]
if args.language and (ROOT/'research/evidence/source-port/sp07-render/vesa-trap-matrix.json').exists():report=[x for x in json.loads((ROOT/'research/evidence/source-port/sp07-render/vesa-trap-matrix.json').read_text()) if x.get('language','ru')!=args.language]
for lang,mode,label in [('ru','2','09'),('ru','3','09'),('ru','4','10'),('en','2','12'),('en','3','12'),('en','4','12')]:
 if args.language and lang!=args.language:continue
 d=ROOT/f'research/evidence/source-port/sp07-render/{lang}-level1-{mode}-{label}';r=json.loads((d/'report.json').read_text());w,h=r['video_dimensions']
 for heading in [0,45,135]:
  subprocess.run([sys.executable,'-B',str(ROOT/'ports/sdl2/tools/extract_render_state.py'),str(d),'--language',lang,'--level','LEVEL1','--memory',f'heading-{heading}-memory.z','--output',f'state-{heading}'],check=True,stdout=subprocess.DEVNULL)
  subprocess.run([str(ROOT/'build/sdl2/windows/dok_render_probe.exe'),str(ROOT/('originals/pkr/RUS/PKR.RES' if lang=='ru' else 'originals/pkr/ENG/PKE.RES')),str(d/f'port-{heading}.idx'),'LEVEL1','--native-state',str(d/f'state-{heading}.bin'),'--native-light-history','--resolution',str(w),str(h),'--geometry',str(d/f'geometry-{heading}.txt')],check=True,stdout=subprocess.DEVNULL)
  a=(d/f'port-{heading}.idx').read_bytes();b=(d/f'heading-{heading}.idx').read_bytes()[:len(a)];diffs=[dict(x=i%w,y=i//w,actual=x,expected=y) for i,(x,y) in enumerate(zip(a,b)) if x!=y];item=dict(language=lang,mode=mode,width=w,height=h,heading=heading,matching=len(a)-len(diffs),total=len(a),differences=diffs,capture=str(d.relative_to(ROOT)));report.append(item);print(lang,w,h,heading,item['matching'],len(a),len(diffs),flush=True)
  (ROOT/'research/evidence/source-port/sp07-render/vesa-trap-matrix.json').write_text(json.dumps(report,indent=2))
