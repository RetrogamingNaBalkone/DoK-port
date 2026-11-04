import subprocess,sys,json,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[3];out=root/'research/evidence/source-port/sp07-render/checks';out.mkdir(exist_ok=True)
res=['originals/pkr/RUS/PKR.RES','originals/pkr/ENG/PKE.RES','originals/Old_Gold_3D/OLD_GOLD.RES'];exe='build/sdl2/windows/'
checks=[('animations',[exe+'dok_animation_scene_tests.exe',*res]),('cadence',[exe+'dok_simulation_cadence_tests.exe',*res]),('monsters',[exe+'dok_monster_probe.exe',*res[:2]]),('old-gold',[exe+'dok_old_gold_probe.exe',res[2]]),('doors',[sys.executable,'-B','ports/sdl2/tools/check_render.py'])]
report=[]
for name,cmd in checks:
 result=subprocess.run(cmd,capture_output=True,text=True,cwd=root);(out/(name+'.log')).write_text(result.stdout+result.stderr);report.append(dict(name=name,command=cmd,exit_code=result.returncode));print(name,result.returncode,flush=True)
(out/'report.json').write_text(json.dumps(report,indent=2));assert all(x['exit_code']==0 for x in report)
