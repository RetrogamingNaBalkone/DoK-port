"""Presentation-interval metrics for N12 and accumulated N13P fixtures."""
from __future__ import annotations
from bisect import bisect_right, bisect_left
from collections import Counter
import math
def distribution(values,refresh):
    ordered=sorted(values)
    def percentile(p):return ordered[max(0,math.ceil(p*len(ordered))-1)]
    return dict(intervals=len(values),histogram=dict(sorted(Counter(values).items())),
        virtual_fps=len(values)*refresh/sum(values),median=percentile(.5),p95=percentile(.95),p99=percentile(.99),max=max(values),
        fraction_over_one=sum(x>1 for x in values)/len(values),fraction_over_two=sum(x>2 for x in values)/len(values))
def summarize(records,probe,cases,*,quad_strips=False):
    refresh=probe['timer_hz']/probe['vblank_ticks'];active=[];cold=[];groups=[];bycase={}
    assert len({r['ordinal'] for r in records})==len(records)
    for case in cases:
        rows=[r for r in records if r['case_id']==case['case_id']]
        rendered=[r for r in rows if r['flags']&8]
        assert len(rendered)==case['frames']
        sync=[r for r in rows if r['flags']==64]
        assert len(sync)==1 and sync[0]['ordinal']+1==rendered[0]['ordinal']
        assert sync[0]['vblank_end']==rendered[0]['vblank_start']
        previous=None;group=[];values=[]
        for row in rendered:
            delta=(row['vblank_end']-row['vblank_start'])&0xffffffff
            assert 0<delta<0x80000000
            q=row['submitted_quads'];v=row['submitted_vertices']
            assert (2*q+2<=v<=4*q and not v%2) if quad_strips else v==4*q
            assert row['hardware_polygons']<=2048 and row['hardware_vertices']<=6144
            assert not row['disp3d_flags'] and not row['gxstat_flags']
            assert row['texture_bytes']<=393216 and row['palette_bytes']<=16384
            if previous:
                assert previous['vblank_end']==row['vblank_start']
                assert previous['presented_frame_id']+1==row['presented_frame_id']
            previous=row
            if row['flags']&1:
                cold.append(dict(case_id=case['case_id'],frames=delta,prepare_ticks=row['prepare_ticks'],upload_ticks=row['io_upload_ticks'],bytes=row['upload_bytes']))
                if group:groups.append(group);group=[]
            else:
                values.append(delta);active.append(row)
                if not group:group.append(row['vblank_start'])
                group.append(row['vblank_end'])
        if group:groups.append(group)
        if values:bycase[str(case['case_id'])]=distribution(values,refresh)
    values=[r['vblank_end']-r['vblank_start'] for r in active]
    windows=[bisect_right(g,start+refresh)-bisect_right(g,start) for g in groups for start in g if start+refresh<=g[-1]]
    # Count complete, genuinely new presentation intervals covering >=1s. The
    # actual span is measured from both enclosing timestamps, not assumed1s.
    # A perfect two-VBlank cadence is29.913Hz, even when an unbracketed1s bin
    # contains29 frames because refresh is59.826Hz. Keep that integer count too.
    rates=[]
    for g in groups:
        for index,start in enumerate(g):
            if start+refresh>g[-1]:break
            end=bisect_left(g,start+refresh)
            rates.append((end-index)*refresh/(g[end]-start))
    result=distribution(values,refresh)
    result.update(refresh_hz=refresh,cases=bycase,cold_loading=cold,rendered_frames=sum(c['frames'] for c in cases),
        minimum_new_frames_in_one_second=min(windows) if windows else None,checked_one_second_windows=len(windows),
        minimum_window_cadence_fps=min(rates) if rates else None,
        cadence_window_method='Complete new-frame intervals spanning at least1s, enclosing timestamp duration; integer1s counts reported separately',
        prepare_ms=sum(r['prepare_ticks'] for r in active)/len(active)*1000/probe['timer_hz'],
        submit_ms=sum(r['submit_ticks'] for r in active)/len(active)*1000/probe['timer_hz'],
        uploads=sum(r['uploads'] for r in active),upload_bytes=sum(r['upload_bytes'] for r in active),
        maximum_texture_bytes=max(r['texture_bytes'] for r in active),maximum_palette_bytes=max(r['palette_bytes'] for r in active))
    result['budget_passed']=result['fraction_over_two']<=.01 and bool(rates) and min(rates)+1e-12>=refresh/2
    result['goal_60_passed']=result['fraction_over_one']<=.01
    return result
