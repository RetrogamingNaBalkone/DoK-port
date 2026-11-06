"""Validate SP12 full native evidence and publish its compact tracked summary."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT/'research/evidence/source-port/sp12-native-complete'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 summary=dict(schema=1,date='2026-11-06',task='SP12',evidence=str(BASE.relative_to(ROOT)).replace('\\','/'),matrices={})
 for name,total,strict,adapt in [('report.json',36040,36040,0),('activation-summary.json',5830,5830,0),('angles-summary.json',1575,1575,0),('death-summary.json',2120,1798,322),('exhaust-summary.json',3,3,0)]:
  p=BASE/name;d=json.loads(p.read_text());rows=d['profiles'];assert rows and d['sha256_before']==d['sha256_after'];counts=[sum(r.get(k,0)for r in rows)for k in ('count','strict_matches','adaptations','mismatches')];assert counts==[total,strict,adapt,0],(name,counts)
  summary['matrices'][name]=dict(count=total,strict=strict,adaptations=adapt,unexpected=0,profiles=rows,report_sha256=digest(p))
  if name=='report.json':
   for row in rows:
    report=json.loads((BASE/(row['profile']+'-'+row['level']+'-report.json')).read_text());assert len(report['vectors'])==row['count'] and all(v['equal']for v in report['vectors'])
 for stage in ('before','after'):
  p=BASE/f'embedded-trajectories-{stage}.json';d=json.loads(p.read_text());assert d['cases']==50 and d['callbacks']==600 and len(d['reports'])==50
  summary['trajectories_'+stage]=dict(cases=50,callbacks=600,source_placed=d['source_placed'],strict_trajectories=sum(r['strict_equal']for r in d['reports']),non_position_difference_cases=sum(bool(r['non_position_differences'])for r in d['reports']),report_sha256=digest(p))
 assert summary['trajectories_after']['strict_trajectories']==50
 summary['angle_ram']=[dict(profile=r['profile'],status=r['status'],address=r['address'],data_base=r['data_base'])for r in json.loads((BASE/'angle-ram/summary.json').read_text())];assert len(summary['angle_ram'])==3 and all(r['status']=='PASS'for r in summary['angle_ram'])
 summary['sources_sha256']={str(p.relative_to(ROOT)).replace('\\','/'):digest(p)for p in [ROOT/'ports/sdl2/tests/monster_ai_probe.cpp',*[ROOT/'ports/sdl2/tools'/n for n in ['check_monster_ai_oracle.py','check_monster_ai_trajectories.py','check_monster_ai_angles.py','export_monster_ai_audit.py']]]}
 summary['boundaries']=['controlled clocks','external scheduler/HUD','audio device returns (SP13 separately)','projectile allocation entry (SP11 separately)']
 summary['movement_regression']=dict(report='research/evidence/source-port/sp10-post-ai-regression/report.json',native=35399,boundary_valid_origin_foreign_occupancy=51,current_bank=990,pickup_fps=[30,60,144])
 (ROOT/'analysis/summaries/source-port-sp12-native-complete-summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('SP12 native full evidence validated: PASS')
if __name__=='__main__':main()
