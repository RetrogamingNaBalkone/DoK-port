#include <stdio.h>
unsigned dok_time_cases(void);
int main(void) {
    unsigned line=dok_time_cases();
    if (line) { fprintf(stderr,"time case failed at line %u\n",line);return 1; }
    puts("time/scheduler edge cases passed");return 0;
}
