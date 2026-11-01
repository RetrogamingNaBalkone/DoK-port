/* G21/BUG01: intentional correction of native player-shot cell quantization.
 * A target retains the nominal cell-sized footprint, centered on its actor.
 * Shot geometry, per-quarter SAR rounding, damage and enemy shots stay native.
 * Two straight segments describe the whole poll exactly: the remainder and
 * repeated identical quarter vectors. Scan actors once, then order the first
 * actor contact against the original terrain traversal with exact fractions.
 * No persistent RAM or per-substep actor scan is required. */
typedef struct {int32_t n,d;unsigned open;} shot_time;
typedef struct {int id,phase;shot_time at;} player_contact;
#if defined(__arm__)
/* ARM7 Thumb has no long multiply; keep exact cross-products in a small ROM
 * ARM function instead of introducing a generic 64-bit compiler runtime. */
__attribute__((target("arm"),noinline))
#endif
static int time_order(shot_time a,shot_time b) {
 int64_t x=(int64_t)a.n*b.d,y=(int64_t)b.n*a.d;
 return x<y?-1:x>y?1:0;
}
static int shot_inside(int32_t x,int32_t z,int32_t left,int32_t top,unsigned side) {
 return (uint32_t)x-(uint32_t)left<side && (uint32_t)z-(uint32_t)top<side;
}
static int shot_slab(int32_t p,int32_t delta,int32_t left,unsigned side,shot_time *enter,shot_time *exit) {
 if(!delta)return (uint32_t)p-(uint32_t)left<side;
 shot_time a,b;
 if(delta>0) {a=(shot_time){left-p,delta,0};b=(shot_time){left+(int32_t)side-p,delta,1};}
 else {a=(shot_time){p-left-(int32_t)side,-delta,1};b=(shot_time){p-left,-delta,0};}
 int order=time_order(a,*enter);
 if(order>0)*enter=a;else if(!order)enter->open|=a.open;
 order=time_order(b,*exit);
 if(order<0)*exit=b;else if(!order)exit->open|=b.open;
 order=time_order(*enter,*exit);
 return order<0 || (!order && !enter->open && !exit->open);
}
static int shot_box(int32_t x,int32_t z,int32_t ex,int32_t ez,int32_t left,int32_t top,unsigned side,shot_time *at) {
 if(shot_inside(x,z,left,top,side)) {*at=(shot_time){0,1,0};return 1;}
 /* Broad phase rejects most of the level without divisions or wide products. */
 int32_t xmin=x<ex?x:ex,xmax=x>ex?x:ex,zmin=z<ez?z:ez,zmax=z>ez?z:ez;
 if(left>xmax || left+(int32_t)side<=xmin || top>zmax || top+(int32_t)side<=zmin)return 0;
 shot_time enter={0,1,0},exit={1,1,0};
 if(!shot_slab(x,ex-x,left,side,&enter,&exit) || !shot_slab(z,ez-z,top,side,&enter,&exit))return 0;
 int order=time_order(enter,exit);
 /* A pure interior corner touch crosses no area. An actual final point in
  * the half-open box still counts, matching the flight endpoint policy. */
 if(!order && !(enter.n==enter.d && shot_inside(ex,ez,left,top,side)))return 0;
 *at=enter;return 1;
}
static player_contact player_target(dok_combat *c,dok_combat_projectile *p,const combat_targets *targets,
 int32_t rx,int32_t rz,int32_t ex,int32_t ez,int whole) {
 const dok_runtime *w=c->world;unsigned side=w->level->settings[DOK_LEVEL_CELL_SIDE];
 uint32_t xmax=(unsigned)w->level->settings[DOK_LEVEL_WIDTH]*side,zmax=(unsigned)w->level->settings[DOK_LEVEL_LENGTH]*side;
 player_contact best={-1,2,{0,1,0}};
 for(unsigned at=targets->players;at<targets->total;++at) {
  unsigned id=targets->ids[at];
  if((w->objects[id].flags&0x400) || !w->objects[id].state.monster.health)continue;
  const dok_runtime_actor *a=dok_runtime_actor_at(w,(uint16_t)id);
  if(!a || (uint32_t)a->x>=xmax || (uint32_t)a->z>=zmax)continue;
  int32_t left=a->x-(int32_t)(side/2),top=a->z-(int32_t)(side/2);
  shot_time hit;int phase=0;
  if(!shot_box(p->x,p->z,rx,rz,left,top,side,&hit)) {
   if(best.phase==0 || whole<=0 || !shot_box(rx,rz,ex,ez,left,top,side,&hit))continue;
   phase=1;
  }
  int order=time_order(hit,best.at);
  if(phase<best.phase || (phase==best.phase && (order<0 || (!order && best.at.open && !hit.open))))
   best=(player_contact){(int)id,phase,hit};
 }
 return best;
}
static int terrain_grid(dok_combat *c,int x,int z) {
 int width=c->world->level->settings[DOK_LEVEL_WIDTH],length=c->world->level->settings[DOK_LEVEL_LENGTH];
 return x<0 || z<0 || x>=width || z>=length?-2:terrain_cell(c,z*width+x);
}
static int player_terrain(dok_combat *c,int32_t x,int32_t z,int32_t ex,int32_t ez,shot_time *at) {
 int old=cell(c,x,z),next=cell(c,ex,ez);
 if(old==next)return -1;
 int side=c->world->level->settings[DOK_LEVEL_CELL_SIDE],width=c->world->level->settings[DOK_LEVEL_WIDTH];
 int ox=old%width,oz=old/width,nx=ex<0?-1:ex/side,nz=ez<0?-1:ez/side;
 int dx=ex-x,dz=ez-z,ax=dx<0?-dx:dx,az=dz<0?-dz:dz;
 shot_time tx={dx>0?side-x%side:x%side,ax?ax:1,dx<0},tz={dz>0?side-z%side:z%side,az?az:1,dz<0};
 if(ox!=nx && oz!=nz) {
  int order=time_order(tx,tz);
  if(order) {
   int hit=order<0?terrain_grid(c,nx,oz):terrain_grid(c,ox,nz);
   if(hit!=-1) {*at=order<0?tx:tz;return hit;}
  }
  *at=order>0?tx:tz;
 }else *at=ox!=nx?tx:tz;
 return next<0?-2:terrain_cell(c,next);
}
static int player_flight(dok_combat *c,dok_combat_projectile *p,int whole,int remainder,int quarter,const combat_targets *targets) {
 int initial=cell(c,p->x,p->z);if(initial<0)return -2;
 if(whole<0)return target_cell(c,p,initial,targets);
 dok_runtime_actor r={0},q={0};r.x=p->x;r.z=p->z;r.heading=q.heading=p->heading;
 dok_motion_free(&r,(int16_t)remainder,0,0);dok_motion_free(&q,(int16_t)quarter,0,0);
 player_contact actor=player_target(c,p,targets,r.x,r.z,r.x+q.x*whole,r.z+q.z*whole,whole);
 if(actor.phase==0 && !actor.at.n && !actor.at.open)return actor.id;
 int hit=terrain_cell(c,initial);
 for(int step=0;hit==-1 && step<=whole;++step) {
  int32_t x=p->x,z=p->z;
  if(!step) {p->x=r.x;p->z=r.z;}else {p->x+=q.x;p->z+=q.z;}
  shot_time wall={1,1,0};hit=player_terrain(c,x,z,p->x,p->z,&wall);
  if(actor.phase==(step?1:0) && time_order(actor.at,(shot_time){step?step:1,step?whole:1,0})<=0) {
   if(step) {wall.n+=(step-1)*wall.d;wall.d*=whole;}
   int order=time_order(actor.at,wall);
   if(hit==-1 || order<0 || (!order && !actor.at.open))hit=actor.id;
  }
 }
 return hit;
}
