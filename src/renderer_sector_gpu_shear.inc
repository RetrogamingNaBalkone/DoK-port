/* Largest feasible source rectangles, PB0 and PC shear. The fit checks every
 * integer mip-U boundary against the actual quantized PA/center, and limits
 * native height variation before replacing a perspective trapezoid. */
static GPU_CODE unsigned sector_gpu_shear_face(dok_renderer_workspace *w,const dok_renderer_face *f,
    const dok_renderer_image *im,const sector_gpu_record *rec,int q0,int q1,int crop_x,int crop_y,unsigned level) {
    unsigned width=8u<<level,height=16u<<level;int projected[65],dx=f->x1-f->x0;
    for(unsigned k=0;k<=width;++k)projected[k]=sector_gpu_inverse(f,(int)k*(im->width-1)/(int)(width-1)+crop_x,q0,q1);
    for(unsigned sx=0;sx<width;) {
        unsigned columns=width-sx,step=0,bw=8,bh=8,max_error=0,dh=0;
        int xa=projected[sx],xe=0,center=0,top=0,pd=0,pa=0,pc=0;
        if(xa==INT_MAX || xa< -32768 || xa>32768){++g_sector_gpu_work[8];return 0;}
        for(;;) {
            ++g_sector_gpu_shear_work[0];xe=projected[sx+columns];int span=xe-xa,abs_span=span<0?-span:span;
            bw=columns<=8?8:columns<=16?16:columns<=32?32:64;bh=bw>=32?64:bw==16?32:32;
            center=(xa+xe)/2;
            if(xe==INT_MAX || xe< -32768 || xe>32768 || !span || abs_span>(int)(bw*2-2))goto smaller;
            int ha=f->bottom0-f->top0+(f->bottom1-f->top1-f->bottom0+f->top0)*(xa-f->x0)/dx;
            int hb=f->bottom0-f->top0+(f->bottom1-f->top1-f->bottom0+f->top0)*(xe-f->x0)/dx;
            dh=(unsigned)(ha>hb?ha-hb:hb-ha);if(dh>2)goto smaller;
            pa=sector_gpu_round((int)columns*256,span);if(!pa || pa>32767 || pa< -32767)goto smaller;
            int pad_x=(int)(bw-columns)/2;
            int hardware_center=center-sector_gpu_round((pad_x+(int)columns/2-(int)bw/2)*256,pa);max_error=0;
            for(unsigned k=0;k<=columns;++k) {
                int expected=projected[sx+k];if(expected==INT_MAX)goto smaller;
                int fitted=hardware_center+sector_gpu_round(((int)k+pad_x-(int)bw/2)*256,pa);
                unsigned error=(unsigned)(expected>fitted?expected-fitted:fitted-expected);
                if(error>2)goto smaller;if(error>max_error)max_error=error;
            }
            top=f->top0+(f->top1-f->top0)*(center-f->x0)/dx;
            int bottom=f->bottom0+(f->bottom1-f->bottom0)*(center-f->x0)/dx,full_h=bottom-top;
            if(full_h<=0)goto smaller;
            int effective=(int)(height-1)*(im->height-1-f->v0)/(im->height-1);
            pd=sector_gpu_round(effective*228,full_h);if(pd<1 || pd>32767)goto smaller;
            int delta_top=f->top1-f->top0,scale=pd*256;
            if(delta_top>65535 || delta_top< -65535 || (delta_top<0?-delta_top:delta_top)>INT_MAX/scale)goto smaller;
            pc=sector_gpu_round(-delta_top*scale,dx*228);if(pc>32767 || pc< -32767)goto smaller;
            unsigned dbl=(pa<256&&pa> -256)||pd<256||pc;
            int max_rows=(pd*((int)(bh*(dbl?2:1))-4)-(pc<0?-pc:pc)*abs_span)/256;
            if(max_rows<1)goto smaller;
            step=(unsigned)max_rows;if(step>bh)step=bh;
            break;
smaller:
            ++g_sector_gpu_shear_work[5];
            if(columns==1){
                if(projected[sx+1]==xa){++sx;columns=0;break;}
                ++g_sector_gpu_work[9];return 0;
            }
            columns=(columns+1)/2;
        }
        if(!columns)continue;
        if(max_error>g_sector_gpu_shear_work[2])g_sector_gpu_shear_work[2]=max_error;
        if(dh>g_sector_gpu_shear_work[3])g_sector_gpu_shear_work[3]=dh;
        unsigned abs_pc=(unsigned)(pc<0?-pc:pc);if(abs_pc>g_sector_gpu_shear_work[4])g_sector_gpu_shear_work[4]=abs_pc;
        int depth=f->depth0+(f->depth1-f->depth0)*(center-f->x0)/dx;if(depth<0)depth=0;
        unsigned shade=light_index(depth);const uint8_t *source=dok_sector_gpu_bank+dok_sector_gpu_offsets[rec->first+shade*4+level];
        sector_gpu_current_pc=pc;
        for(unsigned sy=0;sy<height;sy+=step) {
            unsigned rows=height-sy;if(rows>step)rows=step;
            int source_center=(int)sy+(int)rows/2+(crop_y-f->v0)*(int)(height-1)/(im->height-1);
            int center_y=sector_gpu_round(top*256,228)+sector_gpu_round(source_center*256,pd);
            unsigned before=g_sector_gpu->count;
            if(!sector_gpu_piece(w,source,width,sx,sy,columns,rows,center,center_y,pa,pd,f,w->lights+shade*256,(im->flags&8)?0:g_hw_tile_black))return 0;
            g_sector_gpu_shear_work[1]+=g_sector_gpu->count-before;
        }
        sx+=columns;
    }
    ++g_sector_gpu_work[0];return 1;
}
