/* N23 player correction: exact segment/slab intersections. Original movement
 * (including remainder/quarter SAR rounding) remains in combat.c. No floats. */
typedef struct HitTime { int64_t n,d; unsigned open; } HitTime;
static int hit_compare(HitTime a,HitTime b)
{
    int64_t v=a.n*b.d-b.n*a.d;
    return v<0?-1:v>0?1:0;
}
static int hit_axis(int32_t a,int32_t b,int32_t low,int32_t high,
                    HitTime *enter,HitTime *exit)
{
    int64_t delta=(int64_t)b-a;HitTime lo,hi;
    if(!delta)return a>=low&&a<high;
    if(delta>0){lo=(HitTime){(int64_t)low-a,delta,0};hi=(HitTime){(int64_t)high-a,delta,1};}
    else {lo=(HitTime){(int64_t)a-high,-delta,1};hi=(HitTime){(int64_t)a-low,-delta,0};}
    int c=hit_compare(lo,*enter);
    if(c>0)*enter=lo;else if(!c)enter->open|=lo.open;
    c=hit_compare(hi,*exit);
    if(c<0)*exit=hi;else if(!c)exit->open|=hi.open;
    c=hit_compare(*enter,*exit);
    return c<0||(!c&&!enter->open&&!exit->open);
}
static int hit_box(int32_t x,int32_t z,int32_t xx,int32_t zz,
                   int32_t left,int32_t top,int32_t side,HitTime *at)
{
    HitTime end={1,1,0};*at=(HitTime){0,1,0};
    return hit_axis(x,xx,left,left+side,at,&end)&&
           hit_axis(z,zz,top,top+side,at,&end);
}
static int hit_before(HitTime a,HitTime b)
{int c=hit_compare(a,b);return c<0||(!c&&a.open<b.open);}

/* Dynamic positions are borrowed from the bound N16 owner at this exclusive
 * simulation boundary. Dead actors and stale worlds cannot become targets. */
static dok_status player_contact(dok_combat *s,dok_projectile *p,int32_t x,int32_t z,
    uint32_t *accepted,uint16_t *cl,int16_t *height,dok_object_ref *target)
{
    const dok_initial_world *w=s->items->world;const dok_level_definition *d=w->definition;
    int32_t side=d->settings[3];HitTime best={2,1,0};
    const dok_level_object *chosen=NULL;dok_object_ref ref={0};
    dok_level_object border={0};border.class_id=2;
    *accepted=0;
    if(x<0||z<0||x>=d->settings[0]*side||z>=d->settings[2]*side){
        chosen=&border;best=(HitTime){0,1,0};
    }else if(p->x<0||p->z<0||p->x>=d->settings[0]*side||p->z>=d->settings[2]*side){
        HitTime start={0,1,0},end={1,1,0};
        (void)hit_axis(x,p->x,0,d->settings[0]*side,&start,&end);
        (void)hit_axis(z,p->z,0,d->settings[2]*side,&start,&end);
        best=end;best.open=0;chosen=&border;
    }
    /* Quarter segments touch at most four cells. Include both endpoint cells
     * and test their areas so a diagonal cannot skip a wall between samples. */
    int32_t minx=(x<p->x?x:p->x)/side,maxx=(x>p->x?x:p->x)/side;
    int32_t minz=(z<p->z?z:p->z)/side,maxz=(z>p->z?z:p->z)/side;
    if(minx<0)minx=0;if(minz<0)minz=0;
    if(maxx>=d->settings[0])maxx=d->settings[0]-1;
    if(maxz>=d->settings[2])maxz=d->settings[2]-1;
    for(int32_t cz=minz;cz<=maxz;++cz)for(int32_t cx=minx;cx<=maxx;++cx){
        uint32_t index=(uint32_t)cz*(uint16_t)d->settings[0]+(uint32_t)cx;
        for(unsigned plane=0;plane<2u;++plane){
            uint32_t glyph=w->maps[plane][index];const dok_level_object *o=object(s,glyph);HitTime t;
            if(!o)return DOK_STATUS_INVALID_STATE;
            if(o->class_id==0u||o->class_id==1u||o->class_id==8u||o->class_id==9u)continue;
            if(s->items->collision->flags[glyph]&(o->class_id==7u?8u:0x18u))continue;
            if(hit_box(x,z,p->x,p->z,cx*side,cz*side,side,&t)&&hit_before(t,best)){
                best=t;chosen=o;ref=w->objects[w->glyph_index[glyph]-1u].reference;
            }
        }
    }
    if(s->actors->epoch!=s->epoch)return DOK_STATUS_STALE_REFERENCE;
    for(uint32_t i=0;i<s->actors->count;++i){
        const dok_enemy *e=&s->actors->enemies[i];HitTime t;
        if(e->corpse||e->health<=0||!w->objects[w->glyph_index[e->glyph]-1u].placements)continue;
        if(hit_box(x,z,p->x,p->z,e->motion.x-side/2,e->motion.z-side/2,side,&t)&&hit_before(t,best)){
            chosen=object(s,e->glyph);if(!chosen)return DOK_STATUS_INVALID_STATE;
            best=t;ref=w->objects[w->glyph_index[e->glyph]-1u].reference;
        }
    }
    if(!chosen)return DOK_STATUS_OK;
    *cl=chosen->class_id;*height=chosen->height_parameter;*target=ref;
    if(*cl==9u&&s->services.hit)return s->services.hit(s->services.context,ref,p->owner,p->damage,accepted);
    *accepted=1;return DOK_STATUS_OK;
}
