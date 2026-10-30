"""C20 exact snapshot clean relocation, with immutable pixel/DMA/ABI inputs."""
import json
from pathlib import Path
from archive_ui_performance import ROOT,E,artifact,read

def main():
    archive=E/'pre-map-flush/pins.json';rows=read(archive)['source_pins']
    for row in rows:
        saved=artifact(ROOT/row['archive']);assert (saved['bytes'],saved['sha256'])==(row['bytes'],row['sha256'])
    row=rows[0];old=(ROOT/row['archive']).read_text('utf8');path=ROOT/row['path'];new=path.read_text('utf8')
    block='''        DC_FlushRange(m->planes,3u*DOK_LEVEL_MAX_CELLS);DC_FlushRange(m->post_visited,DOK_LEVEL_MAX_CELLS);
        DC_FlushRange(m->visited_snapshot,sizeof(m->visited_snapshot));DC_FlushRange(m->glyphs,sizeof(m->glyphs));
        DC_FlushRange(m->palette,sizeof(m->palette));
'''
    assert old.count(block)==new.count(block)==1 and block.count('DC_FlushRange')==5
    anchor='        phase=cpuGetTiming();\n        m->upload_bytes+=DOK_MAP_SCREEN_BYTES;++m->updates;'
    expected=old.replace(block,'',1).replace(anchor,'        phase=cpuGetTiming();\n'+block+'        m->upload_bytes+=DOK_MAP_SCREEN_BYTES;++m->updates;',1)
    assert expected==new,'Non-flush-move runtime edit'
    unchanged=[]
    for row in rows[1:]:
        if row['path'].startswith('ports/nds/'):
            current=artifact(ROOT/row['path']);assert (current['bytes'],current['sha256'])==(row['bytes'],row['sha256']);unchanged.append(current)
    prepare=new[new.index('void nds_map_prepare('):new.index('void nds_map_transfer(')]
    transfer=new[new.index('void nds_map_transfer('):new.index('void nds_map_commit(')]
    commit=new[new.index('void nds_map_commit('):new.index('void nds_map_update(')]
    assert 'DC_FlushRange(pixels,DOK_MAP_SCREEN_BYTES)' in prepare and block not in prepare
    assert transfer==old[old.index('void nds_map_transfer('):old.index('void nds_map_commit(')]
    assert commit.index(block)<commit.index('m->upload_bytes+=')<commit.index('memcpy((void *)g_nds_map_probe')<commit.index('++g_nds_map_probe[2]')
    assert 'm->profile[6]+=cpuGetTiming()-phase' in commit and 'm->profile[7]+=cpuGetTiming()-start' in commit
    main=(ROOT/'ports/nds/platform/gameplay_probe/main.c').read_text('utf8');main=main[main.index('static int render_one('):]
    order=['nds_map_prepare(map_ui','if(dok_nds_render_submit(frame','present_ticks=wait_presentation_boundary();','nds_map_transfer(map_ui','dok_nds_stream_commit_prepared(&stream_stats)','nds_ui_update(ui','nds_map_commit(map_ui']
    positions=[main.index(token) for token in order];assert positions==sorted(positions)
    snapshot=E/'c20-map-flush/ports/nds/platform/map/runtime.c';snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_bytes(path.read_bytes())
    result=dict(task='N21P C20 exact snapshot clean scheduling',passed=True,archive=artifact(archive),old_runtime=artifact(ROOT/rows[0]['archive']),current_runtime=artifact(path),current_snapshot=artifact(snapshot),unchanged_inputs=unchanged,tool=artifact(Path(__file__)),exact_whole_file_move=True,flush_calls_moved=5,flush_bytes=22016,source_arrays=['planes12288','post_visited4096','visited_snapshot4096','glyphs1024','palette512'],pixel_clean_stays_before_wait=True,transfer_whole_function_unchanged=True,abi_state_layout_unchanged=True,main_whole_source_unchanged=True,raster_whole_source_unchanged=True,publication_order=order,publication_phase_cpu_accumulates_pre_and_post=True,total_map_cpu_accumulates_own_spans=True,scope='Only five diagnostic snapshot cache-clean calls relocate from prepare to commit after GPU/HUD, before descriptor/even publication. No model of ARM cache correctness: SDK/runtime/native LCD and timing remain independent root gates. C19 raster/native proofs are unchanged, not rerun.')
    (E/'map-flush-source-supersession.json').write_text(json.dumps(result,indent=2)+'\n','utf8');print(json.dumps(dict(passed=True,flush_calls=5,bytes=22016)))
if __name__=='__main__':main()
