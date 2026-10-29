"""The noninteger DS refresh must not reject a perfect every-other-VBlank run."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from render_metrics import summarize
def profile(intervals):
    now=0;rows=[dict(ordinal=1,case_id=1,flags=64,vblank_end=0)]
    for i,delta in enumerate(intervals):
        row=dict(ordinal=i+2,case_id=1,frame_id=i+1,presented_frame_id=i+1,flags=8,
            vblank_start=now,vblank_end=now+delta,submitted_vertices=4,submitted_quads=1,
            hardware_polygons=1,hardware_vertices=4,disp3d_flags=0,gxstat_flags=0,
            texture_bytes=64,palette_bytes=512,prepare_ticks=1,submit_ticks=1,uploads=0,upload_bytes=0)
        now+=delta;rows.append(row)
    return summarize(rows,dict(timer_hz=33513982,vblank_ticks=560190),[dict(case_id=1,frames=len(intervals))])
normal=profile([2]*360)
assert normal['minimum_new_frames_in_one_second']==29
assert abs(normal['minimum_window_cadence_fps']-33513982/560190/2)<1e-10
assert normal['budget_passed'] and not normal['goal_60_passed']
slow=profile([2]*100+[3]*40+[2]*100)
assert not slow['budget_passed'] and slow['minimum_window_cadence_fps']<normal['minimum_window_cadence_fps']
fast=profile([1]*360)
assert fast['budget_passed'] and fast['goal_60_passed']
print('NDS timestamp-cadence metric: 8 boundary/stall checks passed')

# N13 quad-strip submissions retain all polygons while sharing two vertices.
rows=[dict(ordinal=1,case_id=1,flags=64,vblank_end=0)]
for i in range(120):
    rows.append(dict(ordinal=i+2,case_id=1,frame_id=i+1,presented_frame_id=i+1,flags=8,
        vblank_start=i*2,vblank_end=i*2+2,submitted_vertices=22,submitted_quads=10,
        hardware_polygons=10,hardware_vertices=22,disp3d_flags=0,gxstat_flags=0,
        texture_bytes=64,palette_bytes=8192,prepare_ticks=1,submit_ticks=1,uploads=0,upload_bytes=0))
probe=dict(timer_hz=33513982,vblank_ticks=560190);cases=[dict(case_id=1,frames=120)]
assert summarize(rows,probe,cases,quad_strips=True)['budget_passed']
for invalid in (20,21,42):
    rows[1]['submitted_vertices']=invalid
    try:summarize(rows,probe,cases,quad_strips=True)
    except AssertionError:pass
    else:raise AssertionError('invalid quad-strip vertex stream accepted')
print('N13 quad-strip vertex accounting:4 admission checks passed')
