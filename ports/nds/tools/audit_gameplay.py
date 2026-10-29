"""N17P original active callbacks from the integrated, source-time route replay."""
import json
from audit_ai_combat import Original,models,trace_fields,originals,ROOT
def main():
    out=ROOT/'research/evidence/nds/n17p';before=originals();model=models();fields=trace_fields()
    rows=[dict(zip(fields,json.loads(s),strict=True)) for s in (out/'callbacks.jsonl').read_text().splitlines()]
    counts={}
    for language in (1,2):
        native=Original(language);count=0
        for row in rows:
            if row['language']==language:native.callback(row,model[language,row['level'],row['glyph']]);count+=1
        counts[str(language)]=count
    assert before==originals()
    report=dict(task='N17P',passed=True,callbacks=len(rows),languages=counts,original_sha256_before=before,original_sha256_after=originals(),scope='Original active/hero/stat control flow; same explicit external boundaries as N17; integrated moving world supplies actual front/sight and scalar state. Native N09/N10 movement/collision are pinned separate oracles, not re-executed here.')
    (out/'source-replay.json').write_text(json.dumps(report,indent=2)+'\n','utf8');print(json.dumps(report))
if __name__=='__main__':main()
