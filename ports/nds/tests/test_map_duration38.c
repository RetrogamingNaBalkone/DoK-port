#define main baseline_map_main
#include "test_map_runtime.c"
#undef main
int main(void)
{
    CHECK(baseline_map_main()==0);
    /* Actual production transfer snapshots remain coherent across uint32 wrap. */
    clock_ticks=UINT32_MAX;map.prepared=1;map.pending_reason=1;
    map.pending_upload_bytes=sizeof(pixels);
    nds_map_transfer(&map,pixels,vram);
    CHECK(map.profile[12]==UINT32_MAX&&map.profile[13]==0);
    CHECK(map.profile[4]==1&&map.prepared==2);
    CHECK(!memcmp(pixels,vram,sizeof(pixels)));
    puts("map38 uint32-wrap PASS");
    return 0;
}
