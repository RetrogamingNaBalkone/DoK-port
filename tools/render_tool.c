#include "dok/renderer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(const char *path,const void *data,size_t size) {
    FILE *f=fopen(path,"wb");int okay;if(!f) {perror(path);return 0;}
    okay=fwrite(data,1,size,f)==size;if(fclose(f)) okay=0;return okay;
}
int main(int argc,char **argv) {
    FILE *file;long length;uint8_t *bytes=0,palette[768];unsigned number,language,i;int result=1;
    dok_rom_assets assets;dok_runtime_level view;dok_runtime *runtime=0,*saved=0;
    dok_renderer_workspace *work=0;dok_renderer_stats stats,probe_stats;dok_projection_camera camera,probe_camera;
    dok_renderer_status status;int guards,unchanged,profile_probe,argument_probe;
    if(argc!=6 && argc!=10 && argc!=13) {fprintf(stderr,"Usage: dok-render assets.bin level(1..3) ru|en output.idx output.pal [x y z heading [door-glyph frame extent]]\n");return 2;}
    number=(unsigned)strtoul(argv[2],0,10);language=!strcmp(argv[3],"en");
    if(number<1 || number>3 || (strcmp(argv[3],"ru") && strcmp(argv[3],"en"))) return 2;
    file=fopen(argv[1],"rb");if(!file) {perror(argv[1]);return 1;}
    if(fseek(file,0,SEEK_END) || (length=ftell(file))<0 || fseek(file,0,SEEK_SET)) {fclose(file);return 1;}
    bytes=malloc((size_t)length);runtime=calloc(1,sizeof(*runtime));saved=calloc(1,sizeof(*saved));work=calloc(1,sizeof(*work));
    if(!bytes || !runtime || !saved || !work) {fclose(file);goto done;}
    if(fread(bytes,1,(size_t)length,file)!=(size_t)length) {fclose(file);goto done;}
    fclose(file);
    if(dok_rom_open(&assets,bytes,(size_t)length)!=DOK_ROM_OK ||
       dok_rom_level(&view,&assets,number-1,language)!=DOK_ROM_OK || dok_runtime_reset(runtime,&view)!=DOK_RUNTIME_OK) {
        fprintf(stderr,"Assets/level/reset failed\n");goto done;
    }
    status=dok_renderer_camera_from_runtime(&camera,runtime);
    if(status) {fprintf(stderr,"Camera status %u\n",(unsigned)status);goto done;}
    if(argc>=10) {
        camera.x=(int32_t)strtol(argv[6],0,10);camera.y=(int32_t)strtol(argv[7],0,10);
        camera.z=(int32_t)strtol(argv[8],0,10);dok_renderer_camera_heading(&camera,(int)strtol(argv[9],0,10));
    }
    if(argc==13) {
        unsigned glyph=(unsigned)strtoul(argv[10],0,10),frame=(unsigned)strtoul(argv[11],0,10),extent=(unsigned)strtoul(argv[12],0,10);
        if(glyph>255 || !view.glyph_index[glyph] || extent<1 || extent>32767 || frame>=extent) goto done;
        unsigned object=view.glyph_index[glyph]-1;if(view.definitions[object].class_id!=7) goto done;
        runtime->objects[object].frame=(uint16_t)frame;runtime->objects[object].state.door.extent=(int16_t)extent;
    }
    *saved=*runtime;work->frame_guard_before=UINT32_C(0xd04fcafe);work->frame_guard_after=UINT32_C(0xd04fcafe);
    probe_camera=camera;probe_camera.sine=32769;
    profile_probe=dok_renderer_draw(work,runtime,&assets,number-1,language,&probe_camera,&probe_stats)==DOK_RENDER_PROFILE;
    argument_probe=dok_renderer_draw(0,runtime,&assets,number-1,language,&camera,&probe_stats)==DOK_RENDER_ARGUMENT;
    if(!profile_probe || !argument_probe) {fprintf(stderr,"Renderer negative probes failed\n");goto done;}
    status=dok_renderer_draw(work,runtime,&assets,number-1,language,&camera,&stats);
    guards=work->frame_guard_before==UINT32_C(0xd04fcafe) && work->frame_guard_after==UINT32_C(0xd04fcafe);
    unchanged=!memcmp(saved,runtime,sizeof(*saved));
    if(status || !guards || !unchanged) {fprintf(stderr,"Render status %u guards %d runtime_unchanged %d\n",(unsigned)status,guards,unchanged);goto done;}
    if(dok_renderer_palette(palette,&assets,number-1,language)!=DOK_RENDER_OK ||
       !write_file(argv[4],work->frame,sizeof(work->frame)) || !write_file(argv[5],palette,sizeof(palette))) goto done;
    printf("{\"width\":240,\"height\":160,\"scene_height\":144,\"workspace_size\":%lu,\"camera\":{\"x\":%ld,\"y\":%ld,\"z\":%ld,\"sine\":%ld,\"cosine\":%ld,\"distance\":%ld,\"scale\":%d,\"focal\":%d,\"center_x\":%d,\"center_y\":%d},",
        (unsigned long)sizeof(*work),(long)camera.x,(long)camera.y,(long)camera.z,(long)camera.sine,(long)camera.cosine,(long)camera.distance,camera.scale,camera.focal,camera.center_x,camera.center_y);
    printf("\"stats\":{\"cells_considered\":%lu,\"wall_cells\":%lu,\"center_cells\":%lu,\"faces_built\":%lu,\"faces_drawn\":%lu,\"faces_rejected\":%lu,\"near_clipped\":%lu,\"pixels_written\":%lu,\"transparent_pixels\":%lu,\"skipped_billboards\":%lu,\"face_capacity\":%u,\"face_count\":%u},",
        (unsigned long)stats.cells_considered,(unsigned long)stats.wall_cells,(unsigned long)stats.center_cells,(unsigned long)stats.faces_built,
        (unsigned long)stats.faces_drawn,(unsigned long)stats.faces_rejected,(unsigned long)stats.near_clipped,(unsigned long)stats.pixels_written,
        (unsigned long)stats.transparent_pixels,(unsigned long)stats.skipped_billboards,stats.face_capacity,stats.face_count);
    printf("\"checks\":{\"frame_guards\":true,\"runtime_unchanged\":true,\"unsupported_camera\":true,\"null_workspace\":true},\"faces\":[");
    for(i=0;i<work->face_count;++i) {
        const dok_renderer_face *f=&work->faces[i];
        printf("%s{\"x0\":%ld,\"x1\":%ld,\"top0\":%ld,\"top1\":%ld,\"bottom0\":%ld,\"bottom1\":%ld,\"depth0\":%ld,\"depth1\":%ld,\"u0\":%ld,\"u1\":%ld,\"key\":%ld,\"image_index\":%u,\"glyph\":%u,\"v0\":%d}",i?",":"",
            (long)f->x0,(long)f->x1,(long)f->top0,(long)f->top1,(long)f->bottom0,(long)f->bottom1,(long)f->depth0,(long)f->depth1,
            (long)f->u0,(long)f->u1,(long)f->key,f->image_index,view.definitions[f->image_index].glyph,f->v0);
    }
    printf("]}\n");result=ferror(stdout)?1:0;
done:
    free(bytes);free(runtime);free(saved);free(work);return result;
}
