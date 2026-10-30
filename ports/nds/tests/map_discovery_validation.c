/* Reuse fixture loading and the three independently compiled implementations. */
#define main map_discovery_original_differential_main
#include "map_discovery_equivalence.c"
#undef main

static uint32_t validation_cases,error_cases;
extern dok_status baseline_dok_map_record_geometry(const dok_geometry_quad *,
    const dok_nds_render_camera *,dok_map_render_record *);
static int dead_field_cases(void)
{
    uint32_t n,i,j;
    for(n=0;n<2048u;++n) {
        dok_nds_render_camera camera={0};dok_map_render_record expected;
        dok_status expected_status=DOK_STATUS_OK;dok_geometry_quad q;
        int32_t xs[4],zs[4];uint32_t heading=random32()%361u;
        uint16_t object_flags=(uint16_t)random32();
        if(!dok_render_trig(heading,&camera.sin_q15,&camera.cos_q15))return 30;
        for(i=0;i<4u;++i) {
            xs[i]=(int32_t)(random32()%131072u)-65536;
            zs[i]=(int32_t)(random32()%131072u)-65536;
        }
        for(j=0;j<4u;++j) {
            dok_map_render_record record,old;dok_status status,old_status;
            memset(&q,(int)(j*85u),sizeof(q));
            q.kind=(uint8_t)(n%5u);q.cell_x=(uint8_t)(n&63u);q.cell_z=(uint8_t)((n>>6)&63u);
            q.object_flags=object_flags;
            for(i=0;i<4u;++i){q.vertices[i].x=xs[i];q.vertices[i].z=zs[i];}
            memset(&record,0xa5,sizeof(record));memset(&old,0xa5,sizeof(old));
            status=dok_map_record_geometry(&q,&camera,&record);
            old_status=baseline_dok_map_record_geometry(&q,&camera,&old);
            if(!j){expected=record;expected_status=status;}
            if(status!=expected_status || status!=old_status || memcmp(&record,&expected,sizeof(record)) ||
                memcmp(&record,&old,sizeof(record)))return 31;
        }
    }
    return 0;
}
static int validation_case(void)
{
    uint32_t angle;
    for(angle=0;angle<360u;angle+=90u) {
        dok_nds_render_camera camera={0};dok_map_discovery_stats a,b,c,d;
        dok_status sa,sb,sc,sd;
        camera.x=(int32_t)definition.settings[DOK_LEVEL_WIDTH]*240;
        camera.z=(int32_t)definition.settings[DOK_LEVEL_LENGTH]*240;camera.y=-300;
        if(!dok_render_trig(angle,&camera.sin_q15,&camera.cos_q15))return 20;
        memset(&baseline,0,sizeof(baseline));memset(&candidate4,0,sizeof(candidate4));
        memset(&candidate5,0,sizeof(candidate5));
        memset(&optimized,0,sizeof(optimized));
        memset(before,0,sizeof(before));memset(after,0,sizeof(after));memset(middle,0,sizeof(middle));memset(compact,0,sizeof(compact));
        memset(&a,0xa5,sizeof(a));memset(&b,0xa5,sizeof(b));memset(&c,0xa5,sizeof(c));memset(&d,0xa5,sizeof(d));
        sa=baseline_dok_map_discovery_update(&world,bindings,NULL,NULL,&camera,angle,&baseline,before,&a);
        sb=dok_map_discovery_update(&world,bindings,NULL,NULL,&camera,angle,&optimized,after,&b);
        sc=candidate4_dok_map_discovery_update(&world,bindings,NULL,NULL,&camera,angle,&candidate4,middle,&c);
        sd=candidate5_dok_map_discovery_update(&world,bindings,NULL,NULL,&camera,angle,&candidate5,compact,&d);
        if(sa!=sb || sc!=sb || memcmp(before,after,sizeof(before)) || memcmp(middle,after,sizeof(middle)) ||
            memcmp(&a,&b,sizeof(a)) || memcmp(&c,&b,sizeof(c)) ||
            memcmp(baseline.bytes,&optimized,sizeof(baseline.bytes)) ||
            memcmp(candidate4.bytes,&optimized,sizeof(candidate4.bytes)) || sd!=sb ||
            memcmp(compact,after,sizeof(compact)) || memcmp(&d,&b,sizeof(d)) ||
            memcmp(candidate5.bytes,&optimized,sizeof(candidate5.bytes))) {
            fprintf(stderr,"Validation mismatch %u angle %u statuses %u/%u/%u\n",
                validation_cases,angle,(unsigned)sa,(unsigned)sb,(unsigned)sc);return 21;
        }
        ++validation_cases;if(sb!=DOK_STATUS_OK)++error_cases;
    }
    return 0;
}

int main(int argc,char **argv)
{
    uint32_t i,j,k;int result=map_discovery_original_differential_main(argc,argv);
    if(result)return result;
    if((result=dead_field_cases()))return result;
    for(i=0;i<256u;++i)for(j=0;j<4u;++j) {
        dok_geometry_image saved=bindings[i].images[j];
        if(!saved.material && !saved.width && !saved.height)continue;
        for(k=0;k<5u;++k) {
            bindings[i].images[j]=saved;
            if(k==0)bindings[i].images[j].material=0;
            else if(k==1)bindings[i].images[j].width=0;
            else if(k==2)bindings[i].images[j].height=0;
            else if(k==3)bindings[i].images[j].width=32768;
            else bindings[i].images[j].height=32768;
            if((result=validation_case()))return result;
        }
        bindings[i].images[j]=saved;
    }
    for(i=0;i<256u;++i)if(definition.glyph_index[i]) {
        uint16_t saved=definition.glyph_index[i];
        definition.glyph_index[i]=0;if((result=validation_case()))return result;
        definition.glyph_index[i]=(uint16_t)(definition.object_count+1u);
        if((result=validation_case()))return result;
        definition.glyph_index[i]=saved;
    }
    for(i=0;i<definition.object_count;++i) {
        dok_level_object *o=&definition.objects[i];
        int16_t saved_width=o->width_parameter;uint16_t saved_fields=o->field_count;
        static const int16_t widths[]={-32768,-1,0,1,32767};
        for(j=0;j<sizeof(widths)/sizeof(widths[0]);++j) {
            o->width_parameter=widths[j];if((result=validation_case()))return result;
        }
        o->width_parameter=saved_width;o->field_count=DOK_LEVEL_MAX_FIELDS+1u;
        if((result=validation_case()))return result;o->field_count=saved_fields;
        for(j=0;j<o->field_count;++j)if(o->fields[j].native_offset==0x54u) {
            dok_level_field saved=o->fields[j];
            static const int32_t insets[]={-32768,-1,0,1,32767,INT32_MIN,INT32_MAX};
            o->fields[j].kind=DOK_LEVEL_LONG;if((result=validation_case()))return result;
            o->fields[j]=saved;
            for(k=0;k<sizeof(insets)/sizeof(insets[0]);++k) {
                o->fields[j].value=insets[k];if((result=validation_case()))return result;
            }
            o->fields[j]=saved;
        }
    }
    {
        uint8_t language=definition.language;uint16_t objects=definition.object_count;
        definition.language=0;if((result=validation_case()))return result;
        definition.language=2;if((result=validation_case()))return result;definition.language=language;
        definition.object_count=0;if((result=validation_case()))return result;
        definition.object_count=DOK_LEVEL_MAX_OBJECTS+1u;if((result=validation_case()))return result;
        definition.object_count=objects;
    }
    for(i=0;i<5u;++i)if(i!=DOK_LEVEL_HEIGHT) {
        int16_t saved=definition.settings[i];
        definition.settings[i]=0;if((result=validation_case()))return result;
        definition.settings[i]=-1;if((result=validation_case()))return result;
        if(i==DOK_LEVEL_WIDTH || i==DOK_LEVEL_LENGTH) {
            definition.settings[i]=65;if((result=validation_case()))return result;
        }
        definition.settings[i]=saved;
    }
    printf("{\"passed\":true,\"validation_cases\":%u,\"error_cases\":%u,"
        "\"dead_field_vectors\":8192}\n",validation_cases,error_cases);
    return 0;
}
