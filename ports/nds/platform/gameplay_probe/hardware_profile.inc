/* Sample existing ARM9 timers. Each pause shows the completed gameplay segment. */
enum {
    HW_WORK, HW_SIM, HW_GEOMETRY, HW_SPRITES, HW_OVERLAY, HW_EFFECTS,
    HW_SUBMIT, HW_GE_WAIT, HW_TEXTURE, HW_MAP,
#if N23_PIPELINE_PROFILE
    HW_DISCOVERY,
#endif
    HW_STAGES
};
typedef struct {
    uint64_t ticks[HW_STAGES];
    uint32_t frames,vblanks,bulk_frames,map_waits;
#if N23_PIPELINE_PROFILE
    uint64_t quads;
    uint32_t peak_work,peak_vblanks;
#else
    uint64_t upload_bytes;
    uint32_t uploads;
#endif
} HardwareProfileGroup;
static HardwareProfileGroup hardware_profile[2];

static void hardware_profile_sample(const NdsRenderHistoryRecord *row,const uint32_t *profile)
{
    uint32_t blanks=row->vblank_end-row->vblank_start;
    if(!blanks)return;
    HardwareProfileGroup *group=&hardware_profile[blanks>1u];
#if N23_PIPELINE_PROFILE
    group->quads+=row->submitted_quads;
    if(profile[31]>group->peak_work)group->peak_work=profile[31];
    if(blanks>group->peak_vblanks)group->peak_vblanks=blanks;
#else
    group->upload_bytes+=row->upload_bytes;
    group->uploads+=row->upload_bytes!=0u;
#endif
    const uint32_t ticks[HW_STAGES]={profile[31],profile[18],profile[19],profile[20],
        profile[22],profile[23],row->submit_ticks,row->geometry_wait_ticks,row->io_upload_ticks,profile[28],
#if N23_PIPELINE_PROFILE
        profile[21],
#endif
    };
    for(unsigned stage=0;stage<HW_STAGES;++stage)group->ticks[stage]+=ticks[stage];
    group->bulk_frames+=(row->flags&NDS_RENDER_FRAME_LOADING)!=0u;
    group->map_waits+=profile[46]!=0u;
    ++group->frames;
    group->vblanks+=blanks;
}

static void hardware_profile_line(const char *line,unsigned y)
{
    for(unsigned i=0;line[i]&&i<30u;++i){
        const unsigned char *glyph=ui->bank+(unsigned char)line[i]*8u;
        for(unsigned gy=0;gy<8u;++gy)for(unsigned x=0;x<8u;++x)
            if(glyph[gy]&(128u>>x))frontend->bottom[(y+gy)*256u+8u+i*8u+x]=2;
    }
}

static unsigned long hardware_profile_ms10(const HardwareProfileGroup *group,unsigned stage)
{
    if(!group->frames)return 0;
    uint64_t per_frame=group->ticks[stage]/group->frames;
    return (unsigned long)((per_frame*10000u+BUS_CLOCK/2u)/BUS_CLOCK);
}

#if N23_PIPELINE_PROFILE
static void hardware_profile_workload(void)
{
    memset(frontend->bottom,0,49152);
    char line[48];
    const HardwareProfileGroup *a=&hardware_profile[0],*b=&hardware_profile[1];
    unsigned long fps=(a->vblanks||b->vblanks)?(unsigned long)(
        ((uint64_t)a->frames+b->frames)*BUS_CLOCK*10u/
        (((uint64_t)a->vblanks+b->vblanks)*560190u)):0;
    snprintf(line,sizeof(line),"DS room %lu.%lu FPS",fps/10u,fps%10u);
    hardware_profile_line(line,4);
    hardware_profile_line("VBlank/frame         1      2+",17);
    snprintf(line,sizeof(line),"Frames          %6lu  %6lu",(unsigned long)a->frames,(unsigned long)b->frames);
    hardware_profile_line(line,29);
    unsigned long peak_a=(unsigned long)(((uint64_t)a->peak_work*10000u+BUS_CLOCK/2u)/BUS_CLOCK);
    unsigned long peak_b=(unsigned long)(((uint64_t)b->peak_work*10000u+BUS_CLOCK/2u)/BUS_CLOCK);
    snprintf(line,sizeof(line),"Peak work     %6lu.%lu%6lu.%lu",peak_a/10u,peak_a%10u,peak_b/10u,peak_b%10u);
    hardware_profile_line(line,40);
    snprintf(line,sizeof(line),"Mean quads      %6lu  %6lu",a->frames?(unsigned long)(a->quads/a->frames):0ul,
        b->frames?(unsigned long)(b->quads/b->frames):0ul);
    hardware_profile_line(line,51);
    static const struct { const char *name; unsigned stage; } rows[]={
        {"Work",HW_WORK},{"Simulation",HW_SIM},{"Geometry",HW_GEOMETRY},{"Sprites",HW_SPRITES},
        {"  Discovery",HW_DISCOVERY},{"Effects",HW_EFFECTS},{"Submit",HW_SUBMIT},{"Map",HW_MAP}
    };
    for(unsigned i=0;i<sizeof(rows)/sizeof(rows[0]);++i){
        unsigned long x=hardware_profile_ms10(a,rows[i].stage),y=hardware_profile_ms10(b,rows[i].stage);
        snprintf(line,sizeof(line),"%-14s%6lu.%lu%6lu.%lu",rows[i].name,x/10u,x%10u,y/10u,y%10u);
        hardware_profile_line(line,62u+11u*i);
    }
    snprintf(line,sizeof(line),"Bulk frames     %6lu  %6lu",(unsigned long)a->bulk_frames,(unsigned long)b->bulk_frames);
    hardware_profile_line(line,149);
    snprintf(line,sizeof(line),"Map waits       %6lu  %6lu",(unsigned long)a->map_waits,(unsigned long)b->map_waits);
    hardware_profile_line(line,159);
    snprintf(line,sizeof(line),"Max VBlank      %6lu  %6lu",(unsigned long)a->peak_vblanks,(unsigned long)b->peak_vblanks);
    hardware_profile_line(line,173);
}
#endif

static void hardware_profile_draw(void)
{
#if N23_PIPELINE_PROFILE
    hardware_profile_workload();
#else
    static const char *const names[HW_STAGES]={"Work", "Simulation", "Geometry", "Sprites",
        "Overlay", "Effects", "Submit", "GE fence", "Texture", "Map"};
    const HardwareProfileGroup *one=&hardware_profile[0],*late=&hardware_profile[1];
    uint64_t blanks=(uint64_t)one->vblanks+late->vblanks;
    uint64_t frames=(uint64_t)one->frames+late->frames;
    unsigned long fps=blanks?(unsigned long)(frames*BUS_CLOCK*10u/(blanks*560190u)):0;
    char line[48];
    memset(frontend->bottom,0,49152);
    snprintf(line,sizeof(line),"DS profile %lu.%lu FPS",fps/10u,fps%10u);
    hardware_profile_line(line,4);
    hardware_profile_line("VBlank         1        2+",16);
    snprintf(line,sizeof(line),"Frames      %6lu    %6lu",(unsigned long)one->frames,(unsigned long)late->frames);
    hardware_profile_line(line,28);
    for(unsigned stage=0;stage<HW_STAGES;++stage){
        unsigned long a=hardware_profile_ms10(one,stage),b=hardware_profile_ms10(late,stage);
        snprintf(line,sizeof(line),"%-11s%5lu.%lu  %6lu.%lu",names[stage],a/10u,a%10u,b/10u,b%10u);
        hardware_profile_line(line,40u+stage*11u);
    }
    snprintf(line,sizeof(line),"Tex frames  %6lu    %6lu",(unsigned long)one->uploads,(unsigned long)late->uploads);
    hardware_profile_line(line,150);
    unsigned long a=one->uploads?(unsigned long)(one->upload_bytes/one->uploads/1024u):0;
    unsigned long b=late->uploads?(unsigned long)(late->upload_bytes/late->uploads/1024u):0;
    snprintf(line,sizeof(line),"Tex KiB     %6lu    %6lu",a,b);
    hardware_profile_line(line,160);
    snprintf(line,sizeof(line),"Bulk frames %6lu    %6lu",(unsigned long)one->bulk_frames,(unsigned long)late->bulk_frames);
    hardware_profile_line(line,170);
    snprintf(line,sizeof(line),"Map waits   %6lu    %6lu",(unsigned long)one->map_waits,(unsigned long)late->map_waits);
    hardware_profile_line(line,180);
#endif
    frontend->palettes[1][0]=BG_PALETTE_SUB[0]=0;
    frontend->palettes[1][2]=BG_PALETTE_SUB[2]=RGB15(31,31,31);
    DC_FlushRange(frontend->palettes[1],512);
    DC_FlushRange(frontend->bottom,49152);
    dmaCopy(frontend->bottom,bgGetGfxPtr(map_bg),49152);
}
