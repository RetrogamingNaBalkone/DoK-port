"""Compact N19 acceptance record; fails closed until independent gates pass."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[3]
EVIDENCE=ROOT/'research/evidence/nds/n19'
def artifact(path):
    data=path.read_bytes()
    return dict(path=path.relative_to(ROOT).as_posix(),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def read(path):return json.loads(path.read_text('utf8'))
def main():
    inputs={
        'source':ROOT/'analysis/nds-n19-ui-source.json',
        'original':ROOT/'analysis/nds-n19-ui-original.json',
        'host':ROOT/'analysis/nds-n19-ui-host.json',
        'presentation':ROOT/'analysis/nds-n19-presentation.json',
        'ui_language':ROOT/'analysis/nds-n19-ui-language-runtime.json',
        'gameplay_regression':EVIDENCE/'gameplay-regression.json',
        'visual':EVIDENCE/'visual-review.json'}
    gates={name:read(path) for name,path in inputs.items()}
    assert all(g.get('passed') is True for g in gates.values()),'Independent N19 gate remains open'
    runtime=[]
    for language in ('ru','en'):
        target=ROOT/f'build/nds/n19/final-release-{language}'
        hud_path=target/'hud/result.json';renderer_path=target/'emulator/result.json'
        hud=read(hud_path);renderer=read(renderer_path);audio_path=target/'audio/result.json';audio=read(audio_path)
        assert hud['passed'] and renderer['passed']
        assert len(renderer['cases'])==62 and hud['pixel_checks']>0 and hud['hardware_pixel_checks']>0
        assert not any(hud.get('missing_stages',{}).values())
        assert audio['passed'] and audio['catalog_completed'] and len(audio['catalog'])==24
        rom=artifact(target/'dok_nds_gameplay.nds')
        assert rom['sha256']==hud['rom_sha256']==renderer['rom_sha256']
        row=dict(language=language,rom=rom,hud_report=artifact(hud_path),renderer_report=artifact(renderer_path),
            observed_hud_rows=len(hud['rows']),index8_pixel_checks=hud['pixel_checks'],hardware_pixel_checks=hud['hardware_pixel_checks'],
            missing_stages=hud.get('missing_stages'),renderer_checks_passed=True,audio_report=artifact(audio_path),audio_catalog=len(audio['catalog']))
        if 'memory' in renderer:row['memory']=renderer['memory']
        for key in ('history','live_routes'):
            row[key]={k:v for k,v in renderer[key].items() if k not in ('cases','cold_loading')}
        runtime.append(row)
    report=dict(task='N19',status='ready-emulator',passed=True,software_checks_passed=True,
        gates={name:artifact(path) for name,path in inputs.items()},source=gates['source'],original=gates['original'],
        host=gates['host'],presentation=gates['presentation'],gameplay_regression=gates['gameplay_regression']['gameplay'],
        ui_language=gates['ui_language'],
        runtime=runtime,fixture=read(EVIDENCE/'inputs.json'),ui_data=artifact(ROOT/'build/nds/n19/ui.dat'),
        supersession=artifact(EVIDENCE/'supersession.json'),archive=artifact(EVIDENCE/'final-archive.json'),
        user_adjustment='HP/charge numbers are left aligned immediately after original icons, 2-3px visible gap',
        adaptations=['256x38 NDS layout; original 8-pixel-wide fonts and signed16 fields preserved',
            'Full notifications paginate at28columns;20054native ticks per page instead of DOS HUD-call countdown',
            'Explicit diagnostic numeric states never write inventory or world'],
        joint_performance_budget_passed=all(r['history']['budget_passed'] for r in runtime),
        physical_hardware=False,next_task='N20',limits=['DS/DSLite LCD readability, actual FPS/DMA/VBlank and palette behavior remain N24',
            'Functional HUD gates pass; joint renderer performance budget has a single normal3VBlank interval and remains N21P',
            'Trial lives are a separate signedWORD initialized3; campaign/death flow owns later updates',
            'Menu/story presentation remains N21; lower automap remains N20'])
    out=ROOT/'analysis/nds-n19-hud.json';out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,runtime=runtime),ensure_ascii=False))
if __name__=='__main__':main()
