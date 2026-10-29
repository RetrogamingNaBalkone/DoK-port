"""Accept N17P only after the real production cadence, source and raster gates."""
import hashlib,json,math,shutil
from pathlib import Path
from check_gameplay_performance import ROOT,OUT,EVIDENCE,artifact
def read(p):return json.loads(p.read_text('utf8'))
def distribution(values):
    v=sorted(values)
    return dict(mean=sum(v)/len(v),median=v[(len(v)-1)//2],p95=v[math.ceil(len(v)*.95)-1],p99=v[math.ceil(len(v)*.99)-1],maximum=v[-1])
def compact_metrics(m):
    result={k:v for k,v in m.items() if k not in ('cases','cold_loading')}
    result['case_count']=len(m.get('cases',{}));result['cold_loading_records']=len(m.get('cold_loading',[]))
    return result
def main():
    host=read(EVIDENCE/'host-check.json');native=read(EVIDENCE/'source-replay.json');raster=read(EVIDENCE/'water-raster.json')
    assert host['passed'] and native['passed'] and raster['passed']
    report=dict(task='N17P',status='ready-emulator',passed=True,decision='continue to N18 after this iteration stops',host=host['tests'],source_replay=native,water_gpu_pairs=raster['gpu_pairs'],variants={},artifacts=[])
    for label in ('before-release-ru','final-release-ru','final-release-en','final-release-audit'):
        target=OUT/label;r=read(target/'emulator/result.json');manifest=read(target/'build-manifest.json')
        assert r['passed'] and r['fat_unchanged'] and len(r['cases'])==59 and r['last_observation']['total_frames']==7060
        assert not r['last_observation']['error_bits'] and len(r['gameplay_frames'])==7060
        for row in manifest['artifacts']:assert artifact(ROOT/row['path'])['sha256']==row['sha256']
        assert r['rom_sha256']==artifact(target/'dok_nds_gameplay.nds')['sha256']
        for row in manifest['sources']:
            assert artifact(EVIDENCE/'sources'/label/row['path'])['sha256']==row['sha256'],row['path']
        if label in ('final-release-ru','final-release-en'):assert r['history']['budget_passed'] and not r['history']['goal_60_passed']
        ids=set(map(int,r['history']['cases']));rows=[x for x in r['history_records'] if x['case_id'] in ids and x['flags']&8 and not x['flags']&1]
        gp={g[3]:g for g in r['gameplay_frames']};hz=r['last_observation']['timer_hz'];ms=lambda n:n*1000/hz
        active=[gp[x['frame_id']] for x in rows]
        profile=dict(prepare_ms=distribution([ms(x['prepare_ticks']) for x in rows]),submit_ms=distribution([ms(x['submit_ticks']) for x in rows]),upload_ms=distribution([ms(x['io_upload_ticks']) for x in rows]),component_sum_ms=distribution([ms(sum(x[k] for k in ('prepare_ticks','submit_ticks','io_upload_ticks'))) for x in rows]),simulation_ms=distribution([ms(g[4]) for g in active]),input_poll_to_service_max_ms=max(ms(g[35]) for g in active),normal_ai_high_water=max(g[5] for g in active),normal_projectiles_high_water=max(g[6] for g in active),all_awake_ai_high_water=max(g[5] for g in r['gameplay_frames']),world_bytes=max(g[18] for g in active),world_remaining=393216-max(g[18] for g in active),packed_bytes=max(g[38] for g in active),raw_cache_min_capacity=min(g[39] for g in active),maximum_hardware_polygons=max(x['hardware_polygons'] for x in rows),maximum_hardware_vertices=max(x['hardware_vertices'] for x in rows))
        stack=[]
        for p in (target/'obj').glob('*.su'):
            for line in p.read_text().splitlines():
                cells=line.split('\t')
                if len(cells)==3:stack.append(dict(function=cells[0],bytes=int(cells[1]),kind=cells[2]))
        profile['largest_individual_stack_frames']=sorted(stack,key=lambda x:x['bytes'],reverse=True)[:5]
        profile['stack_scope']='Compiler .su per-function only; not a measured IRQ/SDK call-chain high-water mark. Physical validation N24.'
        result=dict(history=compact_metrics(r['history']),live_routes=compact_metrics(r['live_routes']),all_awake=compact_metrics(r['all_awake']),controlled_death=compact_metrics(r['controlled_death']),audit_stress=compact_metrics(r['all_frame_state_stress']),memory=r['memory'],profile=profile,weapon_captures=len(r['weapon_captures']),core_sha256=r['core_sha256'])
        report['variants'][label]=result
        archive=EVIDENCE/'release'/label;archive.mkdir(parents=True,exist_ok=True)
        for p in [target/'build-manifest.json',target/'dok_nds_gameplay.nds',target/'dok_nds_gameplay.elf',target/'dok_nds_gameplay.map',target/'emulator/result.json']:
            q=archive/p.name;shutil.copy2(p,q);report['artifacts'].append(artifact(q))
        for stem in ('case-30-top.png','case-49-top.png','case-50-top.png','weapon-49-top.png'):
            p=target/'emulator'/stem
            if p.exists():shutil.copy2(p,archive/stem)
    for p in (EVIDENCE/'host-check.json',EVIDENCE/'source-replay.json',EVIDENCE/'water-raster.json',OUT/'scene.dat',OUT/'water-raster.dat'):
        report['artifacts'].append(artifact(p))
    for name in ('nds-n13p-performance.json','nds-n14-items.json','nds-n15-combat.json','nds-n16-ai-movement.json','nds-n17-ai-combat.json'):
        previous=read(ROOT/'analysis'/name);assert previous['passed']
        for row in previous['artifacts']:assert artifact(ROOT/row['path'])['sha256']==row['sha256']
    for row in raster['rows']:
        for k in ('before','after'):assert artifact(ROOT/row[k]['path'])['sha256']==row[k]['sha256']
    report['scope']='DS-mode emulator, fixed fixture/input scripts. Actual native-clock lateness is preserved; normalized source replay compares equal admitted poll/input sequences, not trajectories at equal presentation ordinal. No audio, final HUD/map, Game Over or physical DS FPS claim.'
    report['future_arm9_budget_ms']=dict(audio_submission=.3,dirty_hud=.2,incremental_map=.5,total=1.0,validation='requirements for N18-N21P, not measured implemented features')
    report['remaining']='N18 next, not started. Physical DS/DS Lite/flashcart and measured stack/IRQ N24; full audio/UI budget N21P. All-awake and costly audit are explicitly outside normal cadence gate.'
    (ROOT/'analysis/nds-n17p-performance.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    print(json.dumps({k:dict(fps=v['history']['virtual_fps'],minimum=v['history']['minimum_window_cadence_fps'],passed=v['history']['budget_passed']) for k,v in report['variants'].items()},indent=2))
if __name__=='__main__':main()
