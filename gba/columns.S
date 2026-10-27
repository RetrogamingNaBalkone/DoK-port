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

#ifdef DOK_RENDER_HALF_COLUMNS
.global dok_gba_surface_span
.type dok_gba_surface_span,%function
/* surface_span ABI is asserted in renderer_surface_raster.inc. All source
 * coordinates retain signed Q16 arithmetic and exact clamp-before-sample.
 * The bitmap has one 5-word column per displayed pair; zero is opaque here.
 * No calls, no interrupts masked, original ROM bytes and LIGHT table retained. */
dok_gba_surface_span:
    ldr r1,[r0,#20]
    ldr r2,[r0,#24]
    cmp r1,r2
    movhi r0,#0
    bxhi lr
    push {r4-r11,lr}
    sub sp,sp,#4
    mov r12,#0
    str r12,[sp]
    mov lr,r0
    ldmia lr,{r1,r2,r12}
    ldr r3,[lr,#32]
    ldr r4,[lr,#36]
    ldr r5,[lr,#40]
    ldr r6,[lr,#44]
    lsl r5,r5,#1
    lsl r6,r6,#1
    ldr r7,[lr,#12]
    ldr r8,[lr,#16]
    sub r7,r7,#1
    sub r8,r8,#1
    ldr r9,[lr,#48]
    ldr r10,[lr,#52]
    ldr r11,[lr,#24]
    ldr r0,[lr,#20]
    sub r11,r11,r0
    mov r11,r11,lsr #1
    add r11,r11,#1
    mov r0,r12
1:
    ldr r12,[r9]
    tst r12,r10
    bne 2f
    orr r12,r12,r10
    str r12,[r9]
    mov r12,r3,asr #16
    cmp r12,#0
    movlt r12,#0
    cmp r12,r7
    movgt r12,r7
    mov lr,r4,asr #16
    cmp lr,#0
    movlt lr,#0
    cmp lr,r8
    movgt lr,r8
    mla lr,r7,lr,lr
    add lr,lr,r12
    ldrb lr,[r1,lr]
    ldrb lr,[r2,lr]
    orr lr,lr,lr,lsl #8
    strh lr,[r0]
    ldr r12,[sp]
    add r12,r12,#1
    str r12,[sp]
2:
    add r0,r0,#2
#ifdef DOK_HW_COVER4
    add r9,r9,#16
#else
    add r9,r9,#20
#endif
    add r3,r3,r5
    add r4,r4,r6
    subs r11,r11,#1
    bne 1b
    ldr r0,[sp]
    add sp,sp,#4
    pop {r4-r11,lr}
    bx lr
.size dok_gba_surface_span,.-dok_gba_surface_span
.ltorg
#endif
