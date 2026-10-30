#include "dok/hotcode.h"
#include "dok/map_discovery.h"
#include <limits.h>
#include <string.h>

DOK_NDS_HOT_CODE static int32_t bits(uint32_t u)
{return u<=INT32_MAX?(int32_t)u:-1-(int32_t)(UINT32_MAX-u);}
DOK_NDS_HOT_CODE static int32_t wrap(int64_t n)
{return bits((uint32_t)(uint64_t)n);}
DOK_NDS_HOT_CODE static int32_t sar(int32_t n,uint32_t b)
{uint32_t u=(uint32_t)n;return bits((u>>b)|((0u-(u>>31))<<(32u-b)));}
DOK_NDS_HOT_CODE static int16_t word(int32_t n)
{uint32_t u=(uint32_t)n&65535u;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);}
DOK_NDS_HOT_CODE static int32_t rotate(const dok_nds_render_camera *c,int32_t x,int32_t z,uint32_t depth)
{
    uint32_t dx=(uint32_t)x-(uint32_t)c->x,dz=(uint32_t)z-(uint32_t)c->z;
    return bits(depth?dz*(uint32_t)c->cos_q15-dx*(uint32_t)c->sin_q15
        :dx*(uint32_t)c->cos_q15+dz*(uint32_t)c->sin_q15);
}
DOK_NDS_HOT_CODE static int camera_ok(const dok_nds_render_camera *c)
{return c && c->sin_q15>=-32768 && c->sin_q15<=32768 && c->cos_q15>=-32768 && c->cos_q15<=32768;}

DOK_NDS_HOT_CODE dok_status dok_map_discovery_bounds(const dok_initial_world *world,
    const dok_nds_render_camera *camera,uint32_t heading,dok_render_gather_bounds *out)
{
    int32_t s,c,t,r,bx,bz,side,x[4],z[4];uint32_t i;dok_render_gather_bounds result;
    if(!world || !world->definition || !camera || !out ||
        !dok_render_trig(heading,&s,&c) || !dok_render_trig(360u-heading,&t,&r) ||
        s!=camera->sin_q15 || c!=camera->cos_q15)return DOK_STATUS_INVALID_ARGUMENT;
    side=world->definition->settings[DOK_LEVEL_CELL_SIDE];
    if(side<=0)return DOK_STATUS_INVALID_ARGUMENT;
    bx=wrap((int64_t)camera->x+sar(wrap((int64_t)2*side*s),15));
    bz=wrap((int64_t)camera->z-sar(wrap((int64_t)2*side*c),15));
    x[0]=wrap((int64_t)bx+sar(wrap((int64_t)side*r),15));
    z[0]=wrap((int64_t)bz+sar(wrap(-(int64_t)side*t),15));
    x[1]=wrap((int64_t)bx+sar(wrap(-(int64_t)side*r),15));
    z[1]=wrap((int64_t)bz+sar(wrap((int64_t)side*t),15));
    x[2]=wrap((int64_t)bx+sar(wrap((int64_t)6*side*r+(int64_t)9*side*t),15));
    z[2]=wrap((int64_t)bz+sar(wrap((int64_t)9*side*r-(int64_t)6*side*t),15));
    x[3]=wrap((int64_t)bx+sar(wrap(-(int64_t)6*side*r+(int64_t)9*side*t),15));
    z[3]=wrap((int64_t)bz+sar(wrap((int64_t)9*side*r+(int64_t)6*side*t),15));
    for(i=0;i<4u;++i){x[i]=word(x[i]/side);z[i]=word(z[i]/side);}
    result.min_x=result.max_x=x[0];result.min_z=result.max_z=z[0];
    for(i=1;i<4u;++i){if(x[i]<result.min_x)result.min_x=x[i];if(z[i]<result.min_z)result.min_z=z[i];}
    /* Source max evaluates p0,p2,p3 after replacing p1 with min. */
    for(i=2;i<4u;++i){if(x[i]>result.max_x)result.max_x=x[i];if(z[i]>result.max_z)result.max_z=z[i];}
    if(result.min_x<0)result.min_x=0;if(result.min_z<0)result.min_z=0;
    if(result.max_x>=world->definition->settings[DOK_LEVEL_WIDTH])result.max_x=world->definition->settings[DOK_LEVEL_WIDTH]-1;
    if(result.max_z>=world->definition->settings[DOK_LEVEL_LENGTH])result.max_z=world->definition->settings[DOK_LEVEL_LENGTH]-1;
    *out=result;return DOK_STATUS_OK;
}

DOK_NDS_HOT_CODE static dok_status record_geometry(const dok_geometry_quad *q,
    const dok_nds_render_camera *c,dok_map_render_record *out,
    dok_map_projection_factor *factors)
{
    dok_map_render_record r={0};
    int32_t depth[4],lateral[4],screen[4],minimum=INT32_MAX,maximum=INT32_MIN;
    uint32_t i,count,horizontal;
    if(!q || !out || !camera_ok(c) || q->kind>DOK_GEOMETRY_CEILING_BAND ||
        q->cell_x>=64u || q->cell_z>=64u) return DOK_STATUS_INVALID_ARGUMENT;
    r.cell=(uint16_t)((uint32_t)q->cell_z*64u+q->cell_x);
    horizontal=q->kind==DOK_GEOMETRY_FLOOR_QUAD || q->kind==DOK_GEOMETRY_CEILING_QUAD;
    count=horizontal?4u:2u;
    for(i=0;i<count;++i) {
        depth[i]=sar(rotate(c,q->vertices[i].x,q->vertices[i].z,1u),15);
        lateral[i]=sar(rotate(c,q->vertices[i].x,q->vertices[i].z,0u),15);
    }
    if(horizontal) {
        uint32_t positive=0;
        for(i=0;i<4u;++i) if(depth[i]>0) positive=1;
        if(!positive) { *out=r;return DOK_STATUS_OK; }
        for(i=0;i<4u;++i) if(depth[i]<0)
            depth[i]=sar(rotate(c,q->vertices[i].x,q->vertices[i].z,1u),20);
    } else {
        if(depth[0]<=0 && depth[1]<=0) { *out=r;return DOK_STATUS_OK; }
        if(depth[0]<0) {
            int32_t n=wrap((int64_t)(lateral[0]-lateral[1])*depth[1]);
            /* n is the original low DWORD. SAR15 bounds both depths to
             * [-65536,65535], so this positive divisor cannot overflow.
             * Keep the source signed DWORD IDIV instead of software i64. */
            lateral[0]=wrap((int64_t)(n/(depth[1]-depth[0]))+lateral[1]);depth[0]=0;
        } else if(depth[1]<0) {
            int32_t n=wrap((int64_t)(lateral[1]-lateral[0])*depth[0]);
            lateral[1]=wrap((int64_t)(n/(depth[0]-depth[1]))+lateral[0]);depth[1]=0;
        }
    }
    for(i=0;i<count;++i) {
        int32_t denominator=wrap((int64_t)depth[i]+200),factor;
        if(!denominator) return DOK_STATUS_UNSUPPORTED;
        if(factors) {
            uint32_t slot=((uint32_t)denominator*UINT32_C(0x9E3779B9))>>24;
            dok_map_projection_factor *entry=&factors[slot];
            if(entry->denominator!=denominator) {
                entry->denominator=denominator;entry->factor=6553600/denominator;
            }
            factor=entry->factor;
        } else factor=6553600/denominator;
        screen[i]=wrap((int64_t)sar(wrap((int64_t)lateral[i]*factor),15)+159);
        if(screen[i]<minimum)minimum=screen[i];
        if(screen[i]>maximum)maximum=screen[i];
    }
    if(!horizontal && (minimum>319 || maximum<0 ||
        (q->kind!=DOK_GEOMETRY_DOOR && minimum==maximum))) { *out=r;return DOK_STATUS_OK; }
    r.left=word(minimum);r.right=word(maximum);
    /* Builder ORs 8 into paired/single horizontal constructor flags. */
    r.flags=(uint16_t)((q->object_flags&255u)|1u|(horizontal?8u:0u));
    r.key=sar(wrap((int64_t)depth[0]+depth[horizontal?2u:1u]),1);
    if(q->kind==DOK_GEOMETRY_DOOR) {
        int32_t width=q->vertices[1].x-q->vertices[0].x;
        if(!width)width=q->vertices[1].z-q->vertices[0].z;
        if(width<0)width=-width;
        r.key=wrap((int64_t)r.key-sar(width,2));
    }
    if(r.key<0)r.key=0;
    *out=r;return DOK_STATUS_OK;
}

DOK_NDS_HOT_CODE dok_status dok_map_record_geometry(const dok_geometry_quad *q,
    const dok_nds_render_camera *c,dok_map_render_record *out)
{return record_geometry(q,c,out,NULL);}

DOK_NDS_HOT_CODE dok_status dok_map_record_sprite(const dok_sprite_pose *p,
    const dok_nds_render_camera *c,dok_map_render_record *out)
{
    dok_sprite_projection projection;dok_map_render_record r={0};dok_status status;
    if(!p || !out || !camera_ok(c) ||
        (p->source_id&UINT32_C(0xFEFFF000))!=UINT32_C(0x80000000))
        return DOK_STATUS_INVALID_ARGUMENT;
    status=dok_sprite_project(c,p,&projection);if(status!=DOK_STATUS_OK)return status;
    r.cell=(uint16_t)(p->source_id&4095u);
    if(projection.visible) {
        r.key=projection.sort_key;r.flags=(uint16_t)((p->flags&255u)|1u);
        r.left=word(projection.left<projection.right?projection.left:projection.right);
        r.right=word(projection.left>projection.right?projection.left:projection.right);
    }
    *out=r;return DOK_STATUS_OK;
}

DOK_NDS_HOT_CODE dok_status dok_map_discovery_records(dok_map_discovery_workspace *w,
    unsigned char visited[DOK_LEVEL_MAX_CELLS],dok_map_discovery_stats *out)
{
    dok_map_discovery_stats stats={0};uint32_t i,gap;int32_t remaining=320;
    if(!w || !visited || !out || w->count>DOK_MAP_DISCOVERY_RECORDS)
        return DOK_STATUS_INVALID_ARGUMENT;
    for(i=0;i<w->count;++i) {
        if(w->records[i].cell>=DOK_LEVEL_MAX_CELLS || !(w->records[i].flags&1u))
            return DOK_STATUS_INVALID_ARGUMENT;
        w->order[i]=(uint16_t)i;
    }
    /* Original Shell/gap sort, strict key comparison preserves its tie effects. */
    for(gap=w->count/2u;gap;gap/=2u) for(i=gap;i<w->count;++i) {
        int32_t j=(int32_t)(i-gap);
        while(j>=0 && w->records[w->order[(uint32_t)j]].key<
                w->records[w->order[(uint32_t)j+gap]].key) {
            uint16_t t=w->order[(uint32_t)j];
            w->order[(uint32_t)j]=w->order[(uint32_t)j+gap];
            w->order[(uint32_t)j+gap]=t;j-=(int32_t)gap;
        }
    }
    memset(w->columns,1,sizeof(w->columns));stats.records=w->count;
    for(i=w->count;i>0u;--i) {
        dok_map_render_record *r=&w->records[w->order[i-1u]];
        int32_t left=r->left<0?0:r->left,right=r->right>319?319:r->right,x;
        uint32_t visible=0;
        /* Native clears the sort key for EVERY record before column testing. */
        r->key=0;
        if(remaining>0)for(x=left;x<=right;++x)if(w->columns[x]) {
            visible=1;
            if(r->flags&2u){w->columns[x]=0;--remaining;}else break;
        }
        if(!visible){r->flags=(uint16_t)(r->flags&~1u);r->key=0;++stats.occluded;}
        else {
            ++stats.eligible;
            if(!visited[r->cell]){visited[r->cell]=1;++stats.newly_visited;}
        }
    }
    *out=stats;return DOK_STATUS_OK;
}

DOK_NDS_HOT_CODE static dok_status append(dok_map_discovery_workspace *w,dok_map_render_record record)
{
    if(!(record.flags&1u))return DOK_STATUS_OK;
    if(w->count==DOK_MAP_DISCOVERY_RECORDS)return DOK_STATUS_LIMIT_REACHED;
    w->records[w->count++]=record;return DOK_STATUS_OK;
}

/* Only source constructor X/Z and its kind/flags reach the column producer.
 * Keep these inputs directly rather than building y/UV/material vertices,
 * copying a full 12-quad cell and then discarding those fields. */
typedef struct map_shape {
    int32_t ax,az,bx,bz;
    uint16_t flags;
    uint8_t glyph,kind;
} map_shape;

DOK_NDS_HOT_CODE static const dok_level_object *map_object(const dok_initial_world *w,
    uint32_t plane,uint32_t x,uint32_t z)
{
    const dok_level_definition *d=w->definition;
    uint32_t glyph=w->maps[plane][z*(uint32_t)d->settings[DOK_LEVEL_WIDTH]+x];
    uint32_t index=d->glyph_index[glyph];
    return !index || index>d->object_count?NULL:&d->objects[index-1u];
}
DOK_NDS_HOT_CODE static int32_t map_field54(const dok_level_object *o,const dok_level_definition *d,
    const dok_geometry_field54 *prepared)
{return prepared[o-d->objects].value;}
DOK_NDS_HOT_CODE static uint32_t map_surface(const dok_level_object *o,const dok_level_definition *d,
    const dok_geometry_field54 *prepared)
{return (o->class_id==0u || o->class_id==12u || o->class_id==13u) &&
    prepared[o-d->objects].is_word;}
DOK_NDS_HOT_CODE static uint32_t map_image(const dok_geometry_image *i)
{return i->material && i->width && i->height && i->width<=32767u && i->height<=32767u;}
DOK_NDS_HOT_CODE static uint32_t map_door(const dok_level_object *o)
{return o->class_id==2u || o->class_id==4u || o->class_id==7u;}
DOK_NDS_HOT_CODE static void map_vertical(map_shape *s,const dok_level_object *o,uint32_t kind,
    int32_t x,int32_t z,int32_t width,uint32_t orientation,uint32_t centered)
{
    s->ax=s->bx=x;s->az=s->bz=z;
    s->flags=o->flags;s->glyph=o->glyph;s->kind=(uint8_t)kind;
    if(centered) {
        int32_t half=width>=0?width/2:(width-1)/2;
        if(!orientation){s->az=z+half;s->bx=x+width;s->bz=s->az;}
        else{s->ax=x+half;s->bx=s->ax;s->bz=z+width;}
    } else switch(orientation) {
        case 0u:s->bx=x+width;break;
        case 1u:s->az=z+width;break;
        case 2u:s->ax=x+width;s->az=z+width;s->bz=z+width;break;
        default:s->ax=x+width;s->bx=s->ax;s->bz=z+width;break;
    }
}
DOK_NDS_HOT_CODE static dok_status map_cell(const dok_initial_world *w,const dok_geometry_binding bindings[256],
    uint32_t x,uint32_t z,map_shape shapes[DOK_GEOMETRY_CELL_MAX],uint32_t *count,
    const dok_geometry_field54 *prepared)
{
    const dok_level_definition *d=w->definition;
    const dok_level_object *t,*f,*nt[4],*nf[4];const dok_geometry_image *image;
    uint32_t width,length,nx[4],nz[4],i,n=0;int32_t side,xx,zz,inset,scaled;
    /* Match the full pure builder's validation even when a field is not used
     * for horizontal disclosure (including ceiling image and cell height). */
    if(d->language!=1u || !d->object_count || d->object_count>DOK_LEVEL_MAX_OBJECTS ||
        d->settings[DOK_LEVEL_WIDTH]<=0 || d->settings[DOK_LEVEL_LENGTH]<=0 ||
        d->settings[DOK_LEVEL_WIDTH]>64 || d->settings[DOK_LEVEL_LENGTH]>64 ||
        d->settings[DOK_LEVEL_CELL_SIDE]<=0 || d->settings[DOK_LEVEL_CELL_HEIGHT]<=0)
        return DOK_STATUS_INVALID_STATE;
    width=(uint32_t)d->settings[DOK_LEVEL_WIDTH];length=(uint32_t)d->settings[DOK_LEVEL_LENGTH];
    if(x>=width || z>=length)return DOK_STATUS_INVALID_STATE;
    t=map_object(w,DOK_WORLD_TMAP,x,z);f=map_object(w,DOK_WORLD_FCMAP,x,z);
    if(!t || !f || !map_surface(f,d,prepared))return DOK_STATUS_INVALID_STATE;
    nx[0]=x;nx[1]=x?x-1u:0u;nx[2]=x;nx[3]=x+1u<width?x+1u:x;
    nz[0]=z?z-1u:0u;nz[1]=z;nz[2]=z+1u<length?z+1u:z;nz[3]=z;
    for(i=0;i<4u;++i) {
        nt[i]=map_object(w,DOK_WORLD_TMAP,nx[i],nz[i]);
        nf[i]=map_object(w,DOK_WORLD_FCMAP,nx[i],nz[i]);
        if(!nt[i] || !nf[i] || !map_surface(nf[i],d,prepared))return DOK_STATUS_INVALID_STATE;
    }
    side=d->settings[DOK_LEVEL_CELL_SIDE];xx=(int32_t)x*side;zz=(int32_t)z*side;
    inset=map_field54(f,d,prepared);
    if(inset>=0) {
        image=&bindings[f->glyph].images[DOK_GEOMETRY_CEILING_SIDE];
        for(i=0;i<4u;++i)if(map_field54(nf[i],d,prepared)<inset) {
            if(!map_image(image))return DOK_STATUS_INVALID_STATE;
            map_vertical(&shapes[n++],f,DOK_GEOMETRY_CEILING_BAND,xx,zz,side,i,0u);
        }
    }
    if(t->class_id==2u) {
        image=&bindings[t->glyph].images[DOK_GEOMETRY_IMAGE];
        if(!map_image(image))return DOK_STATUS_INVALID_STATE;
        scaled=word((int32_t)image->width*t->width_parameter);
        for(i=0;i<4u;++i)if(nt[i]->class_id!=2u)
            map_vertical(&shapes[n++],t,DOK_GEOMETRY_WALL,xx,zz,scaled,i,0u);
    } else {
        image=&bindings[f->glyph].images[DOK_GEOMETRY_FLOOR];
        if(!map_image(image))return DOK_STATUS_INVALID_STATE;
        shapes[n++]=(map_shape){xx,zz,xx+side,zz+side,f->flags,f->glyph,DOK_GEOMETRY_FLOOR_QUAD};
        if(inset>=0 && !map_image(&bindings[f->glyph].images[DOK_GEOMETRY_CEILING]))
            return DOK_STATUS_INVALID_STATE;
        /* Paired ceiling has no independent native render record. */
        if(t->class_id==4u || t->class_id==7u) {
            image=&bindings[t->glyph].images[DOK_GEOMETRY_IMAGE];
            if(!map_image(image))return DOK_STATUS_INVALID_STATE;
            scaled=word((int32_t)image->width*t->width_parameter);
            for(i=0;i<2u;++i)if(!map_door(nt[i]) && !map_door(nt[i+2u]))
                map_vertical(&shapes[n++],t,DOK_GEOMETRY_DOOR,xx,zz,scaled,i,1u);
        }
    }
    *count=n;return DOK_STATUS_OK;
}

DOK_NDS_HOT_CODE dok_status dok_map_discovery_update(const dok_initial_world *world,
    const dok_geometry_binding bindings[256],const dok_sprite_list *sprites,
    const uint16_t *flags,const dok_nds_render_camera *camera,uint32_t heading,
    dok_map_discovery_workspace *workspace,unsigned char visited[DOK_LEVEL_MAX_CELLS],
    dok_map_discovery_stats *out)
{
    dok_render_gather_bounds bounds;map_shape shapes[DOK_GEOMETRY_CELL_MAX];
    int32_t x,z;uint32_t i,plane;
    if(!world || !world->definition || !bindings || !workspace || !visited || !out ||
        (sprites&&(sprites->world_epoch!=world->epoch || sprites->count>DOK_SPRITE_LIMIT)) ||
        dok_map_discovery_bounds(world,camera,heading,&bounds)!=DOK_STATUS_OK)
        return DOK_STATUS_INVALID_ARGUMENT;
    dok_geometry_prepare_fields(world->definition,workspace->field54);
    workspace->count=0;
    for(i=0;i<256u;++i)workspace->factors[i].denominator=0;
    if(sprites) {
        /* Only heads within the native gather are read. No full 4096-cell
         * clear, and no O(gathered cells * all sprites) rescan. */
        for(z=bounds.min_z;z<=bounds.max_z;++z)for(x=bounds.min_x;x<=bounds.max_x;++x)
            workspace->sprite_heads[(uint32_t)z*64u+(uint32_t)x]=UINT16_MAX;
        for(i=sprites->count;i>0u;--i) {
            const dok_sprite_pose *p=&sprites->poses[i-1u];uint32_t tag,cx,cz;
            if((p->source_id&UINT32_C(0xFEFFF000))!=UINT32_C(0x80000000))continue;
            tag=p->source_id&4095u;cx=tag&63u;cz=tag>>6;
            if((int32_t)cx<bounds.min_x || (int32_t)cx>bounds.max_x ||
                (int32_t)cz<bounds.min_z || (int32_t)cz>bounds.max_z)continue;
            workspace->sprite_next[i-1u]=workspace->sprite_heads[tag];
            workspace->sprite_heads[tag]=(uint16_t)(i-1u);
        }
    }
    for(z=bounds.min_z;z<=bounds.max_z;++z)for(x=bounds.min_x;x<=bounds.max_x;++x) {
        uint32_t count;
        if(map_cell(world,bindings,(uint32_t)x,(uint32_t)z,shapes,&count,workspace->field54)!=DOK_STATUS_OK)
            return DOK_STATUS_INVALID_STATE;
        for(i=0;i<count;++i) {
            const map_shape *s=&shapes[i];
            dok_map_render_record r;dok_geometry_quad q;dok_status status;
            q.kind=s->kind;q.cell_x=(uint8_t)x;q.cell_z=(uint8_t)z;
            q.object_flags=flags?flags[s->glyph]:s->flags;
            q.vertices[0].x=s->ax;q.vertices[1].x=s->bx;
            if(s->kind==DOK_GEOMETRY_FLOOR_QUAD) {
                q.vertices[0].z=q.vertices[1].z=s->bz;
                q.vertices[2].x=s->bx;q.vertices[2].z=s->az;
                q.vertices[3].x=s->ax;q.vertices[3].z=s->az;
            } else {q.vertices[0].z=s->az;q.vertices[1].z=s->bz;}
            status=record_geometry(&q,camera,&r,workspace->factors);if(status!=DOK_STATUS_OK)return status;
            status=append(workspace,r);if(status!=DOK_STATUS_OK)return status;
        }
        /* Native cell order: TMap billboard then MMap billboard. N12 gathers
         * MMap first; select by explicit plane rather than copying its order. */
        if(sprites)for(plane=2u;plane>0u;--plane)for(i=workspace->sprite_heads[(uint32_t)z*64u+(uint32_t)x];
                i!=UINT16_MAX;i=workspace->sprite_next[i]) {
            const dok_sprite_pose *p=&sprites->poses[i];dok_map_render_record r;dok_status status;
            /* NDS correction: pool/synthetic IDs have no native grid-cell
             * provenance. In DOS the global object path incorrectly clamps
             * low WORD world coordinates directly to 0..63 (no CellSide
             * division), leaking the bottom-right cell. Do not reproduce it. */
            if(((p->source_id>>24)&1u)!=plane-1u)continue;
            status=dok_map_record_sprite(p,camera,&r);if(status!=DOK_STATUS_OK)return status;
            status=append(workspace,r);if(status!=DOK_STATUS_OK)return status;
        }
    }
    return dok_map_discovery_records(workspace,visited,out);
}
