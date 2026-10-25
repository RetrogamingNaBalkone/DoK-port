.syntax unified
.cpu arm7tdmi
.arm
.section .text.columns,"ax",%progbits
.global dok_gba_mapped_column
.type dok_gba_mapped_column,%function
/* r0=EWRAM dst, r1=ROM column, r2=Q16 v, r3=Q16 step,
 * stack[0]=count. Opaque samples; stride 240. AAPCS, returns count.
 * Magnification can reuse a texel, with exactly the same Q16 indices. */
dok_gba_mapped_column:
    push {r4,r5,r6,lr}
    ldr r4,[sp,#16]
    mov lr,r4
    cmp r3,#32768
    bhs 4f
    mvn r5,#0
    cmp r4,#16
    blo 2f
1:
    .rept 16
    mov ip,r2,lsr #16
    cmp ip,r5
    ldrbne r6,[r1,ip]
    mov r5,ip
#ifdef DOK_RENDER_HALF_COLUMNS
    orr ip,r6,r6,lsl #8
    strh ip,[r0],#240
#else
    strb r6,[r0],#240
#endif
    add r2,r2,r3
    .endr
    sub r4,r4,#16
    cmp r4,#16
    bhs 1b
2:
    cmp r4,#0
    beq 8f
3:
    mov ip,r2,lsr #16
    cmp ip,r5
    ldrbne r6,[r1,ip]
    mov r5,ip
#ifdef DOK_RENDER_HALF_COLUMNS
    orr ip,r6,r6,lsl #8
    strh ip,[r0],#240
#else
    strb r6,[r0],#240
#endif
    add r2,r2,r3
    subs r4,r4,#1
    bne 3b
    b 8f
4:
    cmp r4,#16
    blo 6f
5:
    .rept 16
    ldrb ip,[r1,r2,lsr #16]
    add r2,r2,r3
#ifdef DOK_RENDER_HALF_COLUMNS
    orr ip,ip,ip,lsl #8
    strh ip,[r0],#240
#else
    strb ip,[r0],#240
#endif
    .endr
    sub r4,r4,#16
    cmp r4,#16
    bhs 5b
6:
    cmp r4,#0
    beq 8f
7:
    ldrb ip,[r1,r2,lsr #16]
    add r2,r2,r3
#ifdef DOK_RENDER_HALF_COLUMNS
    orr ip,ip,ip,lsl #8
    strh ip,[r0],#240
#else
    strb ip,[r0],#240
#endif
    subs r4,r4,#1
    bne 7b
8:
    mov r0,lr
    pop {r4,r5,r6,lr}
    bx lr
.size dok_gba_mapped_column,.-dok_gba_mapped_column
