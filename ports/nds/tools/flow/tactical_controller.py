"""Bounded, read-only observer targeting for the ordinary-input N22 route bot.

Only physical joypad buttons are returned/sent. No teleport, health/stat edits,
save states, gameplay mailbox commands, invulnerability, or AI switches exist.
The helper is navigation assistance, not a replacement for production combat.
"""
from __future__ import annotations
import math
from analyze_tactics import aim_targets

B,Y,SELECT,START,UP,DOWN,LEFT,RIGHT,A,X,L,R=range(12)


def _delta(button,heading,distance):
    angle=math.radians(heading);s,c=math.sin(angle),math.cos(angle)
    return {UP:(-s*distance,c*distance),DOWN:(s*distance,-c*distance),
            LEFT:(-c*distance,-s*distance),RIGHT:(c*distance,s*distance)}[button]


def _safe_step(level,mmap,tmap,x,z,nx,nz,dead_glyphs):
    """Conservative planning test, never collision admission or a world write.

    Avoids visible solid cells and negative-health pickups. Dynamic door flags
    are not inferred: this battle aid does not try to retreat through a door.
    The real native collision code still decides every actual movement.
    """
    origin=(int(z)//level.side)*level.width+int(x)//level.side
    radius=level.data['settings'][7]
    for fraction in (.5,1):
        xx,zz=x+(nx-x)*fraction,z+(nz-z)*fraction
        for px,pz in ((xx-radius,zz-radius),(xx-radius,zz+radius),
                      (xx+radius,zz-radius),(xx+radius,zz+radius)):
            cx,cz=int(px)//level.side,int(pz)//level.side
            if px<0 or pz<0 or not 0<=cx<level.width or not 0<=cz<level.height:return False
            cell=cz*level.width+cx
            actor,tile=level.objects[mmap[cell]],level.objects[tmap[cell]]
            if cell!=origin and actor['class']!=1 and actor['glyph'] not in dead_glyphs and not actor['flags']&16:return False
            if tile['class']==7 or not tile['flags']&16:return False
            if tile['class']==3:
                values={f['offset']:f.get('number') for f in tile['fields']}
                if values.get(0x54)==0 and (values.get(0x56) or 0)<0:return False
    return True


def choose_buttons(level,mmap,tmap,pose,charge,selected_weapon,*,dead_glyphs=(),
                   preferred=(),phase=0,limit_cells=5,excluded_targets=()):
    """Return one physical battle step or None when there is no valid target.

    Input maps must be a stable published snapshot; pose is committed x,y,z,h.
    Caller rereads state after each native hero callback. No fine-turn loop is
    hidden here. `selected_weapon` is the actual selected slot, not possession.
    """
    if charge<=0 or selected_weapon not in range(1,9):return None
    dead_glyphs=set(dead_glyphs)
    clean=bytes(32 if g in dead_glyphs else g for g in mmap)
    x,_,z,heading=pose
    targets=aim_targets(level,clean,tmap,x,z,limit_cells=limit_cells,excluded_targets=excluded_targets)
    if not targets:return None
    target=targets[0]
    angle=(target['heading']-heading+180)%360-180
    tolerance=max(3,min(12,math.degrees(math.atan2(180,target['distance']))))
    # Circle/retreat under fire instead of waiting motionless for an exact
    # heading. Probe one half-cell ahead, accepting only conservative free steps.
    candidates=[]
    tx,tz=[(q+.5)*level.side for q in target['xy']]
    for button in (DOWN,LEFT,RIGHT,UP):
        dx,dz=_delta(button,heading,level.side*.45)
        if not _safe_step(level,clean,tmap,x,z,x+dx,z+dz,dead_glyphs):continue
        outward=((x-tx)*dx+(z-tz)*dz)/max(target['distance'],1)
        lateral=abs((x-tx)*dz-(z-tz)*dx)/max(target['distance'],1)
        # Prefer retreat when close; side-stepping maintains range farther away.
        weight=outward*(2 if target['distance']<level.side*2 else .25)+lateral
        if button in preferred:weight+=level.side*(3 if target['distance']>=level.side*2 else .15)
        if button==(LEFT if (phase//8)%2==0 else RIGHT):weight+=level.side*.05
        candidates.append((weight,button))
    buttons=[]
    if candidates:
        _,movement=max(candidates)
        buttons=[movement,L]
    if abs(angle)>tolerance:buttons.append(Y if angle>0 else A)
    else:buttons.append(R)
    return dict(buttons=buttons,target=target,angle=angle,tolerance=tolerance,
                selected_weapon=selected_weapon,charge=charge,
                movement_safe=bool(candidates))


def tactical_window(observer,level,*,max_callbacks=8,dead_glyphs=(),preferred=()):
    """Run at most 4..12 ordinary game_step calls, then return to navigation.

    Requires RouteObserver's map_snapshot(level), game_step(buttons), last_pose,
    probe, flow, frames, report. `map_snapshot` must already filter dead glyphs
    or supply them via dead_glyphs. The helper never reads unflushed private RAM.
    A world/retry/menu transition stops the window immediately.
    """
    budget=max(4,min(12,int(max_callbacks)))
    initial_flow=observer.flow
    identity=(initial_flow[4],initial_flow[14]) if initial_flow else None
    record=dict(start_frame=observer.frames,acted=False,callbacks=0,steps=[],
                method='Published map snapshot -> physical joypad buttons only')
    for phase in range(budget):
        if not observer.probe or observer.probe[3]!=8 or not observer.last_pose:break
        if identity and (not observer.flow or (observer.flow[4],observer.flow[14])!=identity):break
        maps=observer.map_snapshot(level)
        if not maps:break
        decision=choose_buttons(level,*maps,observer.last_pose,int(observer.probe[49]),
                                int(observer.probe[51]),dead_glyphs=dead_glyphs,
                                preferred=preferred,phase=phase)
        if decision is None:break
        record['steps'].append(dict(video_frame=observer.frames,pose=list(observer.last_pose),**decision))
        observer.game_step(decision['buttons']);record['acted']=True;record['callbacks']+=1
    record['end_frame']=observer.frames
    if record['acted']:observer.report.setdefault('tactical_windows',[]).append(record)
    return record
