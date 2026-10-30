"""Build N23 with the pinned shared SDK; do not overwrite accepted checkpoints."""
import argparse,json,os,re,shutil,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from check_items import ROOT,artifact,run
OUT=ROOT/'build/nds/n23'

def build(label,features=None):
    assert re.fullmatch(r'[a-z0-9-]+-(?:ru|en)',label)
    target=OUT/label;target.mkdir(parents=True,exist_ok=True)
    if (target/'build-manifest.json').exists():
        raise ValueError('Candidate target is immutable; choose a new label')
    (target/'generated').mkdir(exist_ok=True)
    shutil.copyfile(ROOT/'build/nds/n07/generated/render_trig.inc',target/'generated/render_trig.inc')
    for name,source in (('scene.dat','n20/scene.dat'),('ui.dat','n19/ui.dat'),('frontend.dat','n21/frontend.dat')):
        source_path=ROOT/'build/nds'/source;destination=OUT/name
        if destination.exists():
            assert destination.read_bytes()==source_path.read_bytes(), 'Shared prepared input changed'
        else:shutil.copyfile(source_path,destination)
    env=dict(os.environ,MSYSTEM='UCRT64',CHERE_INVOKING='1',MSYS2_PATH_TYPE='minimal')
    repo='/'+ROOT.as_posix()[0].lower()+ROOT.as_posix()[2:]
    selected=label if features is None else ','.join(features)
    audit=int(label.startswith('audit') or 'audit' in selected)
    largest=int(label.startswith('candidate-') and 'largest' in label)
    # Production/audit use the accepted partial planner. Candidate labels retain
    # explicit experimental selection so old controls remain reproducible.
    partial=1
    def chosen(token):
        return token in selected if features is None else token in features
    fine=int(chosen('fine') or chosen('geomfine'))
    batch=int(chosen('batch'));frontend=int(not chosen('scn'));math=int(chosen('math'));arm=int(chosen('arm'))
    bench=int(chosen('bench') and not chosen('clockbench') and not chosen('mapbench') and not chosen('codecbench'))
    vertical=int(chosen('vert'));corners=int(chosen('corners'));frames=int(chosen('frames'))
    group=8 if chosen('group') else 1
    clip=int(chosen('clip'));clock=int(chosen('fastclock'));clockbench=int(chosen('clockbench'))
    geometry=int(chosen('geomfine'));fused=int(chosen('fused'));floor=int(chosen('floorruns'))
    mapbench=int(chosen('mapbench'));door=int(chosen('door'));workset=int(chosen('workset'))
    codec=2 if chosen('widecodec') else int(chosen('smallcodec'));codecbench=int(chosen('codecbench'));discovery=int(chosen('discovercache'));mapbuttons=int(chosen('mapbuttons'));discoverybits=int(chosen('discoverbits'));discoverysort=int(chosen('discoversort'));compact=int(chosen('compactsource'));objectlookup=int(chosen('object'));overlayreuse=int(chosen('overlayreuse'));combatsar=int(chosen('combatsar'));overlayraw=int(chosen('overlayraw'));replace=int(chosen('replace'));mapfine=int(chosen('mapfine'));sector=int(chosen('sector'));huddma=int(chosen('huddma'));mapseen=int(chosen('mapseen'))

    command="set -eo pipefail\nsource /opt/wonderful/bin/wf-env\nwf-pacman -Q\ncd '"+repo+"'\nmake -C ports/nds -f Makefile.n23 OUTDIR=../../build/nds/n23/"+label+f' SPRITE_LANGUAGE={2 if label.endswith("en") else 1} GAMEPLAY_FRONTEND={frontend} FRONTEND_FIXTURES=0 SPRITE_AUDIT={audit} N21P_AUDIT={audit} N22_STREAM_LARGEST_MISSES={largest} N22_STREAM_PARTIAL_DEFRAG={partial} N23_FINE_PROFILE={fine} N23_BATCH_EVICTION={batch} N23_NATIVE_DIV480={math} N23_NATIVE_ARM={arm} N23_CACHE_BENCH={bench} N23_MAP_VERTICAL={vertical} N23_COLLISION_CORNERS={corners} N23_FRAME_BATCH={frames} N23_CACHE_BENCH_GROUP={group} N23_CLIP_CACHE={clip} N23_CLOCK_FAST32={clock} N23_CLOCK_BENCH={clockbench} N21P_GEOMETRY_PROFILE={geometry} N23_FUSED_CULL={fused} N23_MAP_FLOOR_RUNS={floor} N23_MAP_BENCH={mapbench} N23_DOOR_INPLACE={door} N23_EFFECT_POOL_WORKSET={workset} N23_CODEC_SMALL={codec} N23_CODEC_BENCH={codecbench} N23_DISCOVERY_CACHE={discovery} N23_MAP_BUTTON_CACHE={mapbuttons} N23_DISCOVERY_BITSET={discoverybits} N23_DISCOVERY_SORT={discoverysort} N23_COMPACT_SOURCE={compact} N23_COLLISION_OBJECT={objectlookup} N23_OVERLAY_REUSE={overlayreuse} N23_COMBAT_SAR={combatsar} N23_OVERLAY_REUSE_RAW={overlayraw} N23_CACHE_REPLACE={replace} N23_MAP_FINE={mapfine} N23_MAP_SECTOR_DIRTY={sector} N23_HUD_FULL_DMA={huddma} N23_MAP_DIRECT_SEEN={mapseen} GAMEPLAY_WATER_UV=1 GAMEPLAY_GEOMETRY_CACHE=0 GAMEPLAY_FRAME_STORE=1 GAMEPLAY_FAST_OVERLAY=1\n'
    compilation_inputs=[p for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.S','.ld','.lds') or p.name.startswith('Makefile'))]
    before=[artifact(p) for p in compilation_inputs]
    driver_before=artifact(Path(__file__))
    command=command.replace(' GAMEPLAY_WATER_UV=1',f' N23_PIPELINE_PROFILE={int(chosen("pipeline"))} GAMEPLAY_WATER_UV=1')
    try:log=run([Path.home()/'.local/msys64/usr/bin/bash.exe','--login','-s'],input=command,env=env)
    except Exception as error:
        (target/'build-error.log').write_text(str(error),'utf8');raise
    (target/'build.log').write_text(log,'utf8')
    assert before==[artifact(p) for p in compilation_inputs], 'Compilation inputs changed during build; choose a fresh label'
    assert driver_before==artifact(Path(__file__)), 'Build driver changed during build; choose a fresh label'
    (target/'build-error.log').unlink(missing_ok=True)
    lock=json.loads((ROOT/'ports/nds/toolchain_versions.json').read_text('utf8'))
    assert all(k+' '+v in log for k,v in lock['packages'].items())
    sources=[artifact(p) for p in (ROOT/'ports/nds').rglob('*') if p.is_file() and (p.suffix in ('.c','.h','.inc','.py','.S','.ld','.lds') or p.name.startswith('Makefile'))]
    generated=[OUT/n for n in ('scene.dat','ui.dat','frontend.dat')]
    generated += [ROOT/'build/nds/n19/generated/ui_native.inc',ROOT/'build/nds/n21/generated/frontend_hash.inc',target/'generated/render_trig.inc']
    report=dict(task='N23',label=label,feature_selection=features,compiled_inputs_unchanged=True,frontend=bool(frontend),fine_profile=bool(fine),batch_eviction=bool(batch),native_div480=bool(math),native_arm=bool(arm),cache_bench=bool(bench),map_vertical=bool(vertical),collision_corners=bool(corners),frame_batch=bool(frames),cache_bench_group=group,clip_cache=bool(clip),clock_fast32=bool(clock),clock_bench=bool(clockbench),geometry_profile=bool(geometry),fused_cull=bool(fused),map_floor_runs=bool(floor),map_bench=bool(mapbench),door_inplace=bool(door),effect_pool_workset=bool(workset),codec_small=codec,codec_bench=bool(codecbench),discovery_cache=bool(discovery),map_button_cache=bool(mapbuttons),discovery_bitset=bool(discoverybits),discovery_sort=bool(discoverysort),compact_source=bool(compact),collision_object=bool(objectlookup),overlay_reuse=bool(overlayreuse),combat_sar=bool(combatsar),overlay_reuse_raw=bool(overlayraw),cache_replace=bool(replace),map_fine=bool(mapfine),map_sector_dirty=bool(sector),hud_full_dma=bool(huddma),map_direct_seen=bool(mapseen),frontend_fixtures=False,audit=bool(audit),stream_largest_misses=bool(largest),stream_partial_defrag=bool(partial),packages=lock['packages'],
        artifacts=[artifact(p) for p in target.iterdir() if p.suffix in ('.nds','.elf','.map')],sources=sources,
        generated_inputs=[artifact(p) for p in generated],build_drivers=[artifact(Path(__file__)),artifact(ROOT/'ports/nds/toolchain_versions.json'),
            *[artifact(ROOT/'ports/nds/tools'/name) for name in ('check_items.py','check_interactions.py','prepare_interactions_fixture.py')]],
        arm7_dependency=artifact(ROOT/'build/nds/n18/arm7/dok_audio_arm7.elf'))
    report['pipeline_profile']=bool(chosen('pipeline'))
    (target/'build-manifest.json').write_text(json.dumps(report,indent=2)+'\n','utf8')
    if True:
        evidence=ROOT/'research/evidence/nds/n23/candidates'/label
        for name in ('dok_nds_gameplay.nds','dok_nds_gameplay.elf','dok_nds_gameplay.map','build-manifest.json','build.log'):
            destination=evidence/name;destination.parent.mkdir(parents=True,exist_ok=True)
            assert not destination.exists();shutil.copyfile(target/name,destination)
        for row in report['sources']:
            if Path(row['path']).suffix not in ('.c','.h','.inc','.S','.ld','.lds') and not Path(row['path']).name.startswith('Makefile'):continue
            source=ROOT/row['path'];assert artifact(source)==row
            destination=evidence/'sources'/row['path'];destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
        for row in report['generated_inputs']+report['build_drivers']+[report['arm7_dependency']]:
            source=ROOT/row['path'];assert artifact(source)==row
            destination=evidence/'inputs'/row['path'];destination.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,destination)
    print(json.dumps(report['artifacts']))
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('label')
    parser.add_argument('--features',help='Explicit comma-separated features, independent of immutable target label')
    args=parser.parse_args()
    features=None if args.features is None else args.features.split(',')
    allowed={'batch','math','arm','vert','corners','frames','clip','fastclock','fused','floorruns','door','workset','fine','geomfine','bench','group','clockbench','mapbench','codecbench','smallcodec','widecodec','discovercache','mapbuttons','discoverbits','discoversort','compactsource','object','overlayreuse','combatsar','overlayraw','replace','mapfine','sector','huddma','mapseen','scn','audit'}
    allowed.add('pipeline')
    assert features is None or set(features)<=allowed, 'Unknown feature token'
    build(args.label,features)
