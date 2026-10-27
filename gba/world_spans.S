.syntax unified
.cpu arm7tdmi
.arm
.section .text.world_spans,"ax",%progbits
.macro WORLD_KERNEL name, shifted, wrapped, coarse
.global \name
.type \name,%function
/* world_texture_span: dst,bits,pixels,LIGHT,wx,wz,dx,dz,mask,count,w-1,h-1.
 * The caller clips to one map cell; Q16 fractions are therefore texture UV.
 * For the floor, wz and dz are inverted by the caller to preserve its phase.
 * AAPCS; no interrupt masking, heap, per-pixel map search or division. */
\name:
    push {r4-r11,lr}
    sub sp,sp,#20
    mov lr,r0
    ldr r4,[lr,#16]
    ldr r5,[lr,#20]
    ldr r6,[lr,#24]
    ldr r7,[lr,#28]
    .if \coarse
    lsl r6,r6,#1
    lsl r7,r7,#1
    .endif
    ldr r8,[lr,#32]
    ldr r9,[lr,#36]
    ldr r10,[lr,#40]
    ldr r11,[lr,#44]
    .if \shifted
    ldr r12,[lr,#48]
    str r12,[sp,#4]
    ldr r12,[lr,#52]
    str r12,[sp,#8]
    .endif
    mov r12,#0
    str r12,[sp]
    ldmia lr,{r0-r3}
    .if \coarse
    str r2,[sp,#16]
    mov r2,#0
    .endif
1:
    ldr r12,[r1]
    .if \coarse
    orr lr,r8,r8,lsl #1
    and r12,r12,lr
    cmp r12,lr
    bne 6f
    cmp r9,#2
    blo 2f
    ldr r12,[r1,#20]
    and r12,r12,lr
    cmp r12,lr
    beq 2f
6:
    .else
    tst r12,r8
    bne 2f
    orr r12,r12,r8
    str r12,[r1]
    .endif
    mov r12,r4,lsl #16
    mov r12,r12,lsr #16
    mul r12,r10,r12
    mov r12,r12,lsr #16
    .if \shifted
    ldr lr,[sp,#4]
    add r12,r12,lr
    .endif
    mov lr,r5,lsl #16
    mov lr,lr,lsr #16
    mul lr,r11,lr
    mov lr,lr,lsr #16
    .if \shifted
    str r12,[sp,#12] @ preserve tx while loading the constant vertical shift
    ldr r12,[sp,#8]
    add lr,lr,r12
    ldr r12,[sp,#12]
    .if \wrapped
    cmp r12,#0
    addlt r12,r12,r10
    addlt r12,r12,#1
    cmp r12,r10
    subgt r12,r12,r10
    subgt r12,r12,#1
    cmp lr,#0
    addlt lr,lr,r11
    addlt lr,lr,#1
    cmp lr,r11
    subgt lr,lr,r11
    subgt lr,lr,#1
    .else
    cmp r12,r10
    bhi 4f
    cmp lr,r11
    bhi 4f
    .endif
    .endif
    mla lr,r10,lr,lr
    add lr,lr,r12
    .if \coarse
    ldr r12,[sp,#16]
    ldrb lr,[r12,lr]
    .else
    ldrb lr,[r2,lr]
    .endif
    .if \shifted
    .if !\wrapped
    b 5f
4:
    mov lr,#0
5:
    .endif
    .endif
    ldrb lr,[r3,lr]
    orr lr,lr,lr,lsl #8
    .if \coarse
    ldr r12,[r1]
    tst r12,r8
    strheq lr,[r0]
    addeq r2,r2,#1
    tst r12,r8,lsl #1
    strheq lr,[r0,#240]
    addeq r2,r2,#1
    orr r12,r12,r8
    orr r12,r12,r8,lsl #1
    str r12,[r1]
    cmp r9,#2
    blo 2f
    ldr r12,[r1,#20]
    tst r12,r8
    strheq lr,[r0,#2]
    addeq r2,r2,#1
    tst r12,r8,lsl #1
    strheq lr,[r0,#242]
    addeq r2,r2,#1
    orr r12,r12,r8
    orr r12,r12,r8,lsl #1
    str r12,[r1,#20]
    .else
    strh lr,[r0]
    ldr r12,[sp]
    add r12,r12,#1
    str r12,[sp]
    .endif
2:
    .if \coarse
    add r0,r0,#4
    add r1,r1,#40
    .else
    add r0,r0,#2
    add r1,r1,#20
    .endif
    add r4,r4,r6
    add r5,r5,r7
    .if \coarse
    subs r9,r9,#2
    bhi 1b
    mov r0,r2
    .else
    subs r9,r9,#1
    bne 1b
    ldr r0,[sp]
    .endif
    add sp,sp,#20
    pop {r4-r11,lr}
    bx lr
.size \name,.-\name
.endm
#ifdef DOK_RENDER_COARSE_PLANES_PROTOTYPE
WORLD_KERNEL dok_gba_world_texture_span,0,0,1
#else
WORLD_KERNEL dok_gba_world_texture_span,0,0,0
#endif
#ifdef DOK_RENDER_WATER_SPANS_PROTOTYPE
#ifdef DOK_RENDER_COARSE_PLANES_PROTOTYPE
WORLD_KERNEL dok_gba_world_texture_span_shift,1,0,1
WORLD_KERNEL dok_gba_world_texture_span_wrap,1,1,1
#else
WORLD_KERNEL dok_gba_world_texture_span_shift,1,0,0
WORLD_KERNEL dok_gba_world_texture_span_wrap,1,1,0
#endif
#endif
.global dok_gba_world_skip
.type dok_gba_world_skip,%function
/* Count consecutive covered pixel pairs; r0=column-bit pointer, r1=row mask,
 * r2=maximum count. All inputs are clipped by the C row mapper. */
dok_gba_world_skip:
    mov r12,#0
    cmp r2,#0
    beq 2f
1:
    ldr r3,[r0]
    and r3,r3,r1
    cmp r3,r1
    bne 2f
    add r0,r0,#20
    add r12,r12,#1
    subs r2,r2,#1
    bne 1b
2:
    mov r0,r12
    bx lr
.size dok_gba_world_skip,.-dok_gba_world_skip
#ifdef DOK_RENDER_COARSE_PLANES_PROTOTYPE
.balign 4
.global g_world_texture_coarse
g_world_texture_coarse:
    .word 1
#endif
