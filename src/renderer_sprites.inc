/* G11 software billboards. Original constructor: RU10b0:0ccb/EN1040:0ce3.
 * Shared coverage compositor applies LIGHT before transparency; no OBJ/OAM. */
static DOK_COLD dok_renderer_status billboard_image(dok_renderer_workspace *w,const dok_runtime *r,
 const dok_projection_camera *c,dok_renderer_stats *s,uint16_t object,uint16_t texture,
 const dok_renderer_image *im,int32_t x,int32_t y,int32_t z) {
 const dok_runtime_definition *d=&r->level->definitions[object];
 int width=word(im->width*d->width_parameter),height=word(im->height*d->width_parameter);
 if(width<=0 || height<=0 || width>4096 || height>4096) return DOK_RENDER_PROFILE;
 dok_camera_point p;dok_screen_point left,right,bottom;dok_renderer_face f;
 dok_projection_transform(c,x,y-height,z,&p);
 if(p.depth<=0) {++s->faces_rejected;return DOK_RENDER_OK;}
 int32_t middle=p.lateral;p.lateral=sub(middle,sar(width,1));
 if(!dok_projection_project(c,&p,&left)) return DOK_RENDER_PROJECTION;
 p.lateral=add(middle,sar(width,1));
 if(!dok_projection_project(c,&p,&right)) return DOK_RENDER_PROJECTION;
 p.vertical=add(p.vertical,height);
 if(!dok_projection_project(c,&p,&bottom)) return DOK_RENDER_PROJECTION;
 if(left.x< -8192 || right.x>8192 || left.y< -8192 || bottom.y>8192) return DOK_RENDER_PROFILE;
 if(left.x>=right.x || right.x<0 || left.x>319 || bottom.y<0 || left.y>159) return DOK_RENDER_OK;
 f.x0=scale_x(left.x);f.x1=scale_x(right.x);f.top0=f.top1=scale_y(left.y);
 f.bottom0=f.bottom1=scale_y(bottom.y);f.depth0=f.depth1=p.depth;
 f.u0=0;f.u1=im->width-1;f.key=p.depth-sar(width,2);
 if(f.key<0) f.key=0;f.image_index=texture;f.v0=0;
 if(f.x0==f.x1) return DOK_RENDER_OK;
 if(w->face_count>=DOK_RENDER_FACE_CAPACITY) return DOK_RENDER_CAPACITY;
 w->faces[w->face_count++]=f;++s->faces_built;return DOK_RENDER_OK;
}
static DOK_COLD dok_renderer_status billboard(dok_renderer_workspace *w,const dok_runtime *r,
 const dok_rom_assets *a,unsigned level,unsigned language,const dok_projection_camera *c,
 dok_renderer_stats *s,uint16_t object,int32_t x,int32_t y,int32_t z) {
 dok_renderer_status status=image(w,r,a,level,language,object);if(status)return status;
 return billboard_image(w,r,c,s,object,object,&w->images[object],x,y,z);
}
static DOK_COLD dok_renderer_status cell_sprites(dok_renderer_workspace *w,const dok_runtime *r,
 const dok_rom_assets *a,unsigned level,unsigned language,const dok_projection_camera *c,
 dok_renderer_stats *s,int x,int z,uint16_t fc,uint16_t tmap) {
 uint16_t ids[2]={tmap,0};
 if(!object_at(r,DOK_WORLD_MMAP,x,z,&ids[1])) return DOK_RENDER_PROFILE;
 for(unsigned plane=0;plane<2;++plane) {
  unsigned id=ids[plane],cls=r->level->definitions[id].class_id;
  if(r->objects[id].flags&0x400)continue;
  if((plane && cls!=3 && cls!=9) || (!plane && cls!=3 && cls!=9 && cls!=6 && cls!=10)) continue;
  const dok_runtime_definition *d=&r->level->definitions[id];
  int32_t wx=x*480+240,wz=z*480+240;
  if(cls==9) {const dok_runtime_actor *actor=dok_runtime_actor_at(r,(uint16_t)id);
   if(!actor) return DOK_RENDER_PROFILE;wx=actor->x;wz=actor->z;}
  int32_t y=word(-r->level->definitions[fc].height_parameter-d->height_parameter);
  dok_renderer_status status=billboard(w,r,a,level,language,c,s,(uint16_t)id,wx,y,wz);
  if(status) return status;
 }
 return DOK_RENDER_OK;
}
