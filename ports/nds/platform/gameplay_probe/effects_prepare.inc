typedef struct MaskIdentity {uint32_t id;unsigned char row[16];} MaskIdentity;
static MaskIdentity mask_identities[256];
static int mask_identity(const dok_nds_render_texture *t,const unsigned char *light,unsigned char out[16])
{
    uint32_t i,j,k;unsigned char used[256]={0},masks[16][32]={{0}};
    for(i=0;i<mask_identity_count;++i)if(mask_identities[i].id==t->asset_id){memcpy(out,mask_identities[i].row,16);return 1;}
    if(mask_identity_count==256u)return 0;
    for(i=0;i<t->logical_height;++i)for(j=0;j<t->logical_width;++j)used[t->pixels[i*t->storage_width+j]]=1;
    used[0]=0;
    for(i=0;i<16u;++i) {
        uint32_t nonzero=0;
        for(j=1;j<256u;++j)if(used[j] && !light[i*256u+j]){masks[i][j/8u]|=1u<<(j%8u);nonzero=1;}
        out[i]=0;
        if(nonzero){for(k=0;k<i;++k)if(!memcmp(masks[k],masks[i],32))break;out[i]=(unsigned char)(k+1u);}
    }
    mask_identities[mask_identity_count].id=t->asset_id;memcpy(mask_identities[mask_identity_count++].row,out,16);return 1;
}
/* All raw sources stay pinned while generated variants compact. */
static int effects_prepare(const dok_render_case *c,uint32_t ordinal)
{
    static dok_nds_stream_texture raw[DOK_NDS_RENDER_TEXTURE_CAPACITY];
    unsigned char mask_rows[DOK_NDS_RENDER_TEXTURE_CAPACITY][16];
    NdsEffectsRuntime *r=&storage->effects;
    uint32_t i,j,old_count=demand_count;uint32_t pct=99;
    int16_t near_depth=c->source_kind==17u?-2000:1920;
#if EFFECT_GPU_FOG
    /* Keep exact two controlled alpha/depth fixtures as reference. Flat
     * diagnostic colors also remain independent of artistic shading. */
    uint32_t gpu_darkness=c->source_kind!=13u && c->source_kind!=17u && !(c->flags&DOK_RENDER_CASE_FLAT);
    dok_nds_render_darkness((int)gpu_darkness);
    if(gpu_darkness || (c->flags&DOK_RENDER_CASE_FLAT)) {
        memset(&light_counts,0,sizeof(light_counts));
        light_counts.source_quads=light_counts.published_quads=scene->counts.published_quads;
        /* Darkness changes RGB after rasterization. Keep the source index0
         * silhouette, without LIGHT-induced holes or face-centre mask pops.
         * Distant objects disappear fully at the black fog endpoint. */
        for(i=0;i<scene->counts.published_quads;++i)scene->quads[i].reserved=0;
        light_counts.rows[0]=scene->counts.published_quads;
        g_nds_effect_probe[30]=DOK_RENDER_SCENE_OK;
    } else g_nds_effect_probe[30]=dok_light_scene_apply(scene,near_depth,(dok_light_workspace *)(void *)scratch,&light_counts);
#elif EFFECT_LIGHT_CACHE
    g_nds_effect_probe[30]=dok_light_cache_apply(&geometry_cache,scene,near_depth,(dok_light_workspace *)(void *)scratch,&light_counts);
#else
    g_nds_effect_probe[30]=dok_light_scene_apply(scene,near_depth,(dok_light_workspace *)(void *)scratch,&light_counts);
#endif
    if(g_nds_effect_probe[30]!=DOK_RENDER_SCENE_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    if(c->source_kind==18u) {
        uint32_t k=ordinal%68u;pct=k<34u?k*3u:100u-(k-34u)*3u;
    }
    if(pct!=r->percentage) {
        if(dok_light_palette(r->rgb6,r->light,pct,r->palette)!=DOK_STATUS_OK ||
           dok_nds_stream_palette_dirty()!=DOK_NDS_RENDER_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        r->percentage=pct;
    }
    memcpy(raw,demand,old_count*sizeof(*raw));demand_count=0;
    for(i=0;i<old_count;++i)if(raw[i].texture.color0_transparent) {
#if EFFECT_GPU_FOG
        if(gpu_darkness || (c->flags&DOK_RENDER_CASE_FLAT)){memset(mask_rows[i],0,16);continue;}
#endif
        if(!mask_identity(&raw[i].texture,r->light,mask_rows[i]))return 0;
    }
    if(dok_effect_cache_begin(&r->cache)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    for(i=0;i<scene->counts.published_quads;++i) {
        dok_nds_render_quad *q=&scene->quads[i];uint32_t slot=q->texture_slot;
        if(slot==UINT16_MAX){q->reserved=0;continue;}
        if(slot>=old_count || q->reserved>15u)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
        const dok_nds_render_texture *t=&raw[slot].texture;
        uint32_t material=raw_material[slot],phase=0,flags=0,code=0,mask=0;
        const unsigned char *row=r->light+q->reserved*256u;
        if(material!=UINT16_MAX && r->material_water[material]) {
            flags=r->material_flags[material];phase=r->material_phase[material];
            if(c->source_kind==14u || c->source_kind==28u)phase=EFFECT_WATER_PHASES==UINT32_MAX?(ordinal+(c->source_kind==28u?c->cell_x:0u))%18u:(EFFECT_WATER_PHASES>>(flags&0x0440u?0:flags&0x0880u?8:flags&0x1100u?16:24))&255u;
            static const uint16_t pairs[4]={0x0440,0x0880,0x1100,0x2200};
            for(j=0;j<4u;++j)if(flags&pairs[j]) {code=j*2u+((flags&(pairs[j]&0xff00u))?2u:1u);break;}
            if(!phase && !(GAMEPLAY_WATER_UV && gpu_darkness && !(code&1u)))code=0;
        }
        if(t->color0_transparent)mask=mask_rows[slot][q->reserved];
        if(mask)row=r->light+(mask-1u)*256u;
        uint64_t key=raw[slot].key|((uint64_t)(code?phase+1u:0)<<33)|((uint64_t)mask<<38)|((uint64_t)code<<43);
        uint32_t tiled=GAMEPLAY_WATER_UV && gpu_darkness && code && !(code&1u) &&
            t->logical_width==96u && t->logical_height==96u && !mask;
        uint32_t axis=code>4u;
        if(tiled){
            key=raw[slot].key|((uint64_t)(axis?2u:1u)<<48);
            uint32_t shift=dok_water_uv_shift(phase,96u,code==2u||code==6u);
            for(uint32_t v=0;v<4u;++v){if(axis)q->vertex[v].v_q16+=(int32_t)(shift*65536u);else q->vertex[v].u_q16+=(int32_t)(shift*65536u);}
        }
        for(j=0;j<demand_count;++j)if(demand[j].key==key)break;
        if(j==demand_count) {
            if(j==DOK_NDS_RENDER_TEXTURE_CAPACITY)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
            demand[j]=raw[slot];demand[j].key=key;++demand_count;
            if(tiled){
                uint32_t present=0;for(uint32_t n=0;n<r->cache.count;++n)if(r->cache.records[n].key==key){present=1;break;}
                unsigned char *extended=(unsigned char *)(void *)scratch;
                if(!present && dok_water_texture(t->pixels,96,96,128,axis,extended,DOK_NDS_SCRATCH_BYTES)!=DOK_STATUS_OK)return 0;
                dok_nds_render_texture *e=&demand[j].texture;
                e->storage_width=axis?128:256;e->storage_height=axis?256:128;
                e->logical_width=axis?96:192;e->logical_height=axis?192:96;e->bytes=32768;
                if(dok_effect_cache_require(&r->cache,key,extended,e->logical_width,e->logical_height,e->storage_width,e->storage_height,0,0,row,0)!=DOK_STATUS_OK)return 0;
            } else if(code || mask) {
                if(dok_effect_cache_require(&r->cache,key,t->pixels,t->logical_width,t->logical_height,
                    t->storage_width,t->storage_height,(uint16_t)(code?flags:0),(uint16_t)phase,row,mask!=0)!=DOK_STATUS_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
            }
        }
        q->texture_slot=(uint16_t)j;
    }
    for(i=0;i<demand_count;++i)if(demand[i].key>>33) {
        demand[i].texture.pixels=dok_effect_cache_pixels(&r->cache,demand[i].key);
        if(!demand[i].texture.pixels)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    }
    if(dok_nds_stream_plan(demand,demand_count,&stream_stats)!=DOK_NDS_RENDER_OK)do{if(!g_nds_effect_probe[31])g_nds_effect_probe[31]=__LINE__;DC_FlushRange((const void *)g_nds_effect_probe,128);return 0;}while(0);
    sprite_obs.demand_count=demand_count;obs.texture_count=demand_count;obs.texture_bytes=stream_stats.resident_bytes;
    uint32_t data[32]={0x13504b44u,1,32u*4u,0,address(r->light),address(r->rgb6),address(r->palette),
        (uint32_t)(int32_t)near_depth,r->percentage,light_counts.source_quads,light_counts.published_quads,
        light_counts.split_quads,light_counts.near_hidden,r->cache.used,r->cache.hits,r->cache.misses,
        r->cache.evictions,r->cache.generated_bytes,r->callbacks,r->water_callbacks,r->animation_callbacks,
        r->fault,EFFECT_REAL_CLOCK,address((const void *)r->traces),sizeof(NdsEffectTrace),r->trace_total,r->schedule_refusals,c->id,obs.frame_id};
#if EFFECT_LIGHT_CACHE
    data[29]=geometry_cache.last_hit;data[30]=geometry_cache.hits;
#endif
#if EFFECT_GPU_FOG
    data[3]=1;data[29]=gpu_darkness;
#endif
    memcpy((void *)g_nds_effect_probe,data,sizeof(data));DC_FlushRange((const void *)g_nds_effect_probe,sizeof(data));
    obs.assets_high_water=DOK_NDS_ASSETS_BYTES;
    return 1;
}
