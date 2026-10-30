#include "dok/hotcode.h"
#include "dok/geometry.h"
#include "geometry_private.h"
#include <string.h>
_Static_assert(DOK_GEOMETRY_CELL_MAX>=8u,"all native branches fit without dropping faces");
_Static_assert(sizeof(dok_geometry_vertex)==20u && sizeof(dok_geometry_quad)==92u,
    "constructors assign every byte of each active quad, without padding");

DOK_NDS_HOT_CODE static int32_t signed_word(uint32_t value)
{
    value &= UINT32_C(65535);
    return value <= UINT32_C(32767) ? (int32_t)value : (int32_t)value-65536;
}

DOK_NDS_HOT_CODE static const dok_level_object *object_at(const dok_initial_world *world,
    uint32_t plane, uint32_t x, uint32_t z)
{
    const dok_level_definition *level=world->definition;
    uint32_t glyph=world->maps[plane][z*(uint32_t)level->settings[DOK_LEVEL_WIDTH]+x];
    uint32_t index=level->glyph_index[glyph];
    if (index==0u || index>level->object_count) return NULL;
    return &level->objects[index-1u];
}

DOK_NDS_HOT_CODE static int32_t field54(const dok_level_object *object,
    const dok_level_definition *level,const dok_geometry_field54 *prepared)
{
    const dok_level_field *field;
    if(prepared)return prepared[object-level->objects].value;
    field=dok_level_field_at(object,0x54u);
    return field==NULL ? 0 : signed_word((uint32_t)field->value);
}

DOK_NDS_HOT_CODE static uint32_t surface_definition(const dok_level_object *object,
    const dok_level_definition *level,const dok_geometry_field54 *prepared)
{
    const dok_level_field *field;
    if(prepared)return (object->class_id==0u || object->class_id==12u || object->class_id==13u)
        && prepared[object-level->objects].is_word;
    field=dok_level_field_at(object,0x54u);
    return (object->class_id==0u || object->class_id==12u || object->class_id==13u)
        && field!=NULL && field->kind==DOK_LEVEL_WORD;
}

DOK_NDS_HOT_CODE static uint32_t usable(const dok_geometry_image *image)
{
    return image->material!=0u && image->width!=0u && image->height!=0u
        && image->width<=32767u && image->height<=32767u;
}

DOK_NDS_HOT_CODE static void vertex(dok_geometry_vertex *out, int32_t x, int32_t y,
    int32_t z, int32_t u, int32_t v)
{
    out->x=x; out->y=y; out->z=z;
    out->u_q16=u*65536; out->v_q16=v*65536;
}

DOK_NDS_HOT_CODE static dok_geometry_quad *append(dok_geometry_cell *cell,
    const dok_geometry_image *image, const dok_level_object *object,
    uint32_t x, uint32_t z, uint32_t kind, uint32_t orientation)
{
    dok_geometry_quad *quad=&cell->quads[cell->count++];
    quad->material=image->material; quad->image_flags=image->flags;
    quad->object_flags=object->flags; quad->orientation=(uint16_t)orientation;
    quad->kind=(uint8_t)kind; quad->cell_x=(uint8_t)x;
    quad->cell_z=(uint8_t)z; quad->source_glyph=object->glyph;
    return quad;
}

DOK_NDS_HOT_CODE static void vertical(dok_geometry_quad *quad, int32_t x, int32_t y,
    int32_t z, int32_t width, int32_t height, uint32_t orientation,
    uint32_t centered, uint32_t mirror, const dok_geometry_image *image,
    int32_t v0, int32_t v1)
{
    int32_t ax=x,az=z,bx=x,bz=z;
    int32_t half=width>=0?width/2:(width-1)/2;
    if (centered!=0u) {
        if (orientation==0u) { az=z+half; bx=x+width; bz=az; }
        else { ax=x+half; bx=ax; bz=z+width; }
    } else {
        switch (orientation) {
        case 0u: bx=x+width; break;
        case 90u: az=z+width; break;
        case 180u: ax=x+width; az=z+width; bz=z+width; break;
        default: ax=x+width; bx=x+width; bz=z+width; break;
        }
    }
    /* Constructor mirror changes endpoint ordering independently of image20.
     * Vertices retain geometric order; reversing U preserves that mapping. */
    vertex(&quad->vertices[0],ax,y-height,az,mirror!=0u?(int32_t)image->width-1:0,v0);
    vertex(&quad->vertices[1],bx,y-height,bz,mirror!=0u?0:(int32_t)image->width-1,v0);
    vertex(&quad->vertices[2],bx,y,bz,mirror!=0u?0:(int32_t)image->width-1,v1);
    vertex(&quad->vertices[3],ax,y,az,mirror!=0u?(int32_t)image->width-1:0,v1);
}

DOK_NDS_HOT_CODE static void horizontal(dok_geometry_quad *quad, int32_t x, int32_t y,
    int32_t z, int32_t side, uint32_t ceiling, const dok_geometry_image *image)
{
    int32_t maxu=(int32_t)image->width-1,maxv=(int32_t)image->height-1;
    if (ceiling==0u) {
        vertex(&quad->vertices[0],x,y,z+side,0,0);
        vertex(&quad->vertices[1],x+side,y,z+side,maxu,0);
        vertex(&quad->vertices[2],x+side,y,z,maxu,maxv);
        vertex(&quad->vertices[3],x,y,z,0,maxv);
    } else {
        vertex(&quad->vertices[0],x,y,z,0,0);
        vertex(&quad->vertices[1],x+side,y,z,maxu,0);
        vertex(&quad->vertices[2],x+side,y,z+side,maxu,maxv);
        vertex(&quad->vertices[3],x,y,z+side,0,maxv);
    }
}

DOK_NDS_HOT_CODE static uint32_t door_like(const dok_level_object *object)
{
    return object->class_id==2u || object->class_id==4u || object->class_id==7u;
}

DOK_NDS_HOT_CODE static dok_geometry_status build_cell(const dok_initial_world *world,
    const dok_geometry_binding bindings[256], uint32_t x, uint32_t z,
    dok_geometry_cell *candidate,uint32_t full_clear,const dok_geometry_field54 *prepared)
{
    const dok_level_definition *level;
    const dok_level_object *tmap,*fcmap,*nt[4],*nf[4];
    const dok_geometry_image *image;
    uint32_t width,length,sideindex,nx[4],nz[4];
    int32_t side,cellheight,xx,zz,floor,inset,extent,scaledwidth,scaledheight;
    if (world==NULL || bindings==NULL || candidate==NULL || world->definition==NULL)
        return DOK_GEOMETRY_INVALID_ARGUMENT;
    level=world->definition;
    if (level->language!=1u || level->object_count==0u
        || level->object_count>DOK_LEVEL_MAX_OBJECTS
        || level->settings[DOK_LEVEL_WIDTH]<=0 || level->settings[DOK_LEVEL_LENGTH]<=0
        || level->settings[DOK_LEVEL_WIDTH]>64 || level->settings[DOK_LEVEL_LENGTH]>64
        || level->settings[DOK_LEVEL_CELL_SIDE]<=0 || level->settings[DOK_LEVEL_CELL_HEIGHT]<=0)
        return DOK_GEOMETRY_UNSUPPORTED;
    width=(uint32_t)level->settings[DOK_LEVEL_WIDTH];
    length=(uint32_t)level->settings[DOK_LEVEL_LENGTH];
    if (x>=width || z>=length) return DOK_GEOMETRY_INVALID_ARGUMENT;
    side=level->settings[DOK_LEVEL_CELL_SIDE];
    cellheight=level->settings[DOK_LEVEL_CELL_HEIGHT];
    tmap=object_at(world,DOK_WORLD_TMAP,x,z);
    fcmap=object_at(world,DOK_WORLD_FCMAP,x,z);
    if (tmap==NULL || fcmap==NULL) return DOK_GEOMETRY_UNSUPPORTED;
    if (surface_definition(fcmap,level,prepared)==0u)
        return DOK_GEOMETRY_UNSUPPORTED;
    nx[0]=x; nx[1]=x==0u?0u:x-1u; nx[2]=x; nx[3]=x+1u<width?x+1u:x;
    nz[0]=z==0u?0u:z-1u; nz[1]=z; nz[2]=z+1u<length?z+1u:z; nz[3]=z;
    for (sideindex=0u;sideindex<4u;sideindex++) {
        nt[sideindex]=object_at(world,DOK_WORLD_TMAP,nx[sideindex],nz[sideindex]);
        nf[sideindex]=object_at(world,DOK_WORLD_FCMAP,nx[sideindex],nz[sideindex]);
        if (nt[sideindex]==NULL || nf[sideindex]==NULL
            || surface_definition(nf[sideindex],level,prepared)==0u) return DOK_GEOMETRY_UNSUPPORTED;
    }
    if(full_clear)memset(candidate,0,sizeof(*candidate));
    else {candidate->count=0;candidate->reserved=0;}
    xx=(int32_t)x*side; zz=(int32_t)z*side;
    floor=signed_word(0u-(uint32_t)fcmap->height_parameter);
    inset=field54(fcmap,level,prepared);
    extent=signed_word((uint32_t)(cellheight+1-inset-fcmap->height_parameter));
    if (inset>=0) {
        image=&bindings[fcmap->glyph].images[DOK_GEOMETRY_CEILING_SIDE];
        for (sideindex=0u;sideindex<4u;sideindex++) {
            if (field54(nf[sideindex],level,prepared)<inset) {
                int32_t v1;
                dok_geometry_quad *quad;
                if (usable(image)==0u) return DOK_GEOMETRY_MISSING_BINDING;
                v1=signed_word((uint32_t)((int32_t)image->height*inset/cellheight));
                /* Native end=0 is a sentinel for imageHeight-1. */
                if (v1==0) v1=(int32_t)image->height-1;
                quad=append(candidate,image,fcmap,x,z,DOK_GEOMETRY_CEILING_BAND,sideindex*90u);
                vertical(quad,xx,signed_word((uint32_t)(inset-cellheight)),zz,side,inset,
                    sideindex*90u,0u,0u,image,0,v1);
            }
        }
    }
    if (tmap->class_id==2u) {
        image=&bindings[tmap->glyph].images[DOK_GEOMETRY_IMAGE];
        if (usable(image)==0u) return DOK_GEOMETRY_MISSING_BINDING;
        scaledwidth=signed_word((uint32_t)((int32_t)image->width*tmap->width_parameter));
        scaledheight=signed_word((uint32_t)((int32_t)image->height*tmap->width_parameter));
        for (sideindex=0u;sideindex<4u;sideindex++) {
            if (nt[sideindex]->class_id!=2u) {
                int32_t ni=field54(nf[sideindex],level,prepared);
                int32_t h=signed_word((uint32_t)(scaledheight-ni+1));
                int32_t v0=signed_word((uint32_t)((int32_t)image->height*ni/cellheight));
                dok_geometry_quad *quad=append(candidate,image,tmap,x,z,DOK_GEOMETRY_WALL,sideindex*90u);
                vertical(quad,xx,floor,zz,scaledwidth,h,sideindex*90u,0u,0u,image,v0,(int32_t)image->height-1);
            }
        }
    } else {
        image=&bindings[fcmap->glyph].images[DOK_GEOMETRY_FLOOR];
        if (usable(image)==0u) return DOK_GEOMETRY_MISSING_BINDING;
        horizontal(append(candidate,image,fcmap,x,z,DOK_GEOMETRY_FLOOR_QUAD,0u),xx,floor,zz,side,0u,image);
        if (inset>=0) {
            image=&bindings[fcmap->glyph].images[DOK_GEOMETRY_CEILING];
            if (usable(image)==0u) return DOK_GEOMETRY_MISSING_BINDING;
            horizontal(append(candidate,image,fcmap,x,z,DOK_GEOMETRY_CEILING_QUAD,0u),xx,floor-extent,zz,side,1u,image);
        }
        if (tmap->class_id==4u || tmap->class_id==7u) {
            uint32_t orient;
            image=&bindings[tmap->glyph].images[DOK_GEOMETRY_IMAGE];
            if (usable(image)==0u) return DOK_GEOMETRY_MISSING_BINDING;
            scaledwidth=signed_word((uint32_t)((int32_t)image->width*tmap->width_parameter));
            scaledheight=signed_word((uint32_t)((int32_t)image->height*tmap->width_parameter));
            for (orient=0u;orient<2u;orient++) {
                if (door_like(nt[orient])==0u && door_like(nt[orient+2u])==0u) {
                    uint32_t mirror=nt[orient==0u?1u:0u]->class_id==2u;
                    dok_geometry_quad *quad=append(candidate,image,tmap,x,z,DOK_GEOMETRY_DOOR,orient*90u);
                    vertical(quad,xx,floor,zz,scaledwidth,scaledheight,orient*90u,1u,mirror,image,0,(int32_t)image->height-1);
                }
            }
        }
    }
    return DOK_GEOMETRY_OK;
}

DOK_NDS_HOT_CODE dok_geometry_status dok_geometry_build_cell(const dok_initial_world *world,
    const dok_geometry_binding bindings[256],uint32_t x,uint32_t z,
    dok_geometry_cell *candidate,dok_geometry_cell *out)
{
    dok_geometry_status status;
    if(world==NULL || bindings==NULL || candidate==NULL || out==NULL ||
        candidate==out || world->definition==NULL)return DOK_GEOMETRY_INVALID_ARGUMENT;
    status=build_cell(world,bindings,x,z,candidate,1u,NULL);
    if(status==DOK_GEOMETRY_OK)*out=*candidate;
    return status;
}
DOK_NDS_HOT_CODE dok_geometry_status dok_geometry_build_cell_inplace(const dok_initial_world *world,
    const dok_geometry_binding bindings[256],uint32_t x,uint32_t z,dok_geometry_cell *cell)
{return build_cell(world,bindings,x,z,cell,0u,NULL);}

DOK_NDS_HOT_CODE dok_geometry_status dok_geometry_build_cell_prepared(const dok_initial_world *world,
    const dok_geometry_binding bindings[256],uint32_t x,uint32_t z,dok_geometry_cell *cell,
    const dok_geometry_field54 prepared[DOK_LEVEL_MAX_OBJECTS])
{return build_cell(world,bindings,x,z,cell,0u,prepared);}
