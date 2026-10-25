#include "dok/collision.h"

static unsigned cell(const dok_runtime *w,int32_t x,int32_t z)
{
    int32_t side=w->level->settings[DOK_LEVEL_CELL_SIDE];
    int32_t col=x/side,row=z/side;
    int32_t width=w->level->settings[DOK_LEVEL_WIDTH],length=w->level->settings[DOK_LEVEL_LENGTH];
    if(col<0) col=0;
    if(col>=width) col=width-1;
    if(row<0) row=0;
    if(row>=length) row=length-1;
    return (unsigned)(row*width+col);
}
static int pass(const dok_runtime *w,unsigned plane,int32_t x,int32_t z)
{
    unsigned glyph=w->maps[plane*w->level->cells+cell(w,x,z)];
    unsigned id=w->level->glyph_index[glyph];
    return id && id<=w->object_count && (w->objects[id-1].flags&0x10)!=0;
}
int dok_collision_clear(const dok_runtime *w,int32_t x,int32_t z,int16_t r)
{
    /* Native order: MMap --,+-,-+,++; then the same four TMap corners.
     * The footprint is an axis-aligned square, not a circle. */
    for(unsigned p=0;p<2;++p) {
        if(!pass(w,p,x-r,z-r) || !pass(w,p,x-r,z+r) ||
           !pass(w,p,x+r,z-r) || !pass(w,p,x+r,z+r)) return 0;
    }
    return 1;
}
static int32_t edge(int32_t n,int32_t side,int32_t radius,int negative)
{ return n-n%side+(negative?radius:side-radius-1); }
void dok_collision_resolve(const dok_runtime *w,int32_t *px,int32_t *pz,int32_t nx,int32_t nz)
{
    int32_t x=*px,z=*pz,side=w->level->settings[DOK_LEVEL_CELL_SIDE];
    int16_t r=(int16_t)w->level->settings[DOK_LEVEL_MIN_DISTANCE];
    int32_t dx=nx-x,dz=nz-z;
    int32_t mx=(x<nx?x:nx)/side*side+side/2;
    if(mx<(x<nx?x:nx) || mx>(x<nx?nx:x)) mx=x<nx?x:nx;
    /* Original computes a checked Z center, then unconditionally replaces it
     * with the smaller endpoint's cell center at RUS 0a90..0ad7. */
    int32_t mz=(z<nz?z:nz)/side*side+side/2;
    int negx=nx<x,negz=nz<z;
    if(negx) dx=-dx;
    if(negz) dz=-dz;
    int32_t ox=nx,oz=mz;
    if(dx>dz) {
        if(dok_collision_clear(w,nx,z,r)) {
            if(dok_collision_clear(w,nx,nz,r)) {oz=nz;goto done;}
        } else {
            if(dok_collision_clear(w,nx,mz,r)) goto done;
            ox=edge(x,side,r,negx);
            if(dok_collision_clear(w,ox,nz,r)) {oz=nz;goto done;}
        }
        oz=edge(z,side,r,negz);
    } else {
        if(dok_collision_clear(w,x,nz,r)) {
            oz=nz;
            if(dok_collision_clear(w,nx,nz,r)) goto done;
        } else {
            if(dok_collision_clear(w,mx,nz,r)) {ox=mx;oz=nz;goto done;}
            oz=edge(z,side,r,negz);
            if(dok_collision_clear(w,nx,oz,r)) goto done;
        }
        ox=edge(x,side,r,negx);
    }
done:
    *px=ox;*pz=oz;
}
static dok_runtime_object *door_at(dok_runtime *w,unsigned at)
{
    unsigned id=w->level->glyph_index[w->maps[w->level->cells+at]];
    if(!id || id>w->object_count || w->level->definitions[id-1].class_id!=7) return 0;
    return &w->objects[id-1];
}
static int inside(const dok_runtime *w,int32_t x,int32_t z,int32_t r)
{
    int32_t side=w->level->settings[DOK_LEVEL_CELL_SIDE];
    return x>=r && z>=r && x<=(int32_t)w->level->settings[DOK_LEVEL_WIDTH]*side-r-1 &&
        z<=(int32_t)w->level->settings[DOK_LEVEL_LENGTH]*side-r-1;
}
void dok_collision_displace(dok_runtime *w,dok_runtime_actor *a,int16_t dx,int16_t dz)
{
    if(!w || !w->level) return;
    int32_t r=w->level->settings[DOK_LEVEL_MIN_DISTANCE];
    int32_t side=w->level->settings[DOK_LEVEL_CELL_SIDE];
    if(r<0 || r>=side/2 || !inside(w,a->x,a->z,r)) return;
    unsigned old=cell(w,a->x,a->z);
    uint8_t saved=w->maps[old];
    /* Do not erase an unrelated blocker in malformed/teleported state. */
    if(saved!=a->glyph && saved!=' ') return;
    dok_runtime_object *door=door_at(w,old);
    if(door && door->state.door.state_56) --door->state.door.state_56;
    w->maps[old]=' ';
    int32_t nx=a->x+dx,nz=a->z+dz;
    int32_t x=a->x,z=a->z;
    /* A normal quarter step is <=180 units. Reject oversized diagnostic or
     * corrupt requests rather than tunnel across cells or overflow resolver. */
    if(inside(w,nx,nz,r) && dx>-side/2 && dx<side/2 && dz>-side/2 && dz<side/2) {
        dok_collision_resolve(w,&x,&z,nx,nz);
        if(inside(w,x,z,r) && dok_collision_clear(w,x,z,(int16_t)r)) {a->x=x;a->z=z;}
    }
    unsigned next=cell(w,a->x,a->z);
    door=door_at(w,next);
    if(door) door->state.door.state_56=(int16_t)((uint16_t)door->state.door.state_56+1u);
    w->maps[next]=a->glyph;
}
void dok_collision_move(dok_runtime_actor *a,int16_t distance,int strafe,void *context)
{
    dok_runtime *w=context;
    if(!w || !w->level) return;
    int32_t side=w->level->settings[DOK_LEVEL_CELL_SIDE];
    if(distance<=-side/2 || distance>=side/2) return;
    dok_runtime_actor candidate=*a;dok_motion_free(&candidate,distance,strafe,0);
    dok_collision_displace(w,a,(int16_t)(candidate.x-a->x),(int16_t)(candidate.z-a->z));
}
