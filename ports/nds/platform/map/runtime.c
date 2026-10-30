#include "runtime.h"
#include <nds.h>
#include <string.h>
#ifndef N21P_AUDIT
#define N21P_AUDIT 1
#endif
_Alignas(32) volatile uint32_t g_nds_map_probe[64];
_Static_assert(sizeof(dok_map_glyph)==4,"N20 metadata ABI");
/* Platform-only GCC/Clang alias extension permits aligned WORD reads of byte
 * arrays without strict-aliasing UB. The alignment guard also covers hosts and
 * future slab layouts; byte tails retain exact odd-sized map semantics. */
typedef uint32_t map_alias_word __attribute__((__may_alias__));
static int bytes_differ(const void *a,const void *b,uint32_t bytes)
{
    const uint8_t *x=a,*y=b;
    if(!(((uintptr_t)x|(uintptr_t)y)&3u)){
        const map_alias_word *wx=(const map_alias_word *)(const void *)x,*wy=(const map_alias_word *)(const void *)y;
        while(bytes>=16u){
            if((wx[0]^wy[0])|(wx[1]^wy[1])|(wx[2]^wy[2])|(wx[3]^wy[3]))return 1;
            wx+=4;wy+=4;bytes-=16;
        }
        while(bytes>=4u){if(*wx++!=*wy++)return 1;bytes-=4;}
        x=(const uint8_t *)(const void *)wx;y=(const uint8_t *)(const void *)wy;
    }
    while(bytes--)if(*x++!=*y++)return 1;
    return 0;
}
#ifdef N21P_HOST_TEST
int nds_map_test_bytes_differ(const void *a,const void *b,uint32_t n){return bytes_differ(a,b,n);}
#endif
static uint32_t class1_visit(NdsMapRuntime *m,dok_render_world *w,uint32_t count)
{
    uint8_t *map=w->initial.maps[0],*visited=w->initial.maps[3];uint32_t writes=0,n=0;
    if(m->class1_count<=4u&&!((uintptr_t)map&3u)){
        /* has-zero-byte may also flag a neighbour after borrow. This is only
         * a rejection filter: exact byte equality gates every actual write. */
        for(uint32_t g=0;g<m->class1_count;++g){
            uint32_t repeated=(uint32_t)m->class1_glyphs[g]*0x01010101u;n=0;
            for(;n+4u<=count;n+=4u){
                uint32_t value=*(const map_alias_word *)(const void *)(map+n);
                uint32_t match=value^repeated;
                if(((match-0x01010101u)&~match&0x80808080u)!=0u)
                    for(uint32_t b=0;b<4u;++b)if(map[n+b]==m->class1_glyphs[g]&&!visited[n+b]){visited[n+b]=1;++writes;}
            }
            for(;n<count;++n)if(map[n]==m->class1_glyphs[g]&&!visited[n]){visited[n]=1;++writes;}
        }
    }else for(;n<count;++n)if(m->glyphs[map[n]].class_id==1u&&!visited[n]){visited[n]=1;++writes;}
    return writes;
}
void nds_map_begin(void)
{
    if(!(g_nds_map_probe[2]&1u))++g_nds_map_probe[2];
    DC_FlushRange((void *)g_nds_map_probe,32);
}
void nds_map_load(NdsMapRuntime *m,dok_render_world *w,NdsGameplay *g,uint32_t level)
{
    uint32_t pauses=m->pauses,resumes=m->resumes;
    unsigned char *planes=m->planes,*post=m->post_visited;
    memset(m,0,sizeof(*m));m->planes=planes;m->post_visited=post;m->pauses=pauses;m->resumes=resumes;m->level=level;
    dok_map_control_init(&m->control,(uint16_t)w->effective.settings[DOK_LEVEL_WIDTH],
        (uint16_t)w->effective.settings[DOK_LEVEL_LENGTH],(int16_t)(g->hero.x/480),(int16_t)(g->hero.z/480));
    for(uint32_t i=0;i<w->effective.object_count;++i)if(w->effective.objects[i].class_id==1u){
        if(m->class1_count<4u)m->class1_glyphs[m->class1_count]=w->effective.objects[i].glyph;
        ++m->class1_count;
    }
    m->force=1;
}
void nds_map_invalidate(NdsMapRuntime *m){m->force=1;m->input_pending=0;}
uint32_t nds_map_input(NdsMapRuntime *m,dok_world_clock *clock,uint32_t keys,
    uint32_t held,int16_t x,int16_t y,NdsGameplay *g)
{
    if((keys&KEY_START)&&!m->start_held){
        dok_status s=m->paused?dok_world_clock_resume(clock):dok_world_clock_pause(clock);
        if(s!=DOK_STATUS_OK)m->fault=1;
        m->paused=!m->paused;
        if(m->paused)++m->pauses;else ++m->resumes;
        m->absorb=1;g->input_held=0;
        if(dok_controls_initialize(&g->controls)!=DOK_STATUS_OK)m->fault=2;
    }
    m->start_held=(keys&KEY_START)!=0;
    m->touch_held=held;m->touch_x=(uint32_t)(int32_t)x;m->touch_y=(uint32_t)(int32_t)y;
    uint32_t touch_changed=held!=m->control.held|| (held&&(x!=m->control.last_x||y!=m->control.last_y));
    dok_map_control old=m->control;
    dok_map_control_step(&m->control,(dok_map_touch){x,y,(uint8_t)held,(uint8_t)m->paused},
        (int16_t)(g->hero.x/480),(int16_t)(g->hero.z/480));
    /* Gameplay follow is throttled. A stylus-induced viewport or visible
     * button change is presented on this very presentation. */
    if((held&&touch_changed&&(old.center_x_q16!=m->control.center_x_q16||old.center_z_q16!=m->control.center_z_q16))||
       old.zoom!=m->control.zoom||
       (old.capture>=2u?old.capture:0u)!=(m->control.capture>=2u?m->control.capture:0u)){
        m->force=2;m->input_ticks=cpuGetTiming();m->input_pending=1;
    }
    if(m->absorb&&!keys)m->absorb=0;
    return m->paused||m->absorb?0u:keys&~KEY_START;
}
static void text(unsigned char *out,const unsigned char *font,const unsigned char *s,uint32_t n,int x,int y,uint8_t color)
{
    for(uint32_t c=0;c<n;++c)for(uint32_t r=0;r<8u;++r)for(uint32_t b=0;b<8u;++b)
        if(font[s[c]*8u+r]&(128u>>b))out[(y+(int)r)*256+x+(int)c*8+(int)b]=color;
}
static void buttons(NdsMapRuntime *m,unsigned char *out,const unsigned char *font,uint32_t language)
{
    for(uint32_t button=0;button<3u;++button){
        uint32_t x=button==0u?0u:button==1u?85u:170u,w=button==2u?86u:85u;
        uint8_t fill=m->control.capture==button+2u?2u:86u;
        for(uint32_t y=160;y<192u;++y)memset(out+y*256u+x,fill,w);
        for(uint32_t y=160;y<192u;++y)out[y*256u+x]=0;
        memset(out+160u*256u+x,0,w);
        unsigned char minus[1]={'-'},plus[1]={'+'};
        static const unsigned char ru[]={0x96,0xa5,0xad,0xe2,0xe0};
        static const unsigned char en[]={'C','e','n','t','e','r'};
        const unsigned char *s=button==0u?minus:button==1u?plus:language==1u?ru:en;
        uint32_t n=button<2u?1u:language==1u?5u:6u;
        text(out,font,s,n,(int)(x+(w-n*8u)/2u),172,fill==2u?86u:2u);
    }
}
void nds_map_prepare(NdsMapRuntime *m,dok_render_world *w,NdsGameplay *g,
    const unsigned char *font,uint32_t language,unsigned char *pixels,
    uint32_t ticks,uint32_t level,uint32_t serial,uint32_t frame,uint32_t ordinal,uint32_t case_id,
    const dok_nds_render_camera *camera,uint32_t heading)
{
    uint32_t profile_start=cpuGetTiming(),phase=profile_start;
    uint32_t count=m->control.width*m->control.length;
    uint8_t monsters[256]={0};
    int32_t left,top;
    uint32_t changed=!m->updates,reason=0;
    for(uint32_t i=0;i<g->ai.count;++i)if(g->ai.enemies[i].corpse)monsters[g->ai.enemies[i].glyph]=4;
    int16_t hero_x=(int16_t)(g->hero.x/480),hero_z=(int16_t)(g->hero.z/480);
    if(m->control.follow&&!m->paused)dok_map_control_step(&m->control,
        (dok_map_touch){(int16_t)m->touch_x,(int16_t)m->touch_y,(uint8_t)m->touch_held,0},hero_x,hero_z);
    dok_map_control_view(&m->control,&left,&top);
    if(m->snapshot.hero_x!=hero_x||m->snapshot.hero_z!=hero_z||m->snapshot.heading!=(uint16_t)g->hero.heading||
       m->snapshot.map_available!=(uint8_t)g->items.inventory.map_available||m->left!=left||m->top!=top)changed=1;
    /* A cheap known difference already establishes the dirty OR. Avoid
     * rescanning the full planes while aggregating the same pending movement. */
    for(uint32_t i=0;i<w->effective.object_count&&!changed;++i){const dok_level_object *o=&w->effective.objects[i];
        const dok_map_glyph *p=&m->glyphs[o->glyph];uint8_t marker=o->marker;
        if(o->class_id==7u&&g->interactions.doors[o->glyph].ready)marker=g->interactions.doors[o->glyph].marker;
        if(p->class_id!=o->class_id||p->marker!=marker||p->monster_state!=monsters[o->glyph])changed=1;
    }
    /* Exact comparisons avoid hash collisions. Only active cells affect the
     * source raster; full fixed-stride planes are copied on a real redraw. */
    for(uint32_t n=0;n<3u&&!changed;++n)if(bytes_differ(m->planes+n*DOK_LEVEL_MAX_CELLS,w->initial.maps[n],count))changed=1;
    /* FCMap used PRE: class1 POST writes leave one floor-fill redraw debt. */
    if(!changed&&bytes_differ(m->visited_snapshot,w->initial.maps[3],count))changed=1;
    if(m->force)reason|=m->force==2u?2u:1u;
    if(m->language!=language)reason|=4u;
    if(changed&&(uint32_t)(profile_start-m->last_draw_ticks)>=BUS_CLOCK/12u)reason|=8u;
    if(changed&&!reason)++m->deferred;
    m->profile[0]=cpuGetTiming()-phase;
    for(uint32_t i=1;i<8u;++i)m->profile[i]=0;
    m->profile[10]=UINT32_MAX;m->profile[11]=UINT32_MAX;
    if(reason){
        nds_map_begin();
        phase=cpuGetTiming();
        for(uint32_t i=0;i<w->effective.object_count;++i){const dok_level_object *o=&w->effective.objects[i];
            dok_map_glyph *p=&m->glyphs[o->glyph];p->class_id=o->class_id;p->marker=o->marker;p->monster_state=monsters[o->glyph];
            if(o->class_id==7u&&g->interactions.doors[o->glyph].ready)p->marker=g->interactions.doors[o->glyph].marker;
        }
        memcpy(m->planes,w->initial.maps,3u*DOK_LEVEL_MAX_CELLS);
        memcpy(m->visited_snapshot,w->initial.maps[3],sizeof(m->visited_snapshot));
        m->snapshot=(dok_map_snapshot){{m->planes,m->planes+DOK_LEVEL_MAX_CELLS,m->planes+2u*DOK_LEVEL_MAX_CELLS},
            m->visited_snapshot,m->glyphs,m->control.width,m->control.length,hero_x,hero_z,
            (uint16_t)g->hero.heading,(uint8_t)g->items.inventory.map_available};
        m->left=left;m->top=top;
        m->profile[0]+=cpuGetTiming()-phase;phase=cpuGetTiming();
        if(!dok_map_raster(pixels,&m->snapshot,dok_map_scales[m->control.zoom],-left,-top))m->fault=3;
        m->profile[1]=cpuGetTiming()-phase;
    }
    phase=cpuGetTiming();
    /* Source automap class1 writes belong to its logical update, rather than
     * DMA. Keep the identical second-pass side effect on EVERY world frame,
     * including frames whose last displayed PRE/POST snapshots remain cached.
     * Class IDs are immutable level definitions; a first frame always draws. */
    m->hero_visited+=class1_visit(m,w,count);
    m->profile[2]=cpuGetTiming()-phase;
    if(reason){
        phase=cpuGetTiming();
        memcpy(m->post_visited,w->initial.maps[3],DOK_LEVEL_MAX_CELLS);
        buttons(m,pixels,font,language);
        m->profile[3]=cpuGetTiming()-phase;phase=cpuGetTiming();
        /* Clean the RAM source before the wait; commit still performs the
         * actual VRAM transfer. No second framebuffer is allocated. */
        DC_FlushRange(pixels,DOK_MAP_SCREEN_BYTES);
        for(uint32_t i=0;i<256u;++i)m->palette[i]=BG_PALETTE_SUB[i];
        m->profile[0]+=cpuGetTiming()-phase;phase=cpuGetTiming();
        m->language=language;
        uint32_t p[64]={0x20504b44u,2,g_nds_map_probe[2],level,w->initial.epoch,frame,ordinal,case_id,
            m->control.width,m->control.length,dok_map_scales[m->control.zoom],m->control.follow,
            (uint32_t)m->control.center_x_q16,(uint32_t)m->control.center_z_q16,m->control.capture,m->touch_held,m->touch_x,m->touch_y,m->paused,ticks,
            (uint32_t)g->hero.x,(uint32_t)g->hero.z,(uint32_t)(int32_t)g->hero.heading,
            (uint32_t)(uintptr_t)m->visited_snapshot,(uint32_t)(uintptr_t)(m->planes+DOK_LEVEL_MAX_CELLS),(uint32_t)(uintptr_t)m->planes,(uint32_t)(uintptr_t)(m->planes+2u*DOK_LEVEL_MAX_CELLS),
            (uint32_t)(uintptr_t)m->glyphs,(uint32_t)(uintptr_t)pixels,(uint32_t)(uintptr_t)m->palette,g->items.inventory.map_available,m->fault,
            m->upload_bytes,m->updates,0,m->discovery_records,m->input_ticks,m->latency_max,g->ai.callbacks,g->combat.callbacks,g->hero_callbacks,serial,m->verified,
            (uint32_t)camera->x,(uint32_t)camera->z,heading,m->pauses,m->resumes,m->control.starts,m->control.actions,
            (uint32_t)left,(uint32_t)top,m->control.drags,m->discovery_marked,m->control.blocked,
            (uint32_t)(uintptr_t)m->post_visited,m->hero_visited,(uint32_t)(uintptr_t)w->initial.maps[3],
            (uint32_t)m->control.follow_x_q16,(uint32_t)m->control.follow_z_q16,
            (uint32_t)(uintptr_t)m->profile,(uint32_t)(uintptr_t)m->live,frame,reason};
        memcpy(m->pending_probe,p,sizeof(p));
        m->profile[6]=cpuGetTiming()-phase;
    }
    phase=cpuGetTiming();
    uint32_t live[48]={frame,ticks,level,w->initial.epoch,ordinal,case_id,
        (uint32_t)g->hero.x,(uint32_t)g->hero.z,(uint32_t)(int32_t)g->hero.heading,
        dok_map_scales[m->control.zoom],m->control.follow,(uint32_t)m->control.center_x_q16,(uint32_t)m->control.center_z_q16,
        m->control.capture,m->touch_held,m->touch_x,m->touch_y,m->paused,g->ai.callbacks,g->combat.callbacks,g->hero_callbacks,serial,
        (uint32_t)camera->x,(uint32_t)camera->z,heading,m->pauses,m->resumes,m->control.starts,m->control.actions,m->control.drags,m->control.blocked,
        m->discovery_records,m->discovery_marked,m->hero_visited,(uint32_t)(uintptr_t)w->initial.maps[3],(uint32_t)(uintptr_t)w->initial.maps[0],
        (uint32_t)m->control.follow_x_q16,(uint32_t)m->control.follow_z_q16,g->items.inventory.map_available,m->input_ticks,m->latency_max,
        m->updates,m->upload_bytes,m->verified,m->fault,reason,m->skips,m->deferred};
    if(!reason)nds_map_begin();
    memcpy(m->live,live,sizeof(live));
    m->prepared=1;m->pending_reason=reason;m->pending_ticks=profile_start;
    m->profile[6]+=cpuGetTiming()-phase;
    m->prepare_ticks=cpuGetTiming()-profile_start;m->profile[7]=m->prepare_ticks;
}
void nds_map_transfer(NdsMapRuntime *m,unsigned char *pixels,void *vram)
{
    if(m->prepared!=1u)return;
    uint32_t start=cpuGetTiming(),phase=start;
    if(m->pending_reason){
        m->profile[10]=REG_VCOUNT;
        dmaCopy(pixels,vram,DOK_MAP_SCREEN_BYTES);
        m->profile[11]=REG_VCOUNT;
        m->profile[4]=cpuGetTiming()-phase;
    }
    m->prepared=2u;m->profile[7]+=cpuGetTiming()-start;
}
void nds_map_commit(NdsMapRuntime *m,unsigned char *pixels,void *vram)
{
    if(!m->prepared)return;
    if(m->prepared==1u)nds_map_transfer(m,pixels,vram);
    uint32_t start=cpuGetTiming(),phase=start;
    if(m->pending_reason){
#if N21P_AUDIT
        phase=cpuGetTiming();
        if(memcmp(pixels,vram,DOK_MAP_SCREEN_BYTES))m->fault=4;else ++m->verified;
        m->profile[5]=cpuGetTiming()-phase;
#endif
        phase=cpuGetTiming();
        DC_FlushRange(m->planes,3u*DOK_LEVEL_MAX_CELLS);DC_FlushRange(m->post_visited,DOK_LEVEL_MAX_CELLS);
        DC_FlushRange(m->visited_snapshot,sizeof(m->visited_snapshot));DC_FlushRange(m->glyphs,sizeof(m->glyphs));
        DC_FlushRange(m->palette,sizeof(m->palette));
        uint32_t count=m->pending_probe[8]*m->pending_probe[9],visited=0;
        for(uint32_t n=0;n<count;++n)visited+=m->visited_snapshot[n]!=0;
        m->pending_probe[34]=visited;
        m->upload_bytes+=DOK_MAP_SCREEN_BYTES;++m->updates;
        m->last_draw_ticks=m->pending_ticks;m->last_draw_frame=m->pending_probe[62];
        m->last_draw_reason=m->pending_reason;m->force=0;
        if(m->input_pending){uint32_t latency=cpuGetTiming()-m->input_ticks;if(latency>m->latency_max)m->latency_max=latency;m->input_pending=0;}
        m->pending_probe[2]=g_nds_map_probe[2];m->pending_probe[31]=m->fault;
        m->pending_probe[32]=m->upload_bytes;m->pending_probe[33]=m->updates;
        m->pending_probe[37]=m->latency_max;m->pending_probe[42]=m->verified;
        memcpy((void *)g_nds_map_probe,m->pending_probe,sizeof(m->pending_probe));
    }else {++m->skips;phase=cpuGetTiming();}
    m->live[40]=m->latency_max;m->live[41]=m->updates;m->live[42]=m->upload_bytes;
    m->live[43]=m->verified;m->live[44]=m->fault;m->live[46]=m->skips;
    DC_FlushRange(m->live,sizeof(m->live));
    /* Script time advances even when scanout reuses the cached image. Every
     * other display field describes the last actual raster/DMA snapshot. */
    g_nds_map_probe[5]=m->live[0];g_nds_map_probe[31]=m->fault;
    DC_FlushRange((void *)g_nds_map_probe,sizeof(g_nds_map_probe));
    ++g_nds_map_probe[2];DC_FlushRange((void *)g_nds_map_probe,32);
    m->prepared=0;m->profile[6]+=cpuGetTiming()-phase;
    m->profile[7]+=cpuGetTiming()-start;
    DC_FlushRange(m->profile,8u*sizeof(uint32_t));
}
void nds_map_update(NdsMapRuntime *m,dok_render_world *w,NdsGameplay *g,
    const unsigned char *font,uint32_t language,unsigned char *pixels,void *vram,
    uint32_t ticks,uint32_t level,uint32_t serial,uint32_t frame,uint32_t ordinal,uint32_t case_id,
    const dok_nds_render_camera *camera,uint32_t heading)
{
    nds_map_prepare(m,w,g,font,language,pixels,ticks,level,serial,frame,ordinal,case_id,camera,heading);
    nds_map_commit(m,pixels,vram);
}
