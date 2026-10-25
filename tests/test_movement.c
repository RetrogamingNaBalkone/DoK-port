#include <stdio.h>
extern unsigned dok_movement_cases(void);
int main(void) {
    unsigned error=dok_movement_cases();
    if(error) {fprintf(stderr,"Movement case line %u failed\n",error);return 1;}
    puts("Movement: input edges, immediate start/stop/reversal, wrapping, replay passed");return 0;
}
