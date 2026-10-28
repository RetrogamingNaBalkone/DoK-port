/* An entirely zero post-LIGHT source column cannot change a masked image.
 * Count only uncovered destination rows to preserve the public statistics. */
static DOK_COLD unsigned hw_empty_column_count(unsigned x,unsigned first,unsigned last) {
    unsigned count=0;
    for(unsigned word=first>>5;word<=last>>5;++word) {
        uint32_t mask=UINT32_MAX;
        if(word==(first>>5))mask&=UINT32_MAX<<(first&31);
        if(word==(last>>5))mask&=UINT32_MAX>>(31-(last&31));
        uint32_t bits=~coverage_bits[x>>1][word]&mask;
        bits-=((bits>>1)&0x55555555u);
        bits=(bits&0x33333333u)+((bits>>2)&0x33333333u);
        bits=(bits+(bits>>4))&0x0f0f0f0fu;
        count+=(bits*0x01010101u)>>24;
    }
    return count;
}
