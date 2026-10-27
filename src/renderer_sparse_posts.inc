/* G20C: map source intervals to the identical clipped Q16 sample sequence.
 * No division/rounded geometric endpoints: lower_bound over <=144 rows also
 * handles minification, repeated texels and step0. Metadata is post-LIGHT.
 * ROM orchestration preserves the small IWRAM budget and calls the established
 * native tiled sampler for each nonzero run, with black alias disabled. */
static DOK_COLD unsigned sparse_bound(unsigned v,unsigned step,unsigned count,unsigned source) {
    unsigned lo=0,hi=count,threshold=source<<16;
    while(lo<hi) {
        unsigned mid=(lo+hi)>>1;
        if(v+mid*step<threshold)lo=mid+1;else hi=mid;
    }
    return lo;
}
static DOK_COLD unsigned sparse_column(dok_renderer_workspace *w,unsigned x,unsigned begin,unsigned end,
    const uint8_t *pixels,unsigned v,unsigned step,const uint16_t *posts) {
    unsigned available=hw_empty_column_count(x,begin,end),written=0,count=end-begin+1;
    if(!available)return 0;
    unsigned first=v>>16,last=(v+(count-1)*step)>>16;
    unsigned runs=*posts++;
    for(unsigned i=0;i<runs;++i,posts+=2) {
        unsigned lo=posts[0],hi=posts[1];
        if(hi<=first)continue;
        if(lo>last)break;
        unsigned a=lo<=first?0:sparse_bound(v,step,count,lo);
        unsigned b=hi>last?count:sparse_bound(v,step,count,hi);
        if(a==b)continue;
        unsigned y=begin+a;
        extern unsigned dok_gba_tiled_opaque_gaps(uint8_t *,const uint8_t *,unsigned,unsigned,unsigned,unsigned,unsigned,uint32_t *,unsigned);
        written+=dok_gba_tiled_opaque_gaps(scene_pointer(w->frame,x,y),pixels,v+a*step,step,
            b-a,y&7,0,&coverage_bits[x>>1][y>>5],y&31);
    }
    return written|((available-written)<<16);
}
