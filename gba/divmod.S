.syntax unified
.cpu arm7tdmi
.arm

/* EABI divmod returns quotient in r0 and remainder in r1. An ordinary C
 * struct return would use a hidden pointer on this ABI, so use ARM wrappers.
 * LLD supplies ARM/Thumb interworking veneers for the C division helpers. */
.section .text.divmod,"ax",%progbits
.global __aeabi_uidivmod
.type __aeabi_uidivmod,%function
__aeabi_uidivmod:
    push {r4,r5,r6,lr}
    mov r4,r0
    mov r5,r1
    bl __aeabi_uidiv
    mul r6,r0,r5
    sub r1,r4,r6
    pop {r4,r5,r6,lr}
    bx lr
.size __aeabi_uidivmod,.-__aeabi_uidivmod

.global __aeabi_idivmod
.type __aeabi_idivmod,%function
__aeabi_idivmod:
    push {r4,r5,r6,lr}
    mov r4,r0
    mov r5,r1
    bl __aeabi_idiv
    mul r6,r0,r5
    sub r1,r4,r6
    pop {r4,r5,r6,lr}
    bx lr
.size __aeabi_idivmod,.-__aeabi_idivmod

.global gba_divmod_selftest
.type gba_divmod_selftest,%function
gba_divmod_selftest:
    push {r4,r5,r6,r7,r8,lr}
    ldr r4,=unsigned_vectors
    mov r5,#8
1:  ldmia r4!,{r0,r1,r2,r3}
    mov r6,r2
    mov r7,r3
    bl __aeabi_uidivmod
    cmp r0,r6
    cmpeq r1,r7
    bne 4f
    subs r5,r5,#1
    bne 1b
    ldr r4,=signed_vectors
    mov r5,#8
2:  ldmia r4!,{r0,r1,r2,r3}
    mov r6,r2
    mov r7,r3
    bl __aeabi_idivmod
    cmp r0,r6
    cmpeq r1,r7
    bne 4f
    subs r5,r5,#1
    bne 2b
    mov r0,#0
3:  pop {r4,r5,r6,r7,r8,lr}
    bx lr
4:  mov r0,#1
    b 3b
.size gba_divmod_selftest,.-gba_divmod_selftest
.ltorg
.balign 4
unsigned_vectors:
    .word 0,1,0,0
    .word 1,1,1,0
    .word 3,2,1,1
    .word 100,10,10,0
    .word 0xffffffff,1,0xffffffff,0
    .word 0xffffffff,2,0x7fffffff,1
    .word 0xffffffff,0x80000000,1,0x7fffffff
    .word 0x80000000,3,0x2aaaaaaa,2
signed_vectors:
    .word -3,2,-1,-1
    .word 3,-2,-1,1
    .word -3,-2,1,-1
    .word 0x80000000,1,0x80000000,0
    .word 0x80000000,0x80000000,1,0
    .word 0x80000000,3,-715827882,-2
    .word 0x7fffffff,-1,-2147483647,0
    .word 0,-1,0,0
