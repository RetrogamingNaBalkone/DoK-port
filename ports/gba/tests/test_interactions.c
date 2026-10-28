#include "dok/interactions.h"
#include <stdio.h>
extern unsigned dok_interactions_cases(dok_runtime *,dok_interactions *);
static dok_runtime world;
static dok_interactions interactions;
int main(void) {unsigned error=dok_interactions_cases(&world,&interactions);
 if(error) {fprintf(stderr,"Interactions case line %u\n",error);return 1;}
 puts("Interactions: locks, phases, shared glyph, occupancy, sensor edges, class6, autoclose, finish, conveyor passed");return 0;}
