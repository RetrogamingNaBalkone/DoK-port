#include <stddef.h>

/* Freestanding ARM EABI helpers. Volatile accesses prevent recognition of the
 * implementation loop as a recursive call to the same compiler helper. */
void __aeabi_memcpy(void *destination, const void *source, size_t count)
{
    volatile unsigned char *d = destination;
    const volatile unsigned char *s = source;
    while (count--) *d++ = *s++;
}
void __aeabi_memcpy4(void *destination, const void *source, size_t count)
{
    __aeabi_memcpy(destination, source, count);
}
void __aeabi_memcpy8(void *destination, const void *source, size_t count)
{
    __aeabi_memcpy(destination, source, count);
}
void __aeabi_memclr(void *destination, size_t count)
{
    volatile unsigned char *d = destination;
    while (count--) *d++ = 0;
}
void __aeabi_memclr4(void *destination, size_t count)
{
    __aeabi_memclr(destination, count);
}
unsigned __aeabi_uidiv(unsigned numerator, unsigned denominator)
{
    unsigned quotient = 0, bit = 1;
    /* Reader divisors are validated positive. Match the usual EABI zero default. */
    if (!denominator) return 0;
    while (!(denominator & 0x80000000u) && denominator < numerator) {
        denominator <<= 1;
        bit <<= 1;
    }
    do {
        if (numerator >= denominator) {
            numerator -= denominator;
            quotient |= bit;
        }
        denominator >>= 1;
        bit >>= 1;
    } while (bit);
    return quotient;
}
int __aeabi_idiv(int numerator, int denominator)
{
    unsigned n = numerator < 0 ? 0u - (unsigned)numerator : (unsigned)numerator;
    unsigned d = denominator < 0 ? 0u - (unsigned)denominator : (unsigned)denominator;
    unsigned q = __aeabi_uidiv(n, d);
    if ((numerator < 0) != (denominator < 0)) q = 0u - q;
    return q <= 0x7fffffffu ? (int)q : -1 - (int)~q;
}
