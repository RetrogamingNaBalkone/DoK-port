/* One draw epoch. Grid vertices are shared by wall faces, doors and ceiling
 * strips; cache their exact camera-space point and projection denominator.
 * Off-grid/non-map points and near-clipped endpoints use the original path. */
typedef struct {uint32_t tag;int32_t lateral,depth,factor,x;} geometry_vertex;
static geometry_vertex geometry_vertices[256];
static uint32_t geometry_epoch;
static const geometry_vertex *geometry_transform(const dok_projection_camera *c,
    int x,int y,int z,dok_camera_point *p) {
    if(x<0 || z<0 || x>30720 || z>30720 || x%240 || z%240) {
        dok_projection_transform(c,x,y,z,p);return 0;
    }
    unsigned key=(unsigned)(z/240)*129+(unsigned)(x/240);
    geometry_vertex *v=&geometry_vertices[(key^(key>>5))&255];
    uint32_t tag=(geometry_epoch<<15)|key;
    if(v->tag!=tag) {
        dok_screen_point screen;
        dok_projection_transform(c,x,0,z,p);
        v->lateral=p->lateral;v->depth=p->depth;
        if(dok_projection_project(c,p,&screen)) {v->factor=screen.factor;v->x=screen.x;}
        else {v->factor=INT_MIN;v->x=0;}
        v->tag=tag;
    }
    p->lateral=v->lateral;p->depth=v->depth;p->vertical=sub(y,c->y);return v;
}
static int geometry_project(const dok_projection_camera *c,const dok_camera_point *p,
    const geometry_vertex *v,dok_screen_point *out) {
    if(!v) return dok_projection_project(c,p,out);
    if(v->factor==INT_MIN) return 0;
    out->x=v->x;out->y=add(sar(mul(p->vertical,v->factor),15),c->center_y);
    out->factor=v->factor;return 1;
}
