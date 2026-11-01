/* Offline G27 admission probe; compiled ARM7TDMI, not linked into production.
 * One bounded pass over already projected faces, before texture/alpha access.
 * The bbox estimate is only an early filter: it does not check scanline
 * pressure and can false-accept. It is not an optimum or a capacity guarantee. */
typedef unsigned int u32;
typedef struct { int n[11]; unsigned short object, flags; } Face;
__attribute__((section(".text.entry")))
unsigned g27_admit(const Face *faces, const unsigned char *classes,
                   unsigned count, u32 *work) {
    unsigned total=0, visits=0;
    for(unsigned i=0;i<count;++i) {
        ++visits;
        if(classes[i]==2)continue;
        const int *f=faces[i].n;
        int x0=f[0]<f[1]?f[0]:f[1], x1=f[0]>f[1]?f[0]:f[1];
        int y0=f[2]<f[3]?f[2]:f[3], y1=f[4]>f[5]?f[4]:f[5];
        x0=x0<0?0:x0/2;x1=x1>239?119:x1/2;
        y0=y0<0?0:y0;y1=y1>127?127:y1;
        if(x1>=x0 && y1>=y0)total+=(((unsigned)(x1/8-x0/8)+4)/4)*((unsigned)(y1/8-y0/8)+1);
        if(total>64){work[0]=visits;work[1]=total;return 0;}
    }
    work[0]=visits;work[1]=total;return 1;
}
