"""Compare coherent post-draw original snapshots, preserving every differing pixel."""
import subprocess,sys,json,struct,zlib,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
parser=argparse.ArgumentParser();parser.add_argument('--starts-only',action='store_true');args=parser.parse_args()
report=json.loads((ROOT/'research/evidence/source-port/sp07-render/trap-matrix.json').read_text()) if args.starts_only else []
if args.starts_only:report=[x for x in report if x['heading']!=90]
for lang,level,dirname,headings in [(lang,level,f'{lang}-level{level}-vga-08',[0,45,135]) for lang in ['ru','en'] for level in [1,2,3]]+[('og',4,'og-dos-03',[83,0,45,135])]+[(lang,level,f'{lang}-level{level}-vga-11',[90]) for lang in ['ru','en'] for level in [1,2,3]]:
 if args.starts_only and headings!=[90]:continue
 d=ROOT/'research/evidence/source-port/sp07-render'/dirname;res=ROOT/({'ru':'originals/pkr/RUS/PKR.RES','en':'originals/pkr/ENG/PKE.RES','og':'originals/Old_Gold_3D/OLD_GOLD.RES'}[lang]);items=[]
 for heading in headings:
  subprocess.run([sys.executable,'-B',str(ROOT/'ports/sdl2/tools/extract_render_state.py'),str(d),'--language',lang,'--level',f'LEVEL{level}','--memory',f'heading-{heading}-memory.z','--output',f'state-{heading}'],check=True,stdout=subprocess.DEVNULL)
  subprocess.run([str(ROOT/'build/sdl2/windows/dok_render_probe.exe'),str(res),str(d/f'port-{heading}.idx'),f'LEVEL{level}','--native-state',str(d/f'state-{heading}.bin'),'--native-light-history','--geometry',str(d/f'geometry-{heading}.txt')],check=True,stdout=subprocess.DEVNULL)
  actual=(d/f'port-{heading}.idx').read_bytes();expected=(d/f'heading-{heading}.idx').read_bytes()[:len(actual)];m=zlib.decompress((d/f'heading-{heading}-memory.z').read_bytes());hits=[]
  for i,line in enumerate((d/f'geometry-{heading}.txt').read_text().splitlines()):
   first,second=line.split('|')
   for side,vs in [('primary',first.split()[3:]),('secondary',second.split())]:
    if not vs:continue
    at=m.find(b''.join(struct.pack('<iii',*map(int,v.split(','))) for v in vs));kind='xyz'
    if at<0 and lang=='og':
     xy=[struct.pack('<ii',*map(int,v.split(',')[:2])) for v in vs];pos=-1
     while True:
      pos=m.find(xy[0],pos+1)
      if pos<0:break
      if all(m[pos+12*j:pos+12*j+8]==v for j,v in enumerate(xy)):at=pos;kind='xy (OG center depth slots unused)';break
    hits.append(dict(polygon=i,side=side,ram_linear=hex(at) if at>=0 else None,comparison=kind,vertices=vs))
  diffs=[dict(x=i%320,y=i//320,actual=x,expected=y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y]
  item=dict(language=lang,level=level,heading=heading,matching=len(actual)-len(diffs),total=len(actual),geometry_matches=sum(x['ram_linear'] is not None for x in hits),geometry_total=len(hits),geometry=hits,differences=diffs,capture=str(d.relative_to(ROOT)));report.append(item)
  print(lang,level,heading,item['matching'],len(actual),item['geometry_matches'],len(hits),flush=True)
  (ROOT/'research/evidence/source-port/sp07-render/trap-matrix.json').write_text(json.dumps(report,indent=2))
