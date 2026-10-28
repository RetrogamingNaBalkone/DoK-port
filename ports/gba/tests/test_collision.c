#include "dok/runtime.h"
#include <stdio.h>
extern unsigned dok_collision_cases(dok_runtime *);
static dok_runtime world;
int main(void) {unsigned error=dok_collision_cases(&world);if(error) {fprintf(stderr,"Collision case line %u\n",error);return 1;}puts("Collision cases passed");return 0;}
