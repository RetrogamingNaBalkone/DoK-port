#define main loader_main
#include "geometry_clear_equivalence.c"
#undef main
#include "dok/geometry_cache.h"
int main(int argc,char **argv)
{
    uint32_t pass,x,z,hits,misses,bytes,n=0;int result=loader_main(argc,argv);
    if(result)return result;dok_geometry_cache_reset();dok_geometry_cache_begin(&world,bindings);
    for(pass=0;pass<3u;++pass)for(z=0;z<64u;++z)for(x=0;x<64u;++x) {
        dok_geometry_status a,b;
        a=baseline_dok_geometry_build_cell(&world,bindings,x,z,&old_candidate,&old_out);
        b=dok_geometry_build_cell(&world,bindings,x,z,&new_candidate,&new_out);
        if(a!=b || (a==DOK_GEOMETRY_OK && memcmp(&old_out,&new_out,sizeof(old_out))))return 40;++n;
    }
    dok_geometry_cache_stats(&hits,&misses,&bytes);if(!hits || !misses || !bytes)return 41;
    printf("{\"passed\":true,\"cached_calls\":%u,\"hits\":%u,\"misses\":%u,\"cache_bytes\":%u}\n",n,hits,misses,bytes);
    return 0;
}
