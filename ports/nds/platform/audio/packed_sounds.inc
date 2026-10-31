#include "sounds_hash.inc"
#include "dok/sha256.h"
static int load_packed_sounds(unsigned char *banks[2],uint32_t used[2])
{
    FILE *f=fopen(NDS_RESOURCE_ROOT "sounds.dat","rb");if(!f)return 0;
    uint32_t h[4]={0},records[22][4],order[22];dok_sha256 hash;unsigned char digest[32];
    int ok=fread(h,1,16,f)==16&&h[0]==0x31415044u&&h[1]==1u&&h[2]==22u&&h[3]<=327680u&&
        fread(records,1,sizeof(records),f)==sizeof(records);
    dok_sha256_begin(&hash);
    if(ok){dok_sha256_update(&hash,h,16);dok_sha256_update(&hash,records,sizeof(records));}
    uint32_t total=0;
    for(unsigned i=0;ok&&i<22u;++i){
        uint32_t *r=records[i];AudioResident *a=&resident[i];
        if(r[0]!=((SPRITE_LANGUAGE<<30)|(indices[i]<<18)|(DOK_ASSET_PCM_CLIP<<10))||!r[2]||r[2]>65536u||r[3]>32768u){ok=0;break;}
        a->pcm=r[0];a->rate=r[1];a->samples=r[2];a->bytes=r[3];order[i]=i;total+=r[3];
    }
    if(total!=h[3])ok=0;
    if(ok){
        for(unsigned i=0;i<22u;++i)for(unsigned j=i+1u;j<22u;++j)
            if(resident[order[j]].bytes>resident[order[i]].bytes){uint32_t t=order[i];order[i]=order[j];order[j]=t;}
        for(unsigned k=0;ok&&k<22u;++k){AudioResident *a=&resident[order[k]];unsigned b=used[0]<=used[1]?0:1;
            if(used[b]+a->bytes>163840u)b^=1u;
            if(used[b]+a->bytes>163840u){ok=0;break;}
            a->packed=banks[b]+used[b];used[b]+=a->bytes;
        }
        for(unsigned i=0;ok&&i<22u;++i){AudioResident *a=&resident[i];
            ok=fread(a->packed,1,a->bytes,f)==a->bytes;if(ok)dok_sha256_update(&hash,a->packed,a->bytes);
        }
        if(ok)ok=fgetc(f)==EOF&&!ferror(f);
    }
    dok_sha256_end(&hash,digest);if(ok)ok=!memcmp(digest,sounds_sha,32);
    fclose(f);packed_used=total;return ok;
}
