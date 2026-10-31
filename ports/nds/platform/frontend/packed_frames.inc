/* Included by the production shell. The build-time codec already checks every
 * decoded pixel; runtime checks the immutable packed file against its build SHA. */
#include "frames_hash.inc"
static int frontend_frames_load(unsigned char *pool,uint32_t capacity)
{
    FILE *f=fopen(NDS_RESOURCE_ROOT "frames.dat","rb");
    uint32_t h[8];dok_sha256 hash;unsigned char digest[32];
    if(!f)return 0;
    dok_sha256_begin(&hash);
    int ok=fread(h,1,32,f)==32&&h[0]==0x31465044u&&h[1]==1u&&h[2]<=DOK_TEXTURE_CACHE_SLOTS&&h[3]<=capacity&&!h[5]&&!h[6]&&!h[7];
    if(ok){
        dok_frame_store *s=&storage->frames;
        memset(s,0,sizeof(*s));s->pool=pool;s->count=h[2];s->bytes=h[3];s->decoded_bytes=h[4];
        dok_sha256_update(&hash,h,32);
        uint32_t bytes=s->count*sizeof(s->records[0]);
        ok=fread(s->records,1,bytes,f)==bytes;
        if(ok)dok_sha256_update(&hash,s->records,bytes);
        if(ok)ok=fread(pool,1,s->bytes,f)==s->bytes;
        if(ok)dok_sha256_update(&hash,pool,s->bytes);
        if(ok)ok=fgetc(f)==EOF&&!ferror(f);
        dok_sha256_end(&hash,digest);if(ok)ok=!memcmp(digest,frames_sha,32);
        for(uint32_t i=0;ok&&i<storage->bank.image_count;++i){
            const dok_asset_entry *e=&storage->bank.images[i];uint32_t j;
            for(j=0;j<s->count;++j)if(s->records[j].id==e->id)break;
            if(j==s->count||s->records[j].decoded_bytes!=e->bytes)ok=0;
        }
    }
    fclose(f);return ok;
}
