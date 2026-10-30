"""C21 exact diagnostic count scheduling; no model of ARM cache/timing."""
import json
from pathlib import Path
from archive_ui_performance import ROOT,E,artifact,read

def main():
    archive=E/'pre-map-count/pins.json';rows=read(archive)['source_pins']
    for row in rows:
        saved=artifact(ROOT/row['archive'])
        assert (saved['bytes'],saved['sha256'])==(row['bytes'],row['sha256'])
    old=(ROOT/rows[0]['archive']).read_text('utf8');path=ROOT/rows[0]['path'];new=path.read_text('utf8')
    before='        uint32_t visited=0;for(uint32_t n=0;n<count;++n)visited+=m->visited_snapshot[n]!=0;\n'
    token='m->upload_bytes,m->updates,visited,m->discovery_records'
    after='''        uint32_t count=m->pending_probe[8]*m->pending_probe[9],visited=0;
        for(uint32_t n=0;n<count;++n)visited+=m->visited_snapshot[n]!=0;
        m->pending_probe[34]=visited;
'''
    anchor='        m->upload_bytes+=DOK_MAP_SCREEN_BYTES;++m->updates;'
    assert old.count(before)==old.count(token)==old.count(anchor)==1
    expected=old.replace(before,'',1).replace(token,'m->upload_bytes,m->updates,0,m->discovery_records',1).replace(anchor,after+anchor,1)
    assert new==expected,'Unexpected runtime edit'
    unchanged=[]
    for row in rows[1:]:
        current=artifact(ROOT/row['path'])
        assert (current['bytes'],current['sha256'])==(row['bytes'],row['sha256'])
        unchanged.append(current)
    prepare=new[new.index('void nds_map_prepare('):new.index('void nds_map_transfer(')]
    commit=new[new.index('void nds_map_commit('):new.index('void nds_map_update(')]
    assert 'uint32_t count=m->control.width*m->control.length;' in prepare
    assert 'm->control.width,m->control.length,dok_map_scales' in prepare
    assert prepare.index('memcpy(m->visited_snapshot')<prepare.index('uint32_t p[64]')<prepare.index('memcpy(m->pending_probe,p,sizeof(p))')
    assert commit.index('phase=cpuGetTiming();\n        DC_FlushRange')<commit.index(after)<commit.index('memcpy((void *)g_nds_map_probe')<commit.index('++g_nds_map_probe[2]')
    assert 'm->profile[6]+=cpuGetTiming()-phase' in commit and 'm->profile[7]+=cpuGetTiming()-start' in commit
    snapshot=E/'c21-map-count/ports/nds/platform/map/runtime.c';snapshot.parent.mkdir(parents=True,exist_ok=True);snapshot.write_bytes(path.read_bytes())
    result=dict(task='N21P C21 diagnostic PRE visited count scheduling',passed=True,archive=artifact(archive),old_runtime=artifact(ROOT/rows[0]['archive']),current_runtime=artifact(path),current_snapshot=artifact(snapshot),unchanged_inputs=unchanged,tool=artifact(Path(__file__)),exact_whole_file_move=True,private_pending_placeholder=0,final_descriptor_word=34,count_bound_words=[8,9],source='immutable PRE visited_snapshot',abi_state_layout_unchanged=True,main_raster_transfer_unchanged=True,publication_cpu_accumulates_pre_and_post=True,total_map_cpu_accumulates_own_spans=True,scope='Same byte predicate and active-cell bounds, delayed until after map/GPU/HUD and before final descriptor/even publication. Private pending placeholder is never an even published descriptor. No cache or timing model; root runtime LCD/native/cadence/DMA gates remain required.')
    (E/'map-count-source-supersession.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print(json.dumps(dict(passed=True,private_placeholder=0,final_word=34)))
if __name__=='__main__':main()
