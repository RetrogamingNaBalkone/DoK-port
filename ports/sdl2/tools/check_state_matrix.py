import subprocess,sys,json,struct
from pathlib import Path
root=Path(__file__).resolve().parents[3];report=[]
for lang,level,label in [('ru',2,'05'),('ru',3,'05'),('en',1,'06'),('en',2,'06'),('en',3,'06')]:
 d=root/f'research/evidence/source-port/sp07-render/{lang}-level{level}-vga-{label}';res=root/('originals/pkr/RUS/PKR.RES' if lang=='ru' else 'originals/pkr/ENG/PKE.RES')
 subprocess.run([sys.executable,'-B','ports/sdl2/tools/extract_render_state.py',str(d),'--language',lang,'--level',f'LEVEL{level}'],check=True)
 subprocess.run(['build/sdl2/windows/dok_render_probe.exe',str(res),str(d/'native-port.idx'),f'LEVEL{level}','--native-state',str(d/'native-state.bin'),'--native-light-history','--geometry',str(d/'native-geometry.txt')],check=True)
 actual=(d/'native-port.idx').read_bytes();expected=(d/'surface.idx').read_bytes();m=(d/'memory.bin').read_bytes();hits=[]
 for i,line in enumerate((d/'native-geometry.txt').read_text().splitlines()):
  first,second=line.split('|')
  for side,vs in [('primary',first.split()[3:]),('secondary',second.split())]:
   if not vs:continue
   at=m.find(b''.join(struct.pack('<iii',*map(int,v.split(','))) for v in vs));hits.append(dict(polygon=i,side=side,ram_linear=hex(at) if at>=0 else None,vertices=vs))
 diffs=[dict(x=i%320,y=i//320,actual=x,expected=y) for i,(x,y) in enumerate(zip(actual,expected)) if x!=y]
 item=dict(language=lang,level=level,matching=51200-len(diffs),geometry_matches=sum(x['ram_linear'] is not None for x in hits),geometry_total=len(hits),geometry=hits,differences=diffs,capture=str(d.relative_to(root)));report.append(item)
 print(lang,level,item['matching'],item['geometry_matches'],len(hits),flush=True)
(root/'research/evidence/source-port/sp07-render/state-matrix.json').write_text(json.dumps(report,indent=2))
