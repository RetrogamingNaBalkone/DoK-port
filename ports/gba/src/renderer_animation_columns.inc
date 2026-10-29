/* Immutable cartridge frames only. A package-relative pixel offset and exact
 * dimensions identify the lossless column copy; all other images fall back. */
extern const uint8_t dok_animation_columns[];
extern const uint8_t dok_animation_columns_end[];
extern const uint32_t dok_animation_columns_index[];
static const uint8_t *animation_lit_columns(const uint8_t *columns,unsigned row) {
    uintptr_t address=(uintptr_t)columns,base=(uintptr_t)dok_animation_columns;
    if(address<base+448 || address>=(uintptr_t)dok_animation_columns_end)return 0;
    const uint32_t *offsets=(const uint32_t *)(const void *)(columns-448);
    unsigned offset=offsets[(static_view/2)*16+row];
    return offset?dok_animation_columns+offset:0;
}
static DOK_COLD const uint8_t *animation_columns(const dok_rom_assets *a,const dok_image_view *im) {
    uintptr_t address=(uintptr_t)im->pixels,base=(uintptr_t)a->data;
    if(address<base || address-base>=a->size)return 0;
    uint32_t offset=(uint32_t)(address-base),lo=0,hi=dok_animation_columns_index[0];
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        const uint32_t *row=dok_animation_columns_index+1+mid*3;
        if(row[0]==offset)return row[1]==((unsigned)im->width|((unsigned)im->height<<16))?
            dok_animation_columns+row[2]:0;
        if(row[0]<offset)lo=mid+1;else hi=mid;
    }
    return 0;
}
