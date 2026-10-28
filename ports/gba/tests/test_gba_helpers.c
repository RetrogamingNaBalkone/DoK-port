#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

extern unsigned __aeabi_uidiv(unsigned, unsigned);
extern int __aeabi_idiv(int, int);
extern void __aeabi_memcpy4(void *, const void *, size_t);
extern void __aeabi_memclr4(void *, size_t);

int main(void)
{
    static const unsigned values[] = {0,1,2,3,31,32,33,65535,65536,0x7fffffffu,0x80000000u,0xffffffffu};
    uint32_t seed = 0x12345678;
    unsigned char source[67], target[69];
    for (unsigned i = 0; i < sizeof(values)/sizeof(values[0]); ++i)
        for (unsigned j = 1; j < sizeof(values)/sizeof(values[0]); ++j)
            assert(__aeabi_uidiv(values[i], values[j]) == values[i]/values[j]);
    for (unsigned i = 0; i < 10000; ++i) {
        seed = seed*1664525u + 1013904223u;
        unsigned numerator = seed;
        seed = seed*1664525u + 1013904223u;
        unsigned denominator = seed ? seed : 1;
        assert(__aeabi_uidiv(numerator, denominator) == numerator/denominator);
        int sn = numerator <= INT32_MAX ? (int)numerator : -1 - (int)~numerator;
        int sd = denominator <= INT32_MAX ? (int)denominator : -1 - (int)~denominator;
        if (sd && !(sn == INT32_MIN && sd == -1))
            assert(__aeabi_idiv(sn, sd) == sn/sd);
    }
    for (unsigned i = 0; i < sizeof(source); ++i) source[i] = (unsigned char)(i*37);
    for (unsigned n = 0; n <= sizeof(source); ++n) {
        for (unsigned i = 0; i < sizeof(target); ++i) target[i] = 0xa5;
        __aeabi_memcpy4(target+1, source, n);
        assert(target[0] == 0xa5 && target[n+1] == 0xa5);
        for (unsigned i = 0; i < n; ++i) assert(target[i+1] == source[i]);
        __aeabi_memclr4(target+1, n);
        assert(target[0] == 0xa5 && target[n+1] == 0xa5);
        for (unsigned i = 0; i < n; ++i) assert(target[i+1] == 0);
    }
    assert(__aeabi_idiv(INT32_MIN, 1) == INT32_MIN);
    assert(__aeabi_idiv(INT32_MIN, INT32_MIN) == 1);
    assert(__aeabi_idiv(-3, 2) == -1 && __aeabi_idiv(3, -2) == -1);
    puts("GBA EABI: signed/unsigned division oracle and bounded copy/clear passed");
    return 0;
}
