#include "file_reader.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

static dok_asset_status read_at(void *context,uint32_t offset,unsigned char *dst,
                                uint32_t count,uint32_t *actual)
{
    NdsAssetFile *f=context;
    ++f->calls;if(count>f->max_request) f->max_request=count;
    if(f->test_fault_mode!=0u && offset==f->test_fault_offset) {
        if(f->test_fault_mode==2u) return DOK_ASSET_IO_ERROR;
        *actual=count!=0u?count-1u:0u;return DOK_ASSET_OK;
    }
    if(offset>f->bytes || count>f->bytes-offset || offset>(uint32_t)LONG_MAX ||
       count>DOK_ASSET_READ_MAX)
        return DOK_ASSET_IO_ERROR;
    if(f->catalog_cache&&offset>=160u&&offset<f->catalog_end&&count<=f->catalog_end-offset){
        uint32_t relative=offset-160u,page=relative/4096u,within=relative%4096u,slot=page%8u;
        if(count<=4096u-within){
            if(f->cache_tags[slot]!=page){
                uint32_t start=160u+page*4096u,n=f->catalog_end-start;if(n>4096u)n=4096u;
                if(fseek(f->file,(long)start,SEEK_SET))return DOK_ASSET_IO_ERROR;
                size_t got=fread(f->catalog_cache+slot*4096u,1,n,f->file);
                ++f->storage_reads;f->bytes_read+=(uint32_t)got;f->position=start+(uint32_t)got;
                if(got!=n)return DOK_ASSET_IO_ERROR;f->cache_tags[slot]=page;
            }else ++f->cache_hits;
            memcpy(dst,f->catalog_cache+slot*4096u+within,count);*actual=count;return DOK_ASSET_OK;
        }
    }
    if(f->position!=offset&&fseek(f->file,(long)offset,SEEK_SET))return DOK_ASSET_IO_ERROR;
    *actual=(uint32_t)fread(dst,1,count,f->file);f->bytes_read+=*actual;
    ++f->storage_reads;f->position=offset+*actual;
    return ferror(f->file)?DOK_ASSET_IO_ERROR:DOK_ASSET_OK;
}
dok_asset_status nds_asset_file_open(NdsAssetFile *f,const char *path,dok_asset_reader *out)
{
    NdsAssetFile value;dok_asset_reader reader;long bytes;
    if(f==NULL || path==NULL || out==NULL) return DOK_ASSET_INVALID_ARGUMENT;
    memset(&value,0,sizeof(value));value.file=fopen(path,"rb");
    if(value.file==NULL) return errno==ENOENT?DOK_ASSET_NOT_FOUND:DOK_ASSET_IO_ERROR;
    if(fseek(value.file,0,SEEK_END)!=0 || (bytes=ftell(value.file))<0) {
        fclose(value.file);return DOK_ASSET_IO_ERROR;
    }
    value.bytes=(uint32_t)bytes;value.position=value.bytes;reader.context=f;reader.read_at=read_at;reader.file_bytes=value.bytes;
    *f=value;*out=reader;return DOK_ASSET_OK;
}
void nds_asset_file_cache(NdsAssetFile *f,unsigned char *backing,uint32_t entries)
{
    if(!f||!backing||entries>DOK_ASSET_MAX_ENTRIES)return;
    f->catalog_cache=backing;f->catalog_end=160u+entries*128u;
    for(unsigned i=0;i<8u;++i)f->cache_tags[i]=UINT32_MAX;
}
void nds_asset_file_close(NdsAssetFile *f)
{
    if(f!=NULL && f->file!=NULL) {fclose(f->file);memset(f,0,sizeof(*f));}
}
